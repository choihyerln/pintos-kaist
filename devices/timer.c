#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* 부팅된 이후의 timer ticks = kernel tick + idle tick */
static int64_t ticks;

/* 타이머 틱 당 루프 수
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;		// 8254칩의 주파수

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* brief delays를 구현하는 데 사용되는 loops_per_tick를 보정 */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* 운영 체제 부팅 이후의 타이머 틱 수를 반환 */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();	// 인터럽트 비활성화
	int64_t t = ticks;
	intr_set_level (old_level);		// 인터럽트를 원래대로 돌려놓는 작업
	barrier ();		// 순서에 의존해야 해서 최적화 장벽 사용
	return t;
}

/* THEN 이후로 경과한 타이머 틱 수를 반환
   THEN(t)은 이전에 timer_ticks()로 얻은 값 */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;	// start로부터 흐른 시간 반환
}

/* TICKS 타이머 틱 동안 실행을 일시 중단 */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
	while (timer_elapsed (start) < ticks)
		thread_yield ();
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* 간단한 지연을 구현하기 위해 LOOPS번 반복하는 단순한 루프를 순회

  NO_INLINE로 표시되어 있으며 코드 정렬이 시간에 중요한 영향을 미칠 수 있으므로
  이 함수가 서로 다른 위치에서 다르게 인라인화되면 결과를 예측하기 어려울 수 있기 때문 */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* NUM/DENOM 초 시간 동안 sleep 수행, 정확한 시간 지연을 구현하는 데 사용 */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);	// 현재 인터럽트 상태 확인
	if (ticks > 0) {
		/* 최소한 하나의 전체 타이머 틱을 기다린다.
		  다른 프로세스에 CPU를 양보하기 위해 timer_sleep()을 사용하자 */
		timer_sleep (ticks);	// 최소한 하나의 전체 타이머 틱 동안 대기
	} else {
		/* 그렇지 않으면 보다 정확한 서브-틱 타이밍을 위해 busy_wait 루프를 사용
		  오버플로우 가능성을 피하기 위해 분자와 분모를 1000으로 축소 */
		ASSERT (denom % 1000 == 0);
		// 얼마나 긴 시간을 busy_wait하도록 할 것인지 계산
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}

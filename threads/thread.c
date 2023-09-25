#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list sleep_list;		// sleep queue

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 락 */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* CPU가 아무런 작업을 수행하지 않고 대기하는 시간을 측정하는 데 사용 */
static long long kernel_ticks;  /* 커널 스레드가 CPU를 사용한 시간을 추적 (main) */
static long long user_ticks;    /* 사용자 프로그램이 CPU를 사용한 시간을 추적 */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* 마지막으로 실행된 스레드가 사용한 시간 */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
void thread_sleep(int64_t wake_time);
bool compare_priority(struct list_elem *me, struct list_elem *you, void *aux);
void thread_wake(int64_t now_ticks);

static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an (하드웨어, 디바이스 인터럽트)external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 주어진 초기 우선순위 PRIORITY로 이름이 NAME, FUNCTION을 실행하고,
AUX를 인수로 전달하는 새로운 커널 스레드를 생성하고, 이를 준비 큐에 추가
새 스레드의 스레드 식별자를 반환하며, 생성에 실패한 경우 TID_ERROR를 반환

만약 thread_start()가 호출된 경우,
새로운 스레드는 thread_create()가 반환되기 전에 스케줄될 수 있음
심지어 thread_create()가 반환되기 전에 종료될 수도 있다.
반면에 원래 스레드는 새 스레드가 스케줄되기 전에 어떤 시간이든 실행될 수 있습니다.
순서를 보장해야 하는 경우 세마포어 또는 다른 형태의 동기화를 사용해야 한다.

제공된 코드는 새 스레드의 'priority' 멤버를 PRIORITY로 설정하지만
실제로 우선순위 스케줄링은 구현되어 있지 않다. 우선순위 스케줄링은 문제 1-3의 목표이다.*/

tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);		// 로 삽입 시 priority order

	return tid;
}
/*
	sleep_list 에 삽입시 우선순위(tick 오름차순) 정렬 --> 갓벽..!
*/
bool
compare_priority(struct list_elem *me, struct list_elem *you, void *aux) {
	/* list entry :  */
	// me/you 라는 elem 을 이용해 해당 스레드의 시작점을 알기 위해 list entry 사용 (return struct thread *)
	struct thread *me_t = list_entry(me, struct thread, elem);
	struct thread *you_t = list_entry(you, struct thread, elem);
	// me_t가 더 작아야지 우선순위가 높기 때문에, list_insert_ordered 함수에서 ture를 반환
	return me_t->end_tick < you_t->end_tick;	// true
}

void
thread_wake(int64_t now_ticks) {
	if(!list_empty(&sleep_list)) {
		struct list_elem *front_elem = list_front(&sleep_list);
		struct thread *sleep_front = list_entry(front_elem, struct thread, elem);

		if(now_ticks >= sleep_front->end_tick) {	// 깨워야 할 시간이 지나면
			list_pop_front(&sleep_list);			// sleep 리스트에서 빼주고
			thread_unblock(sleep_front);			// unblock 시켜줌
		}
	}
}

void
thread_sleep(int64_t wake_time ){
	// do_schedule , schedule 때문에 터짐...ㅋ
	// old_level = intr_disable ();
	ASSERT (!intr_context ());					// 인터럽트를 처리하고 있지 않아야 하고,
	ASSERT (intr_get_level () == INTR_OFF);		// 인터럽트 상태가 OFF
	struct thread * curr = thread_current();
	curr->status = THREAD_BLOCKED;
	curr->end_tick = wake_time;
	list_insert_ordered(&sleep_list, &(thread_current ()->elem), compare_priority, NULL);

	schedule ();

}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());		// 인터럽트를 처리하고 있지 않아야 하고,
	ASSERT (intr_get_level () == INTR_OFF);		// 인터럽트 상태가 OFF
	thread_current ()->status = THREAD_BLOCKED;
	//(struct list *, struct list_elem *, list_less_func *, void *aux)

	schedule ();
}

/* 차단된 스레드 T를 실행 대기 상태로 전환
T가 차단되지 않은 경우에는 오류이다.
(현재 실행 중인 스레드를 실행 대기 상태로 만들려면 thread_yield()를 사용하십시오.)

이 함수는 현재 실행 중인 스레드를 선점하지 않는다.
호출자가 인터럽트를 비활성화한 상태에서 스레드를 차단 해제하고
다른 데이터를 업데이트할 수 있다고 기대할 수 있기 때문이다.*/
bool
comparing(struct list_elem *me, struct list_elem *you, void *aux) {
	/* list entry :  */
	// me/you 라는 elem 을 이용해 해당 스레드의 시작점을 알기 위해 list entry 사용 (return struct thread *)
	struct thread *me_t = list_entry(me, struct thread, elem);
	struct thread *you_t = list_entry(you, struct thread, elem);
	// me_t가 더 작아야지 우선순위가 높기 때문에, list_insert_ordered 함수에서 ture를 반환
	return me_t->priority < you_t->priority;	// true
}
void
thread_unblock (struct thread *t) {
	// t : lock 의 waiters 중 우선순위가 높은 BLOCKED 상태의 스레드
	/*
	unblock 은 release_lock 를 실행했을 때를 전제로 이루어지는 것이다. (donation 은 lock_aquire 일때)
	따라서 lock 의 waiters 에 있던 block 상태의 스레드를 ready_list 에 넣고 READY 상태(아직 실행x) 로 만들어 줘야 할 필요가 있다.
	이때 release 한 스레드가 return 되기 전에 ready_list 에서 우선순위가 높은 스레드를 꺼내 yield 를 시켜줘야 한다.
	1. current 가 우선순위가 높은 경우도  yield-> current을 ready_list에 넣기 -> schedule 해준다.
	next~에 current 가 튀어 나오지만 schdeule 했을 때 current-current문맥 교환 걱정 없다 -> 왜냐하면 launch 까지 못가도록 로직이 짜여 있기 때문
	2. current 보다 우선순위가 높은 경우도 위와 같다.
	next~에 다른 스레드가 나오고 문맥교환이 이뤄진다.
	위에서 ready_list 마지막(?)에 넣는다. release 한 스레드여도 문제 없다. thread_create 인자 중 func, aux 의 func을 보자
	fun 에는 kernel thread? 자기 할일을 끝내면 자동으로 exit 을 실행하기 때문이다.
	

	*/
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);

	list_insert_ordered(&ready_list, &t->elem, comparing, NULL);
	// list_push_back (&ready_list, &t->elem);
	t->status = THREAD_READY;
	/* 현재 실행중인 쓰레드보다 높은 우선순위를 가진 쓰레드가 ready list에 추가되면 현재 쓰레드는 즉시 프로세서를 새 쓰레드에게 양보 */
	thread_yield();
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim.
   
   */
void
thread_yield (void) {
	// 현재 실행중인 쓰레드보다 높은 우선순위를 가진 쓰레드가 ready list에 추가
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);
	do_schedule (THREAD_READY);
	// thread_current ()->status = THREAD_READY;
	// schedule()
	intr_set_level (old_level);
}

/* 현재 스레드의 우선순위 = NEW_PRIORITY
   현재 스레드의 우선순위를 설정하고 ready_list 정렬 */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
}

/* 현재 스레드의 우선순위 반환 */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
	/* 우선순위가 높은 스레드를 꺼내기 + 우선순위 origin 으로 복귀 */
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {		// 새로운 프로세스를 스케줄 하는 과정, READY, DYING
	ASSERT (intr_get_level () == INTR_OFF);		// 인터럽트 비활성화
	ASSERT (thread_current()->status == THREAD_RUNNING);	// 현재 진행 중인
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *  = running_thread ();		// 현재 실행중인 스레드인 주소
	struct thread *next = next_thread_to_run ();	// 다음에 실행될 스레드인 주소

	  (intr_get_level () == INTR_OFF);		// 인터럽트 X
	ASSERT (curr->status != THREAD_RUNNING);	// 러닝상태가 아니어야하고
	ASSERT (is_thread (next));					// next가 유효한 thread인지
	/* Mark us as running. */
	next->status = THREAD_RUNNING;				// next를 running상태로 만들어줌

	/* Start new time slice. */
	thread_ticks = 0;							// 마지막으로 실행된 스레드가 사용한 시간 = 0으로 초기화

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif
	// current 의 우선순위가 unblock 요청 들어온 스레드보다 우선순위가 높아 ready_list 에 들어온 상태인 경우 curr과 next가 일치하는 상황 발생할 수 있음
	if (curr != next) {
		/* 만약 우리가 스위칭한 스레드가 종료 중인 경우, 해당 스레드의 struct thread를 파괴한다.
		   이것은 thread_exit()가 자신의 발을 잡아당기지 않도록 늦게 발생해야 한다. 
		   여기에서는 페이지 해제 요청을 대기열에 추가하는 것만 수행한다.
		   왜냐하면 현재 페이지는 스택에서 사용 중이기 때문이다.
		   실제 파괴 로직은 schedule()의 시작 부분에서 호출될 것이다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* 새로운 스레드에 사용할 스레드 ID를 반환 */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

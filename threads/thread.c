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

/* Lock used by allocate_tid(). */
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
bool compare_ticks(struct list_elem *me, struct list_elem *you, void *aux);
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
	struct lock *lock;
	
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
실제로 우선순위 스케줄링은 구현되어 있지 않다. 우선순위 스케줄링은 문제 1-3의 목표이다. */
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

	t->parent = thread_current();

#ifdef USERPROG
	struct child_info *child_info = (struct child_info *) malloc(sizeof(struct child_info));
	child_info->tid = tid;
	child_info->exit_status = 0;
	child_info->child_t = t;
	child_info->exited = false;
	list_push_back(&thread_current()->child_list, &child_info->c_elem);

	t->fd_cnt = 2;                       // 표준 입출력 0,1 제외
    t->fd_table = palloc_get_page(0);    // fd table 초기화
#endif

	thread_unblock (t);		// 자식을 ready list 에 넣기
	if (t->priority >= thread_current()->priority)
		thread_yield();	   // 현재 실행중인 스레드 = 부모, readylist 에 있는 스레드 = 자식
	return tid;
}

/* sleep_list 에 삽입시 우선순위(tick 오름차순) 정렬 --> 갓벽..! */
bool
compare_ticks(struct list_elem *me, struct list_elem *you, void *aux) {
	// me/you 라는 elem 을 이용해 해당 스레드의 시작점을 알기 위해 list entry 사용 (return struct thread *)
	struct thread *me_t = list_entry(me, struct thread, elem);
	struct thread *you_t = list_entry(you, struct thread, elem);
	// me_t가 더 작아야지 우선순위가 높기 때문에, list_insert_ordered 함수에서 ture를 반환
	return me_t->end_tick < you_t->end_tick;
}

/* 우선순위 비교 (숫자 클수록 우선순위 높음) */
bool
compare_priority(struct list_elem *me, struct list_elem *you, void *aux) {
	struct thread *lock_holder = list_entry(me, struct thread, elem);
	struct thread *lock_requester = list_entry(you, struct thread, elem);
	return lock_holder->priority > lock_requester->priority;
}

/* donate 시 우선순위 비교 (숫자 클수록 우선순위 높음) */
bool
donate_compare_priority(struct list_elem *me, struct list_elem *you, void *aux) {
	struct thread *lock_holder = list_entry(me, struct thread, d_elem);
	struct thread *lock_requester = list_entry(you, struct thread, d_elem);
	return lock_holder->priority > lock_requester->priority;
}

/* 자신의 priority를 lock holder에게 donation 해주는 함수 */
void
donation_priority(void) {
	struct thread *curr = thread_current();

	while (curr->want_lock) {
		struct thread *holder = curr->want_lock->holder;	// curr가 요청한 락의 홀더
		if (holder->priority < curr->priority) {
			// holder->priority = list_entry(list_min(&holder->donation_list, donate_compare_priority, 0), struct thread, d_elem)->priority;
			holder->priority = curr->priority;	// donation, curr는 실행중이므로 이미 홀더보다 우선순위 높음
			curr = holder;
		}
	}
}

/* donation_list에서 스레드 지우는 함수 */
void
remove_thread_in_donation_list (struct lock *lock) {
	struct list_elem *delem;

	for (delem=list_begin(&lock->holder->donation_list);
		delem!=list_end(&lock->holder->donation_list); delem=list_next(delem)) {
			struct thread *remove_t = list_entry(delem, struct thread, d_elem);	// 삭제할 스레드
			if (remove_t->want_lock == lock)
				list_remove(delem);
	}
}

/* priority 재설정하는 함수 */
void
reset_priority(void) {
	struct thread *curr = thread_current();		// lock holder

	curr->priority = curr->origin_priority;
	
	if (!list_empty(&curr->donation_list)) {
		// list_sort(&curr->donation_list, donate_compare_priority, NULL);
		struct thread *max_thread = list_entry(list_min(&curr->donation_list, compare_priority, 0), struct thread, d_elem);
		if (max_thread->priority > curr->priority)
			curr->priority = max_thread->priority;
	}
}

void
thread_wake(int64_t now_ticks) {
    while (!list_empty(&sleep_list)) {
    	struct list_elem *front_elem = list_front(&sleep_list);
        struct thread *sleep_thread = list_entry(front_elem, struct thread, elem);
		
		// 현재 시각이 일어날 시간을 지났으면 -> 일어나!
        if (now_ticks >= sleep_thread->end_tick) { 
			struct thread *wake_thread = list_entry(list_pop_front(&sleep_list), struct thread, elem);
            thread_unblock(sleep_thread);
        }
        else {
            break;
        }
    }
}

void
thread_sleep(int64_t wake_time) {
	enum intr_level old_level = intr_disable();	// 인터럽트 비활성화
	ASSERT (!intr_context ());		// 인터럽트를 처리하고 있지 않아야 하고,
	ASSERT (intr_get_level () == INTR_OFF);		// 인터럽트 상태가 OFF

	struct thread *curr = thread_current();
	curr->end_tick = wake_time;		// block하는 구조체 깨울 시간 저장
	list_insert_ordered(&sleep_list, &(curr->elem), compare_ticks, NULL);	// sleep 리스트에 삽입정렬

	thread_block ();				// block하고 스케줄링
	intr_set_level(old_level);		// 인터럽트 다시 활성화
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
	schedule ();
}

/* 차단된 스레드 T를 실행 대기 상태로 전환
T가 차단되지 않은 경우에는 오류이다.
(현재 실행 중인 스레드를 실행 대기 상태로 만들려면 thread_yield()를 사용하십시오.)
이 함수는 현재 실행 중인 스레드를 선점하지 않는다.
호출자가 인터럽트를 비활성화한 상태에서 스레드를 차단 해제하고
다른 데이터를 업데이트할 수 있다고 기대할 수 있기 때문이다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	
	// ready_list에 넣을 때, list_insert_ordered 사용하기
	list_insert_ordered(&ready_list, &(t->elem), compare_priority, NULL);


	t->status = THREAD_READY;

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

/* CPU 양보
   현재 스레드는 sleep 상태로 전환되지 않으며
   스케줄러의 재량에 따라 즉시 다시 스케줄 될 수 있음 */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();

	if (curr != idle_thread) {
		list_insert_ordered(&ready_list, &(curr->elem), compare_priority, NULL);
	}

	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* 현재 스레드의 우선순위 = NEW_PRIORITY
   현재 스레드의 우선순위를 설정하고 ready_list 정렬 */
void
thread_set_priority (int new_priority) {

	struct thread *curr = thread_current();
	curr->origin_priority = new_priority;
	reset_priority();
	thread_yield();
}

/* 현재 스레드의 우선순위 반환 */
int
thread_get_priority (void) {
	return thread_current()->priority;
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
	// sema init
#ifdef USERPROG
	sema_init(&t->fork_sema, 0);
	sema_init(&t->wait_sema, 0);
	list_init(&t->child_list);
#endif
	
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	t->origin_priority = priority;
	list_init(&t->donation_list);	// donation_list init
	t->want_lock = NULL;			// want_lock init
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
	struct thread *curr = running_thread ();		// 현재 실행중인 스레드인 주소
	struct thread *next = next_thread_to_run ();	// 다음에 실행될 스레드인 주소

	ASSERT (intr_get_level () == INTR_OFF);		// 인터럽트 X
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
		// printf("--- 그 다음 실행되는 애: %d\n", next->priority);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H
#define FDT_COUNT_LIMIT 128

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */

struct child_info{
	tid_t pid;						/* 똥강아지 주민번호 */
	int8_t is_alive;				/* 죽었니 살았니?   */
	int8_t status;					/* 똥강아지 상태    */
	struct thraed* parent_thread	/* 부모스레드 주소   */
	struct list_elem c_elem;        /* 똥강아지 elem   */ 
};
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	int origin_priority;				/* 기존 priority */

	/* Shared between thread.c and synch.c. */
	// alam
	int64_t end_tick;					/* End tick: alarm 할 때 쓴 거 */

	// priority schedule
	struct list donation_list;			/* 나한테 기부해준 스레드 담을 리스트 */
	struct lock *want_lock;				/* 해당 스레드가 원하는 lock이 뭔지 알아야 함 */
	struct list_elem d_elem;			/* donation_list init될 때 사용되는 elem */
	struct list_elem elem;              /* ready list가 init될 때 사용되는 elem */

	// syscall
	struct file **fd_table;				/* 파일의 배열 */
	int fd_cnt;							/* 한 프로세스 당 몇개의 파일이 열려있는지 카운트 */
	struct list child_list;				/* 똥강아지들의 정보(구조체) 리스트 */

	struct semaphore wait_sema;			/* 자식 프로세스의 생성 대기 */
	struct semaphore exit_sema;			/* 자식 프로세스의 졸료 대기*/

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_sleep(int64_t wake_time);
void thread_wake(int64_t now_ticks);
void thread_block (void);
void thread_unblock (struct thread *);
bool compare_ticks(struct list_elem *me, struct list_elem *you, void *aux);
bool compare_priority(struct list_elem *me, struct list_elem *you, void *aux);

/* donation시 필요한 함수 */
bool donate_compare_priority(struct list_elem *me, struct list_elem *you, void *aux);
void donation_priority(void);
void remove_thread_in_donation_list (struct lock *lock);
void reset_priority(void);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */

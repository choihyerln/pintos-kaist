#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore {
	unsigned value;        /* 현재 세마포어 값 */
	struct list waiters;   /* 세마포어를 기다리는 스레드 목록을 관리하기 위한 연결 리스트, 세마포어를 기다리며 차단된 스레드들이 들어감 */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock {
	struct thread *holder;      /* 현재 락을 소유한 스레드 (for debugging). */
	struct semaphore semaphore; /* 락을 구현하기 위해 이진 세마포어를 활용한 구조체 */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);
bool cond_compare_priority (const struct list_elem *a, const struct list_elem *b, void *aux);

/* 최적화 장벽
 *
 * 컴파일러는 최적화 장벽을 통해 연산을 재배열하지 않는다.
 * 자세한 정보는 '참조 가이드'의 '최적화 장벽'을 참조 */
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */

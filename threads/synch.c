/* 이 파일은 나초스(Nachos) 교육용 운영 체제의 소스 코드를 기반으로 하였습니다. 나초스의 저작권 고지가 아래에 완전히 재현되어 있습니다. */

/* 저작권 (c) 1992-1996 캘리포니아 대학 교수회. 모든 권리 보유.

이 소프트웨어 및 그 문서를 복사, 수정, 배포, 사용하는 권한은 금액이나 서면 합의 없이도 허용됩니다.
단, 상기의 저작권 고지와 아래 두 단락이 이 소프트웨어의 모든 복사본에 나타나야 합니다.

캘리포니아 대학교는 본 소프트웨어 및 이의 문서의 사용으로 인해 발생하는 직접, 간접, 특수, 부수적 
또는 결과적 손해에 대해 어떤 당사자에 대해서도 책임을 지지 않습니다.

캘리포니아 대학교는 명시적이나 묵시적으로든 상품성이나 특정 목적에 대한 묵시적 보증을 포함하여 
어떠한 종류의 보증도 명시적으로 거부합니다. 여기에 제공되는 소프트웨어는 "있는 그대로" 기반으로 하며,
캘리포니아 대학교에는 유지, 지원, 업데이트, 향상 또는 수정의 의무가 없습니다.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"


/* 세마포어 SEMA를 VALUE로 초기화
   세마포어는 음수가 아닌 정수와 그 값을 조작하는
   두 가지 원자적(atomic) 연산을 포함한다.

   - down or "P": 값이 양수가 될 때까지 기다린 다음 값을 감소시킴

   - up or "V": 값을 증가시킴
   (그리고 대기 중인 스레드가 있는 경우 그 중 하나를 깨움) */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);		// 세마리스트 초기화
}

/* 세마포어에 대한 Down or "P" 연산
   SEMA의 값이 양수가 될 때까지 기다린 다음 값을 원자적으로 감소시킴

   이 함수는 sleep할 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안됨!
   인터럽트가 비활성화된 상태에서 호출할 수 있지만, 슬립하는 경우
   스케줄된 스레드가 아마도 인터럽트를 다시 활성화할 것이다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;
	struct thread *run_curr = thread_current();

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());
	old_level = intr_disable ();	// 인터럽트 비활성화

	while (sema->value == 0) {
		list_push_back(&sema->waiters, &run_curr->elem);
		thread_block ();	// 세마 = 0일 때, 요청 들어오면 세마리스트에 추가 후 block 처리

	}
	sema->value--;			// sema = 1일 때
	intr_set_level (old_level);		// 인터럽트 상태 반환
}

/* 세마포에 대한 "P" 연산 또는 감소 연산을 수행하되, 세마포가 이미 0이 아닌 경우에만 수행한다. 
   세마포가 감소되면 true를 반환하고, 그렇지 않으면 false를 반환

   이 함수는 인터럽트 핸들러에서 호출할 수 있습니다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;			// 인터럽트가 on인지 off인지
	bool success;						// 

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포에 대한 "V" 연산 또는 증가 연산을 수행합니다. SEMA의 값을 증가시키고,
   SEMA를 기다리는 스레드 중 하나를 깨웁니다(있는 경우).

   이 함수는 인터럽트 핸들러에서 호출할 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();

	if (!list_empty (&sema->waiters))	{// waiters에 들어있을 때
		list_sort(&sema->waiters, compare_priority, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));	// unblock처리 -> ready list로 옮겨줌
	}
	sema->value++;	// sema 값 증가
	thread_yield();		// unblock 일어나므로 양보 작업 해줘야 함
	intr_set_level (old_level); 
}

static void sema_test_helper (void *sema_);

/* 쌍의 스레드 간에 제어를 "ping-pong" 하도록 하는 세마포에 대한 자체 테스트입니다.
   무슨 일이 벌어지는지 확인하려면 printf() 호출을 삽입하십시오.*/
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수입니다. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* 락(LOCK)을 초기화합니다. 락은 언제나 최대 하나의 스레드만 보유할 수 있습니다.
우리의 락은 '재귀적'이지 않습니다.
즉, 현재 락을 보유한 스레드가 해당 락을 다시 얻으려고 시도하는 것은 오류입니다.

락은 초기 값이 1인 세마포어의 특별한 경우입니다.
락과 이러한 세마포어 간의 차이는 두 가지입니다.
첫째, 세마포어는 1보다 큰 값을 가질 수 있지만 락은 한 번에 하나의 스레드만 소유할 수 있습니다.
둘째, 세마포어는 소유자(owner)가 없으며,
즉 하나의 스레드가 세마포어를 '다운'하고 다른 스레드가 '업'할 수 있지만
락은 동일한 스레드가 락을 획득하고 해제해야 합니다.
이러한 제한이 부담스러울 때, 세마포어를 사용해야 하는 좋은 신호입니다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}


/* LOCK 요청
 * lock을 획득하거나, 누군가 lock을 보유하고 있다면 사용 가능해질 때까지 대기
 * 현재 스레드가 LOCK을 이미 보유하고 있으면 안 됩니다.
 * 이 함수는 대기 상태로 들어갈 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
 * 이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만,
 * 대기가 필요한 경우 인터럽트가 다시 활성화됩니다. */
void
lock_acquire (struct lock *lock) {

	struct thread *curr = thread_current();

	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	if (lock->holder) {
		curr->want_lock = lock;	// acquire 요청한 스레드의 want_lock 설정
		/* lock->holder->donate_priority < curr->priority
			curr가 실행되고 있다는 자체로 lock holder보다
			우선순위가 높다는 뜻이기 때문에 이 조건은 없어도 됨 */
		list_push_back(&lock->holder->donation_list, &curr->d_elem);
		donation_priority();
	}
	// sema_down을 기점으로 이전은 lock을 얻기 전, 이후는 lock을 얻은 후
	sema_down (&lock->semaphore);

	curr->want_lock = NULL;
	lock->holder = curr;
	// waiters에서 최댓값 뽑아서 holder에게 donation
	if (!list_empty(&lock->semaphore.waiters)) {
		struct thread *max_t = list_entry(list_min(&lock->semaphore.waiters, compare_priority, 0), struct thread, elem);
		if (lock->holder->origin_priority < max_t->priority) {
			list_push_back(&lock->holder->donation_list, &max_t->d_elem);
			lock->holder->priority = max_t->priority;
		}
	}
}



/* LOCK을 획득을 시도하고, 성공한 경우에는 true를 반환하며 실패한 경우에는 false를 반환
 * 현재 스레드가 LOCK을 이미 보유하고 있으면 안 됩니다.
 * 이 함수는 대기하지 않으므로 인터럽트 핸들러 내에서 호출될 수 있습니다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

void
donator_remove(struct lock *lock){
	for (struct list_elem *e = list_begin (&lock->holder->donators); e != list_end (&lock->holder->donators); e = list_next (e)){

		struct thread *d_t = list_entry(e, struct thread, d_elem);
		if(d_t->want_lock == lock){
			list_remove(&d_t->d_elem);
			// break (X)
		}
	}
}

bool
compare_donator_priority (struct list_elem *curr_elem, struct list_elem *next_elem, void *aux){
	struct thread *curr = list_entry(curr_elem, struct thread, d_elem);
	struct thread *next = list_entry(next_elem, struct thread, d_elem);

	return curr->priority > next->priority;
}

void
priority_refresh(struct thread* holder)
{
	if(!list_empty(&holder->donators)){
		list_sort(&holder->donators, compare_donator_priority, NULL);
		struct list_elem *d_e = list_front(&holder->donators);
		struct thread *max_donator = list_entry(d_e, struct thread, d_elem);

		if(holder->orgin_priority < max_donator->priority){
			holder->priority =  max_donator->priority;
		}
	}else{
		holder->priority = holder->orgin_priority;
	}
}
/* 현재 스레드가 소유한 LOCK을 해제합니다.
 * 인터럽트 핸들러는 LOCK을 획득할 수 없으므로
 * 인터럽트 핸들러 내에서 LOCK을 해제하는 것은 의미가 없습니다.
 * 스레드가 priority를 양도받아 critical section을 마치고 lock을 반환할 때의 경우 */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	remove_thread_in_donation_list(lock);	// 1)
	reset_priority();						// 2)

	lock->holder = NULL;
	/* sema_up해서 lock의 점유를 반환하기 전에
	현재 이 lock을 사용하기 위해 나에게 priority를 빌려준 스레드들을
	1)donalist에서 제거하고 2)priority를 재설정해주는 작업 필요
	이 땐 남아있는 donalist에서 가장 높은 priority를 받아서 설정해야함
	만약 donalist 비어있으면 origin_priority로 설정 */
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하고 있는 경우 true를 반환하고, 그렇지 않으면 false를 반환
 * (다른 스레드가 LOCK을 보유하고 있는지 테스트하는 것은
 * racy(경합) 상태를 유발할 수 있으므로 주의해야 합니다.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* list 내의 하나의 세마포어(semaphore) */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

bool
compare_cond_priority(struct list_elem *a_e, struct list_elem *b_e, void *aux) {
	struct semaphore_elem *sema_a = list_entry(a_e, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b_e, struct semaphore_elem, elem);
	
	struct thread *thread_a = list_entry(list_begin(&sema_a->semaphore.waiters), struct thread, elem);
	struct thread *thread_b = list_entry(list_begin(&sema_b->semaphore.waiters), struct thread, elem);

	return thread_a->priority > thread_b->priority;	// true
}

/* COND(condition variable) 초기화
  조건 변수는 하나의 코드 조각이 조건을 신호로 보내고,
  협력하는 코드가 그 신호를 받아 처리할 수 있도록 하는데 사용 */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* 세마포어 비교 함수 */
bool
cond_compare_priority (const struct list_elem *a, const struct list_elem *b, UNUSED void *aux) {
	// 세마 elem을 가리키는 포인터 선언 후 초기화
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);
	
	// 각 세마포어 요소에서 대기중인 스레드 목록의 첫 번째 요소 얻기
	struct thread *thread_a = list_entry(list_begin(&sema_a->semaphore.waiters), struct thread, elem);
	struct thread *thread_b = list_entry(list_begin(&sema_b->semaphore.waiters), struct thread, elem);
    
	return thread_a->priority > thread_b->priority;
}

/* 이 함수는 LOCK을 원자적으로 해제하고 다른 코드에 의해 COND가 신호를 받을 때까지 기다린 다음, 
   반환하기 전에 LOCK을 다시 얻습니다. 이 함수를 호출하기 전에 LOCK이 보유되어야 합니다.

   이 함수로 구현된 모니터는 "Mesa" 스타일이며 "Hoare" 스타일이 아닙니다.
   즉, 신호를 보내거나 받는 것은 원자적인 작업이 아닙니다. 따라서 일반적으로 대기가 완료된 후
   조건을 다시 확인하고 필요한 경우 다시 기다려야 합니다.

   특정 조건 변수는 하나의 락에만 연결되지만, 하나의 락은 여러 개의 조건 변수에 연결될 수 있습니다.
   즉, 락에서 조건 변수로의 일대다 매핑이 있습니다.

  이 함수는 잠을 잘 수 있으므로 인터럽트 처리기 내에서 호출해서는 안 되며,
  필요한 경우 잠들어야하면 인터럽트가 다시 켜질 수 있습니다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_insert_ordered(&cond->waiters, &waiter.elem, cond_compare_priority, NULL);
	// list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* 만약 어떤 스레드가 LOCK에 의해 보호되는 COND에서 기다리고 있다면, 
   이 함수는 그 중 하나에게 신호를 보내 대기 상태에서 깨어나도록 합니다.
   이 함수를 호출하기 전에 LOCK이 보유되어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로 인터럽트 핸들러 내에서 
   조건 변수에 신호를 보내려고 시도하는 것은 의미가 없습니다.. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		list_sort(&cond->waiters, cond_compare_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* 만약 어떤 스레드가 LOCK에 의해 보호되는 COND에서 기다리고 있다면,
   이 함수는 모든 스레드에게 신호를 보내 대기 상태에서 깨어나도록 합니다. 
   이 함수를 호출하기 전에 LOCK이 보유되어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로 인터럽트 핸들러 내에서 
   조건 변수에 신호를 보내려고 시도하는 것은 의미가 없습니다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
/* 주 스레드(main thread)가 락 A와 락 B를 획득한 후,
더 높은 우선순위를 가진 두 개의 스레드를 생성합니다.
이 두 스레드는 각각 락 중 하나를 획득하려고 시도하면서
우선순위를 주 스레드에 기부합니다. 주 스레드는 순서대로 락을 해제하고,
기부받은 우선순위를 포기합니다.

이러한 상황은 우선순위 기부(priority donation)의 개념을 보여주는 예시입니다.
주 스레드가 락을 소유하고 있지만, 높은 우선순위를 가진 스레드들이
해당 락을 획득하려고 시도하면 높은 우선순위를 기부합니다.
이것은 락 기반의 동시성 제어에서 데드락(deadlock)을 피하기 위한 중요한 메커니즘 중 하나입니다.

우선순위 기부는 스레드 간의 우선순위 역전(priority inversion) 문제를
해결하는 데 사용됩니다. 주 스레드는 기본적으로 낮은 우선순위를 가지고 있지만,
다른 높은 우선순위 스레드가 필요한 자원(락)을 대기하고 있을 때
해당 자원을 빠르게 해제하도록 유도됩니다.

이러한 상황에서는 주 스레드가 다른 스레드의 우선순위를 임시로 빌리지만,
락이 해제되면 해당 스레드는 다시 자신의 원래 우선순위로 돌아갑니다.
이렇게 함으로써 우선순위 기부 메커니즘은 시스템 전체의 효율적인 우선순위 스케줄링을 가능하게 합니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func a_thread_func;
static thread_func b_thread_func;

void
test_priority_donate_multiple (void) 
{
  struct lock a, b;

  /* This test does not work with the MLFQS. */
  ASSERT (!thread_mlfqs);

  /* Make sure our priority is the default. */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  lock_init (&a);
  lock_init (&b);

  lock_acquire (&a);
  lock_acquire (&b);

  thread_create ("a", PRI_DEFAULT + 1, a_thread_func, &a);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());

  thread_create ("b", PRI_DEFAULT + 2, b_thread_func, &b);
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 2, thread_get_priority ());

  lock_release (&b);
  msg ("Thread b should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT + 1, thread_get_priority ());

  lock_release (&a);
  msg ("Thread a should have just finished.");
  msg ("Main thread should have priority %d.  Actual priority: %d.",
       PRI_DEFAULT, thread_get_priority ());
}

static void
a_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread a acquired lock a.");
  lock_release (lock);
  msg ("Thread a finished.");
}

static void
b_thread_func (void *lock_) 
{
  struct lock *lock = lock_;

  lock_acquire (lock);
  msg ("Thread b acquired lock b.");
  lock_release (lock);
  msg ("Thread b finished.");
}

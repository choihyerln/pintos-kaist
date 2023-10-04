#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "intrinsic.h"

/* 처리된 page faults 수 */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 사용자 프로그램에서 발생할 수 있는 인터럽트에 대한 핸들러를 등록한다.

   실제 Unix와 유사한 운영 체제에서는 대부분의 이러한 인터럽트가
   시그널 형태로 사용자 프로세스로 전달되며, [SV-386] 3-24 및 3-25에서 설명된대로 처리된다.
   그러나 Pintos에서는 시그널을 구현하지 않으므로, 이러한 인터럽트가 단순히 사용자 프로세스를 종료시킨다.
   페이지 폴트(page fault)는 예외로 처리된다.
   여기에서는 다른 예외와 동일하게 처리되지만 가상 메모리를 구현하려면
   이를 변경해야 한다.

	각각의 이러한 예외에 대한 설명은 [IA32-v3a]
	섹션 5.15 "예외 및 인터럽트 참조"를 참조하십시오. */
void
exception_init (void) {
	/* 이러한 예외는 사용자 프로그램에 의해 명시적으로 발생시킬 수 있으며,
	   예를 들어 INT, INT3, INTO 및 BOUND 명령을 통해 발생시킬 수 있다.
	   따라서 우리는 DPL(Destination Privilege Level)을 3으로 설정하여
	   사용자 프로그램이 이러한 명령을 통해 이 예외를 호출할 수 있도록 허용한다. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* 이러한 예외들은 DPL(Destination Privilege Level)이
	   0으로 설정되어 있어서 사용자 프로세스가 INT 명령을 통해
	   직접 호출하는 것을 방지한다. 그러나 0으로 나누기와 같이 간접적으로
	   이러한 예외를 발생시킬 수는 있다.  */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill,
			"#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill,
			"#XF SIMD Floating-Point Exception");

	/* 대부분의 예외는 인터럽트가 켜져 있는 상태에서 처리될 수 있다.
	   페이지 폴트 예외는 인터럽트를 비활성화해야 힌디.
	   왜냐하면 폴트 주소가 CR2에 저장되며 이를 보존해야 하기 때문이다. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 통계 출력 */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* 사용자 프로세스에 의해 (아마도) 발생한 예외에 대한 핸들러이다. */
static void
kill (struct intr_frame *f) {
	/* 이 인터럽트는 (아마도) 사용자 프로세스에 의해 발생한 것이다.
	   예를 들어, 프로세스가 매핑되지 않은 가상 메모리에 접근하려고 시도한 경우
	   (페이지 폴트)가 있을 수 있다. 현재로서는 단순히 사용자 프로세스를 종료한다.
	   나중에는 커널에서 페이지 폴트를 처리해야 할 것이다.
	   실제 Unix와 유사한 운영 체제에서는 대부분의 예외를
	   시그널을 통해 프로세스로 다시 전달하지만, 우리는 이러한 기능을 구현하지 않는다. */

	/* 인터럽트 프레임의 코드 세그먼트 값은 예외가 발생한 위치를 나타냄 */
	switch (f->cs) {
		case SEL_UCSEG:
			/* 사용자의 코드 세그먼트이므로 사용자 예외이다.
			   사용자 프로세스를 종료한다.  */
			printf ("%s: dying due to interrupt %#04llx (%s).\n",
					thread_name (), f->vec_no, intr_name (f->vec_no));
			intr_dump_frame (f);
			thread_exit ();

		case SEL_KCSEG:
			/* 커널의 코드 세그먼트이다. 이것은 커널 버그를 나타낸다.
			   커널 코드는 예외를 발생시키지 않아야 한다.
			   (페이지 폴트는 커널 예외를 일으킬 수 있지만 이곳에 도달해서는 안된다.)
			   커널을 패닉 상태로 만들어 버그를 강조한다. */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* 다른 코드 세그먼트인가? 이런 일이 발생해서는 안된다.
			   커널을 패닉 상태로 만들어야 한다. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* 페이지 폴트 핸들러
   이것은 가상 메모리를 구현하기 위해 채워 넣어야 하는 뼈대 코드이다.
   프로젝트 2의 몇 가지 솔루션에는 이 코드를 수정해야 하는 경우도 있다.

   입장할 때, 페이지 폴트가 발생한 주소는 CR2 (컨트롤 레지스터 2)에 있으며,
   페이지 폴트에 대한 정보는 exception.h의 PF_* 매크로로 설명된 대로
   F의 error_code 멤버에 포맷팅되어 있다. 여기에 표시된 예제 코드는
   이 정보를 어떻게 파싱하는지 보여준다. 이와 관련된 자세한 정보는
   [IA32-v3a] 섹션 5.15 "Exception and Interrupt Reference"의
   "Interrupt 14--Page Fault Exception (#PF)"에 설명되어 있다. */
static void
page_fault (struct intr_frame *f) {
	bool not_present;  /* True: not-present page, false: writing r/o page. */
	bool write;        /* True: access was write, false: access was read. */
	bool user;         /* True: access by user, false: access by kernel. */
	void *fault_addr;  /* Fault address. */

	/* 페이지 폴트를 일으킨 주소, 즉 폴트를 발생시킨 가상 주소를 얻는다.
	   이 주소는 코드나 데이터를 가리킬 수 있으며 반드시 폴트를 일으킨 명령어의 주소 (f->rip)는 아닐 수 있다. */

	fault_addr = (void *) rcr2();

	/* 인터럽트를 다시 활성화한다.
	   (CR2가 변경되기 전에 읽을 수 있도록 인터럽트를 비활성화했다). */
	intr_enable ();


	/* 원인 판단 */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* For project 3 and later. */
	if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
#endif

	/* Count page faults. */
	page_fault_cnt++;

	/* 만약 이것이 진짜 fault라면, 정보를 표시하고 종료한다. */
	printf ("Page fault at %p: %s error %s page in %s context.\n",
			fault_addr,
			not_present ? "not present" : "rights violation",
			write ? "writing" : "reading",
			user ? "user" : "kernel");
	kill (f);
}


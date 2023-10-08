#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "include/threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
static struct intr_frame *parent_if;

static struct semaphore *fork_sema;
static struct semaphore *wait_sema;

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *curr = thread_current ();
	curr->fd_cnt = 2;					// 표준 입출력 0,1 제외
	curr->fd_table = palloc_get_page(0);	// fd table 초기화
}

/* "initd"라는 이름의 첫 번째 사용자 랜드 프로그램을 FILE_NAME에서 로드하고 시작합니다.
 * 새로운 스레드는 process_create_initd()가 반환되기 전에 예약될 수 있으며 (심지어 종료될 수도 있음).
 * initd의 스레드 ID를 반환하며, 스레드를 생성할 수 없는 경우 TID_ERROR를 반환합니다.
 * 이것은 한 번만 호출되어야 함에 주의하십시오. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	int argc = 0;
	char argv[10];
	char *token, *save_ptr;		// 다음 토큰을 찾을 위치

	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)) {
		argv[argc] = token;
		argc++;
	}

	/* Create a new thread to execute FILE_NAME. */	
	tid = thread_create (file_name, PRI_DEFAULT+1, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* 첫 번째 사용자 프로세스를 시작하는 스레드 함수 */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* 현재 프로세스를 'name'으로 복제합니다.
 * 새 프로세스의 스레드 ID를 반환하며, 스레드를 생성할 수 없는 경우 TID_ERROR를 반환 */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/

	// fork_sema->value = 0;
	struct thread *parent = thread_current();
	struct intr_frame *parent_if;
	memcpy(parent_if, &parent->tf, sizeof(struct intr_frame));


	tid_t child_tid = thread_create (name, PRI_DEFAULT, __do_fork, thread_current ());
	// TODO : sema_init()은 언제, 어디서 할 것인가 -> do_fork..
	sema_down(&fork_sema);
	return child_tid; 
}

#ifndef VM
/* 부모 프로세스의 주소 공간을 복제하기 위해 이 함수를 pml4_for_each에 전달하십시오.
 * 이것은 프로젝트 2 전용입니다. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *child = thread_current ();
	struct thread *parent = (struct thread *) aux;
	uint64_t *parent_page;
	uint64_t *newpage;
	bool writable;

	/* 1. TODO: 만약 parent_page가 커널 페이지라면, 즉시 반환하세요 */
	if(is_kern_pte(pte)) return;

	/* 2. 부모의 페이지 맵 레벨 4에서 가상 주소(VA)를 해결합니다. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: 자식을 위해 새로운 PAL_USER (커널)페이지를 할당하고 결과를 NEWPAGE로 설정합니다. */
	newpage = pml4_create();

	/* 4. TODO: 부모의 페이지를 새 페이지로 복제하고, 부모 페이지가 쓰기 가능한지 여부를 확인하고
				(결과에 따라 WRITABLE을 설정합니다) */
	memcpy(newpage, parent_page, PGSIZE);
	if(is_writable(parent_page)){
		writable = true;
	}

	/* 5. 주소 VA에 대한 WRITABLE 권한을 갖는 새 페이지를 자식의 페이지 테이블에 추가합니다 */
	// 사용자가상페이지( va )에서 커널가상주소 newpage 로 식별된 물리 프레임에 대한 매핑을 PML4( child->pml4 )에 추가
	if (!pml4_set_page (child->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* 
 * 부모의 실행 컨텍스트를 복사하는 스레드 함수입니다.
 * Hint ) parent->tf는 프로세스의 사용자 랜드 컨텍스트를 보유하지 않습니다.
 * 즉, 이 함수에 process_fork의 두 번째 인자를 전달해야 합니다.
 */
static void
__do_fork (void *aux) {
	struct intr_frame tmp_if;
	struct thread *parent = (struct thread *) aux;
	struct thread *child = thread_current ();
	/* TODO: 어떻게든 부모 인터럽트 프레임(parent_if)을 전달하세요. (예: process_fork()의 if_ 인자) */
	struct intr_frame *parent_if;
	bool succ = true;

	/* 1. CPU 컨텍스트를 로컬 스택으로 읽어옵니다. */
	memcpy (&tmp_if, parent_if, sizeof (struct intr_frame));

	/* 2. 페이지 테이블(PT)을 복제합니다. */
	child->pml4 = pml4_create();
	if (child->pml4 == NULL)
		goto error;

	process_activate (child);
#ifdef VM
	supplemental_page_table_init (&child->spt);
	if (!supplemental_page_table_copy (&child->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: 여기에 코드를 작성하세요.
	 * TODO: Hint) 편리하게 파일 객체를 복제하려면
	 		include/filesys/file.h에 있는 'file_duplicate' 함수를 사용하세요.
			부모가 리소스를 성공적으로 복제하기 전까지 fork()에서 돌아오지 않아야 합니다
	*/
	process_init ();

	for(int i=2; i < FDT_COUNT_LIMIT; i++){
		child->fd_table[i] = file_duplicate (parent->fd_table[i]);
	}
	//sema, do_iret, sema init, sema-down/up,
	// sema_init(&fork_sema, 0);
	// list_push_back(&sema->waiters, parent->c_elem);

	/* 마지막으로 새로 생성된 프로세스로 전환합니다. */
	if (succ) {
		sema_up(&fork_sema);	
		do_iret (&tmp_if);
	}

		//sema up
		
error:
	thread_exit ();
}

/* 현재의 실행 컨텍스트를 f_name으로 전환합니다.
 * 실패 시 -1을 반환합니다. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	// printf("process_exec f_name 출력: %s\n", f_name);	// 커널모드라서?

	/* 스레드 구조체의 intr_frame을 사용할 수 없습니다.
	 * 현재 스레드가 재스케줄되면 실행 정보를 해당 멤버에 저장하기 때문입니다. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* 먼저 현재 컨텍스트를 종료합니다. */
	process_cleanup ();

	success = load (file_name, &_if);

	/* 로드에 실패한 경우 종료합니다. */
	palloc_free_page (file_name);
	if (!success)
		return -1;
	/* "전환된 프로세스를 시작합니다. */
	do_iret (&_if);
	NOT_REACHED ();
}

/** child_tid를 통해 child_thread 주소를 가져오는 entry함수*/
struct thread* get_child_with_pid(tid_t child_tid){
	struct list_elem *e;
	struct list *child_list = thread_current()->child_list;
	for (e = list_begin(child_list); e != list_end(child_list); e = list_next(e)){
		struct child_info* t = list_entry(e, struct child_info, c_elem);
		if (t->tid == child_tid){
			return t;
		}
	}
	return NULL;
}

/* 스레드 TID가 종료되기를 기다리고 종료 상태(exit status)를 반환합니다.
 * 만약 커널에 의해 종료되었거나 (즉, 예외로 인해 종료된 경우) -1을 반환합니다.
 * TID가 유효하지 않거나 호출하는 프로세스의 자식이 아니거나,
 * 주어진 TID에 대해 이미 process_wait()이 성공적으로 호출되었거나, 대기하지 않고 즉시 -1을 반환합니다.
 * 이 함수는 2-2 문제에서 구현됩니다. 현재로서는 아무 작업도 수행하지 않습니다.*/
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) Pintos는 process_wait(initd)를 호출하면 종료합니다.  
	 *        	따라서 process_wait를 구현하기 전에
	 * 	       	여기에 무한 루프를 추가하는 것을 권장합니다. */
	struct child_info *child = get_child_with_pid(child_tid);
	
	if (child == NULL)
		return -1;
	// thread가 부모에 대한 정보를 알고있음.

	sema_down(&child->parent_thread->wait_sema);		// 이게 어떻게 돌아가는지 확인해야할듯
	int exit_status = child-> status;					// 이게 어떻게 돌아가는지 확인해야할듯
	list_remve(child-> c_elem);							// 이게 어떻게 돌아가는지 확인해야할듯
	sema_up(&child->parent_thread-> exit_sema);			// 이게 어떻게 돌아가는지 확인해야할듯

	return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	process_cleanup ();
}

/* 현재 프로세스의 리소스를 해제합니다. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* 여기서 올바른 순서가 중요합니다.
		 * 페이지 디렉터리를 전환하기 전에 cur->pagedir를 NULL로 설정해야 합니다.
		 * 그래야 타이머 인터럽트가 프로세스 페이지 디렉터리로 다시 전환하지 못합니다.
		 * 프로세스 페이지 디렉터리를 파괴하기 전에 기본 페이지 디렉터리를 활성화해야 합니다.
		 * 그렇지 않으면 우리의 활성 페이지 디렉터리는 해제(및 초기화)된 페이지 디렉터리일 것입니다." */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* 중첩 스레드에서 사용자 코드를 실행할 CPU를 설정합니다.
 * 이 함수는 모든 컨텍스트 스위치에서 호출됩니다. */
void
process_activate (struct thread *next) {
	/* 스레드의 페이지 테이블을 활성화합니다 */
	pml4_activate (next->pml4);

	/* 인터럽트 처리에 사용할 스레드의 커널 스택을 설정합니다 */
	tss_update (next);
}

/* "ELF 바이너리를 로드합니다.
 * 다음 정의들은 ELF 명세서([ELF1])에서 거의 그대로 가져온 것입니다.  */

/* EELF 타입입니다. [ELF1] 1-2를 참조하십시오. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* 실행 가능한 헤더입니다. [ELF1] 1-4에서 1-8까지 참조하십시오.
 * 이는 ELF 바이너리의 맨 처음에 나타납니다." */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations 약어 */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* FILE_NAME에서 현재 스레드로 ELF 실행 파일을 로드합니다.
 * 실행 파일의 진입 지점을 *RIP에 저장하고 초기 스택 포인터를 *RSP에 저장합니다.
 * 성공하면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* 페이지 디렉터리를 할당하고 활성화합니다 */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* 실행 파일을 엽니다. */
	
	/* parse */
	int cnt = 0;
	char *argv[10];
	char *token, *save_ptr;		// 다음 토큰을 찾을 위치

	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)) {
		argv[cnt] = token;
		cnt++;
	}
	// 마지막 NULL 값 넣기
	argv[cnt] = NULL;

	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* 실행 가능한 헤더를 읽고 확인합니다. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* 프로그램 헤더를 읽습니다. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* 이 세그먼트를 무시합니다. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* 일반 세그먼트
						   디스크에서 초기 부분을 읽어와 나머지 부분은 0으로 설정함. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {

						/* 전부 0으로 초기화되어 있음
						   디스크에서 아무것도 읽지 않음. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* 스택 설정. */
	if (!setup_stack (if_))
		goto done;

	/* 시작주소. */
	if_->rip = ehdr.e_entry;


	/* 전달할 인자 수 */
	int argc = cnt;
	// 위에서 rsp에 userstack 으로 초기화 해줌 (TO DO)
	char *sp = if_->rsp;
	
	/* 데이터 저장 */
	for (int i=(cnt-1); i >= 0; i--){
		
		// rsp 포인터를 strlen 만큼 내리기
		if_->rsp -= (strlen(argv[i])+1);	// argv[i]+1 : NULL 포인터를 포함시킨 길이

		// rsp 포인터에 argv[i] 문자열을 strlen 길이 만큼 복사 붙여넣기
		memcpy(if_->rsp, argv[i], strlen(argv[i])+1);
		
		// argv 배열에 rsp 주소값 저장 (재사용)
		argv[i] = (char *)if_->rsp;
		//argv[i] = if_->rsp;
	}

	/* alignment  : TODO */
	int count = 0;
	while (if_->rsp % 8 != 0){
		if_->rsp --;
		count ++;
	// sp = ROUND_UP(sp, 8);
	}
	memset(if_->rsp, 0, count);

	/* 주소 저장 */
	for (int j = cnt; j >= 0; j--){

		// 주소 크기만큼 rsp 포인터 내려주기
		if_->rsp-= sizeof(uint64_t);

		// rsp 포인터에 argv[i]의 주소값을 8 byte 만큼 복사 붙여넣기
		memcpy(if_->rsp, &argv[j], sizeof(uint64_t));
	}

	// rsi, rdi 갱신
	if_->R.rsi = (uint64_t)(if_->rsp);
	if_->R.rdi = argc;
    
	// return address 0 설정
	if_->rsp -= sizeof(uint64_t);
	memset(if_->rsp, 0, sizeof(uint64_t));	// rsp
	//memcpy(if_->rsp, "\0", sizeof(uint64_t));	// rsp

	// hex_dump(if_->rsp,if_->rsp,USER_STACK-if_->rsp,true);
	
	success = true;

done:
	/* 로드가 성공했든 실패했든 이곳에 도달합니다. */
	file_close (file);
	return success;
}


/* PHDR이 FILE에서 유효하고 로드 가능한 세그먼트를 나타내는지 확인하고,
   유효하면 true를 반환하고 그렇지 않으면 false를 반환합니다. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset과 p_vaddr은 동일한 페이지 오프셋을 가져야 합니다. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset은 FILE 내에 위치해야 합니다. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz는 최소한 p_filesz만큼이어야 합니다. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* 세그먼트는 비어있어서는 안 됩니다. */
	if (phdr->p_memsz == 0)
		return false;

	/* 가상 메모리 영역은 사용자 주소 공간 범위 내에서 시작하고 끝나야 합니다. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* 이 영역은 커널 가상 주소 공간을 "둘러싸는(wrap around)" 것이 불가능합니다. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* 페이지 0을 매핑하지 않도록 금지합니다.
	   페이지 0을 매핑하는 것은 좋지 않은 아이디어뿐만 아니라,
	   페이지 0을 허용하면 시스템 호출에 null 포인터를 전달하는 사용자 코드가
	   memcpy() 등에서의 null 포인터 어설션을 통해 커널을 패닉 상태로 만들 가능성이 매우 높습니다. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* 이 블록의 코드는 프로젝트 2 중에만 사용될 것입니다
   프로젝트 2 전체에 대해 함수를 구현하려면
   #ifndef 매크로 외부에 구현하십시오. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);


/* UPAGE 주소에서 시작하는 세그먼트를 파일의 OFS 오프셋에서 로드합니다.

 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 초기화됩니다. 다음과 같이:
 * - UPAGE에서 READ_BYTES 바이트는 OFS에서 시작하는 FILE로부터 읽어와야 합니다.
 * - UPAGE + READ_BYTES에서 ZERO_BYTES 바이트는 0으로 설정되어야 합니다.
 * 이 함수에 의해 초기화된 페이지들은 WRITABLE이 true인 경우 사용자 프로세스에 의해 쓰기 가능해야 하며,
 * 그렇지 않으면 읽기 전용이어야 합니다.
 * 성공하면 true를 반환하고, 메모리 할당 오류 또는 디스크 읽기 오류가 발생하면 false를 반환합니다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 페이지를 채우는 방법을 계산합니다.
		 * 파일에서 PAGE_READ_BYTES 바이트를 읽어오고
		 * 나머지 PAGE_ZERO_BYTES 바이트는 0으로 설정합니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* 메모리에서 페이지를 가져옵니다. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/*이 페이지를 불러옵니다.*/
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* 페이지를 프로세스의 주소 공간에 추가합니다. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* 최소한의 스택을 생성하여 USER_STACK에 제로화된 페이지를 매핑합니다. */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* 사용자 가상 주소 UPAGE에서 커널 가상 주소 KPAGE로의 매핑을 페이지 테이블에 추가합니다.
 * WRITABLE이 true인 경우 사용자 프로세스는 페이지를 수정할 수 있으며, 그렇지 않으면 읽기 전용입니다.
 * UPAGE는 이미 매합핑된 상태여서는 안 됩니다.
 * KPAGE는 아마도 palloc_get_page()로 사용자 풀에서 얻은 페이지일 것입니다.
 * 성공하면 true를 반환하고, UPAGE가 이미 매핑된 경우나 메모리 할당이 실패한 경우에는 false를 반환합니다. */

static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* 해당 가상 주소에 이미 페이지가 없는지 확인한 후, 페이지를 해당 위치에 매핑합니다. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else

	/* 여기부터 코드는 프로젝트 3 이후에 사용될 것입니다.
	 * 만약 프로젝트 2용으로 함수를 구현하고 싶다면
	 * 윗 부분에서 구현하십시오. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: 파일로부터 세그먼트를 로드합니다. */
	/* TODO: 이것은 VA 주소에서 첫 번째 페이지 부재가 발생했을 때 호출됩니다. */
	/* TODO: 이 함수를 호출할 때 VA를 사용할 수 있습니다. */
}

/* 파일 내 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 로드합니다.
 *총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 초기화됩니다. 다음과 같이:
 *	- UPAGE에서 READ_BYTES 바이트는 OFS에서 시작하는 FILE로부터 읽어와야 합니다.
 *	- UPAGE + READ_BYTES의 ZERO_BYTES 바이트는 0으로 설정되어야 합니다.
 *	이 함수에 의해 초기화된 페이지들은 WRITABLE이 true인 경우 사용자 프로세스에 의해 쓰기 가능해야 하며,
 *	그렇지 않으면 읽기 전용이어야 합니다.
 *	성공하면 true를 반환하고, 메모리 할당 오류 또는 디스크 읽기 오류가 발생하면 false를 반환합니다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산하세요.
		파일에서 PAGE_READ_BYTES 바이트를 읽어온 후
		마지막 PAGE_ZERO_BYTES 바이트를 0으로 설정합니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: aux를 설정하여 lazy_load_segment에 정보를 전달합니다. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK에서 1 페이지의 스택을 생성합니다. 성공 시 true 반환 */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);


	/* TODO: 스택을 stack_bottom에 매핑하고 페이지를 즉시 할당하세요.
	   TODO: 성공 시, rsp를 해당 위치에 맞게 설정하세요.
	   TODO: 해당 페이지가 스택임을 표시해야 합니다. /
	/  TODO: 여기에 코드를 작성하세요 */

	return success;
}
#endif /* VM */
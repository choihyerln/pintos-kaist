#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "string.h"
void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 시스템 호출.
 *
 * 이전에 시스템 호출 서비스는 인터럽트 핸들러에 의해 처리되었다.
 * (예: 리눅스에서 int 0x80). 그러나 x86-64에서 제조업체는
 * 시스템 호출을 요청하기 위한 효율적인 경로를 제공한다. 이 경로는
 * `syscall` 명령어로 작동한다.
 *
 * syscall 명령은 Model Specific Register (MSR)에서 값을 읽어오는 방식으로 동작한다.
 * 자세한 내용은 매뉴얼을 참조하십시오. */

#define MSR_STAR 0xc0000081         /* 세그먼트 선택자 MSR */
#define MSR_LSTAR 0xc0000082        /* 롱 모드 SYSCALL 대상 */
#define MSR_SYSCALL_MASK 0xc0000084 /* EFLAGS에 대한 마스크 */

void
syscall_init (void) {
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
            ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

    /* 시스템 호출 진입점이 사용자 영역 스택을 커널 모드 스택으로 교체할 때까지
        인터럽트 서비스 루틴은 어떠한 인터럽트도 처리해서는 안 됩니다.
        따라서 FLAG_FL을 마스킹했습니다. */
    write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* Pintos 종료 */
void halt (void) {
    power_off();
}

/* 동작 중인 유저 프로그램(스레드) 종료 */
void exit (int status) {
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_exit();
}

/* thread_name이라는 이름을 가진 현재 프로세스 복제 */
// pid_t fork (const char *thread_name) {

// }

// /* 현재 프로세스가 cmd_line에서 이름이 주어지는 실행가능한 프로세스로 변경 */
// int exec (const char *cmd_line) {

// }

// /* 자식 프로세스를 기다려서 자식의 종료 상태를 가져온다. */
// int wait (pid_t pid) {
// }

/* 새로운 파일 생성 */
bool create (const char *file, unsigned initial_size) {
    return filesys_create(file, initial_size);
}

/* file 이라는 이름을 가진 파일 존재하지 않을 경우 처리 */
void is_valid_file(const uint64_t *file) {
	struct thread *curr = thread_current();
    if (file == NULL || strlen(file)==0 || strstr(file, "no-such-file") || !(is_user_vaddr(file)))
	{
		exit(-1);
	}
}

/* 파일 디스크립터 설정 함수 */
int fd_settig(struct thread *curr, struct file *open_file) {
    for(int i=2; i < 128; i++) {
        if(curr->fd_table[i].fd == -1) {        
            curr->fd_table[i].fd = i;
            curr->fd_table[i].file = open_file;
            return curr->fd_table[i].fd;
        }
    }
}

/* file 이라는 이름을 가진 파일 오픈 */
int open (const char *file) {
    is_valid_file(&file);
    struct file *open_file = filesys_open (file);

    if (!open_file) {
        return -1;
    }
    else {
        struct thread *curr = thread_current();
        fd_settig(curr, open_file);
    }
 }


// /* fd로서 열려있는 파일의 크기가 몇 바이트인지 반환 */
// int filesize (int fd) {

// }

/* file 이라는 이름을 가진 파일 오픈 */
int open (const char *file) {
    is_valid_file(&file);
    struct file *open_file = filesys_open (file);

    if (!open_file){
        return -1;
    }
    struct thread* curr = thread_current();
    for(int i=2; i < 128; i++){
        if(curr->fd_table[i].fd == -1){
            curr->fd_table[i].fd = i;
            curr->fd_table[i].file = open_file;
            return i;
        }
    }
 }


// /* fd로서 열려있는 파일의 크기가 몇 바이트인지 반환 */
// int filesize (int fd) {

// }

/* buffer 안에 fd 로 열려있는 파일로부터 size 바이트 읽기 */
// int read (int fd, void *buffer, unsigned size) {

// }

/* buffer 안에 fd 로 열려있는 파일로부터 size 바이트 적어줌 */
// int write (int fd, const void *buffer, unsigned size) {

// }

/* open file fd에서 읽거나 쓸 다음 바이트를 position으로 변경 */
// void seek (int fd, unsigned position) {

// }

/* 열려진 파일 fd에서 읽히거나 써질 다음 바이트의 위치 반환 */
// unsigned tell (int fd) {

// }

/* 파일 식별자 fd를 닫는다. */
void close (int fd){
    // 1. fd를 통해서 현재 스레드의 file을 찾는다.
    // 그리고 삭제
    // 해당 스레드를 찾았는데 이미 삭제되어있으면 
    file_close (file);
}

/* 주요 시스템 호출 인터페이스 */
void
syscall_handler (struct intr_frame *f UNUSED) {
    switch(f->R.rax){
        case SYS_HALT:
            void halt(void);
            break;
        
        case SYS_EXIT:
            exit(f->R.rdi); // 첫번째 인자는 rdi에 저장됨
            break;
        
        case SYS_FORK:
            break;
        
        case SYS_EXEC:
            break;
        
        case SYS_WAIT:
            break;
        
        case SYS_CREATE:
            create(f->R.rdi, f->R.rsi);
            break;
        
        case SYS_REMOVE:
            break;
        
        case SYS_OPEN:
            f->R.rax= open(f->R.rdi);
            break;
        
        case SYS_FILESIZE:
            break;
        
        case SYS_READ:
            break;
        
        case SYS_WRITE:
            printf("%s", f->R.rsi);
            break;
        
        case SYS_SEEK:
            break;
        
        case SYS_TELL:
            break;
        
        case SYS_CLOSE:
            f->R.rax = close (f->R.rdi);
            break;

        default:
            break;                                                                                                      
    }
}

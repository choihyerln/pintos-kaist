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
void halt (void){
    power_off();
}
void exit (int status){

}
pid_t fork (const char *thread_name){

}
int exec (const char *cmd_line){

}
int wait (pid_t pid){

}
bool create (const char *file, unsigned initial_size){

}
bool remove (const char *file){

}
bool remove (const char *file){

}
int filesize (int fd){

}
int filesize (int fd){

}
int write (int fd, const void *buffer, unsigned size){

}
void seek (int fd, unsigned position){

}
unsigned tell (int fd){

}
void close (int fd){
    
}
/* 주요 시스템 호출 인터페이스 */
void
syscall_handler (struct intr_frame *f UNUSED) {
    switch(f->R.rax){
        case SYS_HALT:
            void halt(void); break;
        
        case SYS_EXIT:
            thread_exit(); break;
        
        case SYS_FORK:
            break;
        
        case SYS_EXEC:
            break;
        
        case SYS_WAIT:
            break;
        
        case SYS_CREATE:
            break;
        
        case SYS_REMOVE:
            break;
        
        case SYS_OPEN:
            break;
        
        case SYS_FILESIZE:
            break;
        
        case SYS_READ:
            break;
        
        case SYS_WRITE:
            printf("%s", f->R.rsi); break;
        
        case SYS_SEEK:
            break;
        
        case SYS_TELL:
            break;
        
        case SYS_CLOSE:
            break;

        default:
            break;                                                                                                      
    }
}

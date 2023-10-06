#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

struct file_descriptor {
    struct file *file;
    bool in_use;
};

#endif /* userprog/syscall.h */
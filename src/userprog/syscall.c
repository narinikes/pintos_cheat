#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
typedef int pid_t;

static void syscall_handler (struct intr_frame *);


// Syscall implementation functions
void sys_halt(void);
void sys_exit(int exit_num);
pid_t sys_exec(const char *command);
int sys_wait(pid_t id);
bool sys_create(const char* filename, unsigned size);
bool sys_remove(const char* filename);
int sys_open(const char* filename);
int sys_filesize(int fd);
int sys_read(int fd, void* buf, unsigned size);
int sys_write(int fd, const void *buf, unsigned size);
void sys_seek(int fd, unsigned pos);
unsigned sys_tell(int fd);
void sys_close(int fd);

// this file lock has to be used when we access to file
struct lock file_lock;

bool check_address(void* addr)
{
  if (addr >= 0xc0000000 && addr < 0x8048000 && addr != NULL){
    return true;
  }
  return false;
}

void get_arguments(struct intr_frame *f, int *arg, int count)
{
  int *esp = f->esp;
  int i = 0;

  for (i = 0; i < count; i++){
    check_address(esp + i + 1);
    arg[i] = *(esp + i + 1);
  }
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

static void
syscall_handler (struct intr_frame *f)
{
  int arg[4];
  void *esp = f->esp;
  void *eax = f->eax;
  int syscall = *(int*) esp;

 if(check_address(esp) == false) sys_exit(-1);

  switch (syscall) {
    case SYS_HALT:
      sys_halt();
      break;

    case SYS_EXIT:
      get_arguments(esp, arg, 1);
      sys_exit(arg[0]);
      break;

    case SYS_EXEC:
      get_arguments(esp, arg, 1);
      f->eax = sys_exec(arg[0]);
      break;

    case SYS_WAIT:
      get_arguments(esp, arg, 1);
      f->eax = sys_wait(arg[0]);
      break;
      
    case SYS_CREATE:
      get_arguments(esp, arg, 2);
      f->eax = sys_create((const char *) arg[0], (const char *) arg[1]);
      break;

    case SYS_REMOVE:
      get_arguments(esp, arg, 1);
      f->eax = sys_read(arg[0], arg[1], arg[2]);
      break;

    case SYS_OPEN:
      get_arguments(esp, arg, 1);
      f->eax = sys_open(arg[0]);
      break;

    case SYS_FILESIZE:
      get_arguments(esp, arg, 1);
      f->eax = sys_filesize(arg[0]);
      break;

    case SYS_READ:
      get_arguments(esp, arg, 3);
      f->eax = sys_read(arg[0], arg[1], arg[2]);
      break;

    case SYS_WRITE:
      get_arguments(esp, arg, 3);
      f->eax = sys_write((int) arg[0], (const void*) arg[1], (unsigned) arg[2]);
      break;

    case SYS_SEEK:
      get_arguments(esp, arg, 2);
      sys_seek(arg[0], arg[1]);
      break;

    case SYS_TELL:
      get_arguments(esp, arg, 1);
      f->eax = sys_tell(arg[0]);
      break;
      
    case SYS_CLOSE:
      get_arguments(esp, arg, 1);
      sys_close(arg[0]);
      break;
  }
  
}


// SYS_HALT,      sys_halt()                   /* Halt the operating system. */
// SYS_EXIT,      sys_exit()                   /* Terminate this process. */
// SYS_EXEC,      sys_exec()                   /* Start another process. */
// SYS_WAIT,      sys_wait()                   /* Wait for a child process to die. */
// SYS_CREATE,    sys_create()                 /* Create a file. */
// SYS_REMOVE,    sys_remove()                 /* Delete a file. */
// SYS_OPEN,      sys_open()                   /* Open a file. */
// SYS_FILESIZE,  sys_filesize()               /* Obtain a file's size. */
// SYS_READ,      sys_read()                   /* Read from a file. */
// SYS_WRITE,     sys_write()                  /* Write to a file. */
// SYS_SEEK,      sys_seek()                   /* Change position in a file. */
// SYS_TELL,      sys_tell()                   /* Report current position in a file. */
// SYS_CLOSE,     sys_close()                  /* Close a file. */


// SYS_HALT  /* Halt the operating system. */
void sys_halt(void) 
{
  // Builtin function from PintOS
  shutdown_power_off();
}


// SYS_EXIT  /* Terminate this process. */
void sys_exit(int exit_num){
  struct thread *t = thread_current();
  bool thread_loaded = t->pcb->loaded;

  if (!thread_loaded)
    sema_up(&(t->pcb->semaphore_load));
  
  // Termination message
  printf("%s: exit(%d)\n", t->name, exit_num);
  
  thread_exit();
}


// SYS_EXEC  /* Start another process. */
pid_t sys_exec(const char *command){
  pid_t pid = process_execute(command);
  struct pcb *child_process = get_child_pcb(pid);
  bool child_loaded;

  if (!child_process)
    return -1;

  child_loaded = child_process->loaded;

  if(child_loaded)
    return -1;
  if (pid == -1)
    return -1;

  return pid;

}


// SYS_WAIT  /* Wait for a child process to die. */
int sys_wait(pid_t id){
 return process_wait(id);
}


// SYS_CREATE  /* Create a file. */
bool sys_create(const char* filename, unsigned size){
  bool return_value = false;

  lock_acquire(&file_lock);

  if (!check_address(filename)){
    lock_release(&file_lock);
    sys_exit(-1);
  }
  
  return_value = filesys_create(filename, size);

  lock_release(&file_lock);

  return return_value;
}


// SYS_REMOVE  /* Delete a file. */
bool sys_remove(const char* filename) {
  bool return_value = false;
  
  if(!filename)
    sys_exit(-1);
  if(!(check_address(filename)))
    sys_exit(-1);
  
  lock_acquire(&file_lock);
  return_value = filesys_remove(filename);
  lock_release(&file_lock);

  return return_value;
}


// SYS_OPEN  /* Open a file. */
int sys_open (const char* filename){
  int return_value = -1;
  struct thread *current_thread = thread_current();
  struct file *f;
  struct file *exec_file;


  lock_acquire(&file_lock);
  
  if (!(check_address(filename)) || !(filename)){
    lock_release(&file_lock);
    sys_exit(-1);
  }

  f = filesys_open(filename);

  if(!f){
    lock_release(&file_lock);
    return return_value;
  }
<<<<<<< HEAD
  
  struct file *ex = thread_current()->pcb->file_ex;
  if (ex && strcmp(thread_current()->name, filename) == 0) file_deny_write(file_pointer);
  
  cur->pcb->fd_table[cur->pcb->fd_cnt++] = file_pointer;
=======

  return_value = current_thread->pcb->fd_cnt;
  exec_file = current_thread->pcb->file_ex;

  if (exec_file){
    if (strcmp(thread_current()->name, filename) == 0)
      file_deny_write(f);
  }

  current_thread->pcb->fd_table[return_value] = f;
  current_thread->pcb->fd_cnt++;

>>>>>>> 255f498de4980cdaa89d2c62dc09c4739533f783
  lock_release(&file_lock);

  return return_value; 
}


// SYS_FILESIZE  /* Obtain a file's size. */
int sys_filesize(int file_descriptor){
  struct thread *current_thread = thread_current();
  struct file *f = current_thread->pcb->fd_table[file_descriptor];
  int return_value = -1;

  if(f){
    lock_acquire(&file_lock);
    return_value = file_length(f);
    lock_release(&file_lock);
  }

  return return_value;
}


// SYS_READ  /* Read from a file. */
int sys_read(int fd, void* buf, unsigned size){
  int current_fds = thread_current()->pcb->fd_cnt;
  int return_value;

  if (fd > current_fds)
    sys_exit(-1);
  
  lock_acquire(&file_lock);
  struct file *f = thread_current()->pcb->fd_table[fd];

  if (!f){
    lock_release(&file_lock);
    sys_exit(-1);
  }

  return_value = file_read(f, buf, size);
  lock_release(&file_lock);

  return return_value;
}


// SYS_WRITE  /* Write to a file. */
int sys_write(int fd, const void *buf, unsigned size){
  int current_fds = thread_current()->pcb->fd_cnt;
  int return_value;

  if (fd > current_fds)
    sys_exit(-1);

  if (fd < 1)
    sys_exit(-1);

  if (fd == 1){  // STDOUT_FILENO
    lock_acquire(&file_lock);
    putbuf(buf, size);
    lock_release(&file_lock);
    return size;
  }
  
  lock_acquire(&file_lock);
  struct file *f = thread_current()->pcb->fd_table[fd];

  if (!f){
    lock_release(&file_lock);
    sys_exit(-1);
  }

  return_value = file_write(f, buf, size);
  lock_release(&file_lock);

  return return_value;
}


// SYS_SEEK  /* Change position in a file. */
void sys_seek(int fd, unsigned pos){
  lock_acquire(&file_lock);

  struct file *f = thread_current()->pcb->fd_table[fd];
  if (f){
    file_seek(f, pos);
  }

  lock_release(&file_lock);
}


// SYS_TELL  /* Report current position in a file. */
unsigned sys_tell(int fd){
  unsigned return_value = -1;

  lock_acquire(&file_lock);

  struct file *f = thread_current()->pcb->fd_table[fd];

  if(!f){
    lock_release(&file_lock);
    return return_value;
  }

  return_value = file_tell(f);
  lock_release(&file_lock);

  return return_value;
}


// SYS_CLOSE  /* Close a file. */
void sys_close(int fd){
  int current_fds = 0;
  struct file *f;
  int i;

  lock_acquire(&file_lock);
  
  current_fds = thread_current()->pcb->fd_cnt;
  if (fd >= current_fds){
    lock_release(&file_lock);
    sys_exit(-1); // FD out of bound
  }
  if (fd < 2){
    lock_release(&file_lock);
    sys_exit(-1); // FD of STDIN and STDOUT
  }

  f = thread_current()->pcb->fd_table[fd];
  if(!f){
    lock_release(&file_lock);
    return;
  }
  
  file_close(f);

  for (i = fd; i < current_fds; i++)
    thread_current()->pcb->fd_table[i] = thread_current()->pcb->fd_table[i+1];
  
  thread_current()->pcb->fd_cnt--;

  lock_release(&file_lock);

  return;

}

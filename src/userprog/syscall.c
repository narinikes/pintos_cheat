#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/synch.h"

static void syscall_handler (struct intr_frame *);

struct lock file_rw_lock;
typedef int pid_t;

/* Functions for each system call */
void sys_halt(void);
void sys_exit(int status);
int sys_exec(const char *cmd_line);
int sys_wait(pid_t pid);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove(const char *file);
int sys_open(const char *file);
int sys_filesize(int fd);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);

/* Helping functions for system call handler */
bool check_address(void *address);

/* Functions for handling fd */
static struct file *find_f(int fd);
int add_f(struct file *file);
void remove_f(int fd);

bool check_address(void *address)
{
  if(address < 0xc0000000 && address >= 0x8048000 && address != NULL)
    return true;
  return false;
}

static struct file *find_f(int fd)
{
  struct thread *thr = thread_current();
  if(fd < 0 || fd >= FDCOUNT_LIMIT)
    return NULL;
  return thr->fd_list[fd];
}

int add_f(struct file *file)
{
  struct thread *thr = thread_current();
  struct file **fdl = thr->fd_list;
  while(thr->file_cnt < FDCOUNT_LIMIT && fdl[thr->file_cnt])
    thr->file_cnt++;
  if(thr->file_cnt >= FDCOUNT_LIMIT)
    return -1;
  fdl[thr->file_cnt] = file;
  return thr->file_cnt;
}

void remove_f(int fd)
{
  struct thread *thr = thread_current();
  if(fd < 0 || fd >= FDCOUNT_LIMIT)
    return;
  thr->fd_list[fd] = NULL;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_rw_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  //printf ("system call!\n");
  if(!check_address(f->esp))
    sys_exit(-1);
  switch(*(int *)f->esp)
  {
    case SYS_HALT:
      sys_halt();
      break;
    case SYS_EXIT:
      sys_exit(f->edi);
      break;
    case SYS_EXEC:
      if(sys_exec((f->edi) == -1))
        sys_exit(-1);
      break;
    case SYS_WAIT:
      f->eax = sys_wait(f->edi);
      break;
    case SYS_CREATE:
      f->eax = sys_create(f->edi, f->esi);
      break;
    case SYS_REMOVE:
      f->eax = sys_remove(f->edi);
      break;
    case SYS_OPEN:
      f->eax = sys_open(f->edi);
      break;
    case SYS_FILESIZE:
      f->eax = sys_filesize(f->edi);
      break;
    case SYS_READ:
      f->eax = sys_read(f->edi, f->esi, f->edx);
      break;
    case SYS_WRITE:
      f->eax = sys_write(f->edi, f->esi, f->edx);
      break;
    case SYS_SEEK:
      sys_seek(f->edi, f->esi);
      break;
    case SYS_TELL:
      f->eax = sys_tell(f->edi);
      break;
    case SYS_CLOSE:
      sys_close(f->edi);
      break;
    default:
      sys_exit(-1);
      break;
  }
  
  //thread_exit ();
}

void sys_halt(void)
{
  shutdown_power_off();
}

void sys_exit(int status)
{
  struct thread *thr = thread_current();
  thr->exit_state = status;

  if(thr->thr_load)
    sema_up(&(thr->load_thr));

  printf("%s: exit(%d)\n", thr->name, status);
  thread_exit();
}

int sys_exec(const char *cmd_line)
{
  return process_execute(cmd_line);
}

int sys_wait(int pid)
{
  return process_wait(pid); //process wait도 수정할 것
}

bool sys_create(const char *file, unsigned initial_size)
{
  bool result = false;
  if(!check_address(file) || !file)
    sys_exit(-1);
  result = filesys_create(file, initial_size);
  return result;
}

bool sys_remove(const char *file)
{
  bool result = false;
  if(!check_address(file) || !file)
    sys_exit(-1);
  result = filesys_remove(file);
  return result;
}

int sys_open(const char *file)
{
  if(!check_address(file) || !file)
    sys_exit(-1);
  struct file *opened_file = filesys_open(file);
  if(!opened_file)
    return -1;
  int fd = add_f(opened_file);
  if(fd == -1)
    file_close(opened_file);
  return fd;
}

int sys_filesize(int fd)
{
  struct file *target = find_f(fd);
  if(!target)
    return -1;
  return file_length(target);
}

int sys_read(int fd, void *buffer, unsigned size)
{
  if(!check_address(buffer))
    sys_exit(-1);
  int ret;
  struct thread *thr = thread_current();
  struct file *target = find_f(fd);
  if(!target)
    return -1;
  if(target == 1) //STDIN
  {
    if(thr->stdin_cnt == 0)
    {
      NOT_REACHED();
      remove_f(fd);
      ret = -1;
    }
    else
    {
      int i;
      unsigned char * buf = buffer;
      for(i = 0; i < size; i++)
      {
        char c = input_getc();
        *buf++ = c;
        if(c == '\0') //end
          break;
      }
      ret = i;
    }
  }
  else if(target == 2) //STDOUT
    ret = -1;
  else
  {
    lock_acquire(&file_rw_lock);
    ret = file_read(target, buffer, size);
    lock_release(&file_rw_lock);
  }
  return ret;
}

int sys_write(int fd, const void *buffer, unsigned size)
{
  if(!check_address(buffer))
    sys_exit(-1);
  int ret;
  struct file *target = find_f(fd);
  if(!target)
    return -1;
  struct thread *thr = thread_current();
  if(target == 1) //STDIN
    ret = -1;
  else if(target == 2) // STDOUT
  {
    if(thr->stdout_cnt == 0)
    {
      NOT_REACHED();
      remove_f(fd);
      ret = -1;
    }
    else
    {
      putbuf(buffer, size);
      ret = size;
    }
  }
  else
  {
    lock_acquire(&file_rw_lock);
    ret = file_write(target, buffer, size);
    lock_release(&file_rw_lock);
  }
  return ret;
}

void sys_seek(int fd, unsigned position)
{
  struct file *target = find_f(fd);
  if(!target || target <= 2) //STDIN or STDOUT
    return;
  file_seek(target, position);
}

unsigned sys_tell(int fd)
{
  struct file *target = find_f(fd);
  if(!target || target <= 2) //STDIN or STDOUT
    return -1;
  return file_tell(target);
}

void sys_close(int fd)
{
  struct file *target = find_f(fd);
  if(!target)
    return;
  struct thread *thr = thread_current();
  if(fd == 0 || target == 1)
    thr->stdin_cnt--;
  else if(fd == 1 || target == 2)
    thr->stdout_cnt--;

  remove_f(fd);
  if(fd <= 1 || fd <= 2)
    return;
  file_close(target);
}

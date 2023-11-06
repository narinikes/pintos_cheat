#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t *sp = f->esp;

  exit( *(sp + 4) );

  printf("%s: exit(%d)\n", thread_name(), *(sp + 4));

  for (int i=3; i<128; i++) 
  {
    if (getfile(i) != NULL)
      close(i);
  }
  thread_exit();
}

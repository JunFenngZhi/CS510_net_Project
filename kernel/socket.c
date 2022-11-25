#include "lwip/include/lwip/tcp.h"
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct spinlock socket_lock;

void socket_init() {
  initlock(&socket_lock, "socket");
}

// Create a new socekt. Currently, only socket only supports
// TCP and IPv4 protocol.
int socket() {
  struct file* f;
  struct tcp_pcb* pcb;
  int fd;

  acquire(&socket_lock);
  pcb = tcp_new();
  release(&socket_lock);
  if (pcb == NULL) {
    panic("not enough memory to create a new socket.\n");
  }

  f = filealloc();
  if (f == 0) {
    panic("fail to alloc struct file.\n");
  }
  f->pcb = pcb;

  fd = fdalloc(f);
  if (fd == -1) {
    panic("fail to alloc fd for struct file.\n");
  }

  return fd;
}

int socket_bind() { return 0; }

int socket_listen() { return 0; }

int socket_accept() { return 0; }

int socket_connect() { return 0; }

int socket_read() { return 0; }

int socket_write() { return 0; }

int socket_close() { return 0; }
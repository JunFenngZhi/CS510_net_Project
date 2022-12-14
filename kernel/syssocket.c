#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_socket(void) { return socket(); }

uint64 sys_socket_connect() {
  int fd;
  uint64 ip_addr;
  uint64 port;
  struct file* f;
  
  if (argint(0, &fd) < 0) {
    panic("fail to get fd.");
  }
  if (argint(1, (int*)&ip_addr) < 0) {
    panic("fail to get ip_addr.");
  }
  if (argint(2, (int*)&port) < 0) {
    panic("fail to get port.");
  }
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0) {
    panic("incorrect fd.");
  }
  return socket_connect(f, ip_addr, port);
}

uint64 sys_socket_bind(void) {
  int fd;
  uint64 ip_addr;
  uint64 port;
  struct file* f;

  if (argint(0, &fd) < 0) {
    panic("fail to get fd.");
  }
  if (argint(1, (int*)&ip_addr) < 0) {
    panic("fail to get ip_addr.");
  }
  if (argint(2, (int*)&port) < 0) {
    panic("fail to get port.");
  }
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0) {
    panic("incorrect fd.");
  }

  return socket_bind(f, ip_addr, port);
}

uint64 sys_socket_listen(void) {
  int fd;
  struct file* f;

  if (argint(0, &fd) < 0) {
    panic("fail to get fd.");
  }
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0) {
    panic("incorrect fd.");
  }

  return socket_listen(f);
}

uint64 sys_socket_accept(void) {
  int fd;
  struct file* f;

  if (argint(0, &fd) < 0) {
    panic("fail to get fd.");
  }
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0) {
    panic("incorrect fd.");
  }

  return socket_accept(f);
}
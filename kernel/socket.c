#include "lwip/include/lwip/tcp.h"
#include "lwip/include/lwip/ip_addr.h"
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

void socket_init() { initlock(&socket_lock, "socket"); }

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

// This function will be called when tcp connection is established.
err_t tcp_connect_success(void *arg, struct tcp_pcb *tpcb, err_t err){
  printf("tcp connection builds successfully.\n");
  int* flag = arg;
  *(flag) = 1;
  wakeup(flag);
  return err;
}

// Error callback function for tcp pcb block. This function will be called,
// when fatal errors happen in tcp connection.
void tcp_connect_failure(void* arg, err_t err){
  printf("tcp connection fails because %d. Re-try tcp_connect().\n", err);
}

// Create a connection to specific {ip,port}.
// It will keep blocking until connection is established.
// Return 0 when succeed, otherwise it will panic the error infomation.
int socket_connect(struct file* f, uint32 ip, uint16 prot) {
  struct tcp_pcb* pcb = f->pcb;
  int success = 0;
  tcp_connected_fn success_fn = tcp_connect_success;
  acquire(&socket_lock);

  // setup failure call back
  tcp_err_fn failure_fn = tcp_connect_failure;
  tcp_err(pcb, failure_fn);

  // setup args for all the call back function
  tcp_arg(pcb, &success);

  // Proc will sleep and wait until wakeup by success_fn.
  // If TCP connection fails, proc will re-try after error callback fn.
  while (success == 0) {
    err_t res = tcp_connect(pcb, (ip_addr_t*)&ip, prot, success_fn);
    if (res != ERR_OK) {
      printf("res = %d", res);
      panic("Error when calling tcp_connect");
    }
    sleep(&success, &socket_lock);
  }

  release(&socket_lock);
  return 0;
}

int socket_bind() { return 0; }

int socket_listen() { return 0; }

int socket_accept() { return 0; }

int socket_read() { return 0; }

int socket_write() { return 0; }

int socket_close() { return 0; }


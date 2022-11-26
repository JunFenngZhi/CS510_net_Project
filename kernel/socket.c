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
  f->type = FD_SOCKET;
  f->pcb = pcb;
  f->readable = 1;
  f->writable = 1;

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
  int* flag = arg;
  wakeup(flag);
  return err;
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
  err_t res = tcp_connect(pcb, (ip_addr_t*)&ip, prot, success_fn);
  if (res != ERR_OK) {
    printf("res = %d", res);
    panic("Error when calling tcp_connect");
  }
  sleep(&success, &socket_lock);
  release(&socket_lock);
  if (success == 0) { // TCP connect fails
    return -1;
  }

  return 0;
}

int socket_bind(struct file* f, uint32 ip, uint16 port) {
  struct tcp_pcb* pcb = f->pcb;
  acquire(&socket_lock);

  err_t res = tcp_bind(pcb, (ip_addr_t*)&ip, port);
  if (res != ERR_OK) {
    printf("res = %d", res);
    panic("Error when calling tcp_connect");
    return ERR_USE;
  }

  release(&socket_lock);
  return ERR_OK;
}

int socket_listen() { return 0; }

int socket_accept() { return 0; }

// Callback function when data has been received
err_t tcp_recv_packet(void* arg, struct tcp_pcb* tpcb, struct pbuf* p,
                      err_t err) {
    //TODO: get data from p. how to copyout?

    //TODO: wakeup proc based on tpcb
}

// Read up to n bytes from socket to buf. 
// This function will block until data is available.
// Return the num of bytes that it gets,
// Return -1 if error happens.
int socket_read(struct file *f, uint64 buf, int n) { 
  struct tcp_pcb* pcb= f->pcb; 

  if(n<=0){
    panic("invalid length of buffer for socket_read().");
  }

  acquire(&socket_lock);
  tcp_recv_fn recv_fn = tcp_recv_packet;
  tcp_recv(pcb, recv_fn);

  while(1){
   //TODO: sleep and wakeup by tcp_recv_packet()
  }
  
  release(&socket_lock);
  return 0;
}

int socket_write() { return 0; }

int socket_close() { return 0; }


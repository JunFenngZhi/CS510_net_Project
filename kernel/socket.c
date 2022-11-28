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

enum STATUS { BLOCK, FAILURE, SUCCESS };

struct recv_callback_args {
  uint64 buf;
  int avail_buf_len;  // available buf length
  int data_len;
  enum STATUS status;
};

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
err_t tcp_connect_success(void* arg, struct tcp_pcb* tpcb, err_t err) {
  printf("tcp connection builds successfully.\n");
  int* flag = arg;
  *(flag) = 1;
  wakeup(flag);
  return err;
}

// Error callback function for tcp pcb block. This function will be called,
// when fatal errors happen in tcp connection.
void tcp_connect_failure(void* arg, err_t err) {
  printf("tcp connection fails because %d. Re-try tcp_connect().\n", err);
  int* flag = arg;
  wakeup(flag);
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

  // Proc will sleep and wait until wakeup by callback fn.
  err_t res = tcp_connect(pcb, (ip_addr_t*)&ip, prot, success_fn);
  if (res != ERR_OK) {
    printf("res = %d", res);
    panic("Error when calling tcp_connect");
  }
  sleep(&success, &socket_lock);
  release(&socket_lock);

  if (success == 0) {  // TCP connect fails
    return -1;
  }

  return 0;
}

// Bind given socekt to specific {ip,port}
int socket_bind(struct file* f, uint32 ip, uint16 port) {
  struct tcp_pcb* pcb = f->pcb;
  acquire(&socket_lock);

  err_t res = tcp_bind(pcb, (ip_addr_t*)&ip, port);
  if (res != ERR_OK) {
    printf("res = %d", res);
    panic("Error when calling tcp_bind");
    return res;
  }

  release(&socket_lock);
  return ERR_OK;
}

// Set the state of the connection to be LISTEN,
// which means that it is able to accept incoming connections.
// Return 0 if successful. Return -1 for errors.
int socket_listen(struct file* f) {
  struct tcp_pcb* pcb = f->pcb;
  acquire(&socket_lock);

  pcb = tcp_listen_with_backlog(pcb, TCP_DEFAULT_LISTEN_BACKLOG);
  if (pcb == NULL) {
    printf("No memory was available for the listening connection.\n");
    release(&socket_lock);
    return -1;
  }
  f->pcb = pcb;

  release(&socket_lock);
  return 0;
}

// This function will be called when tcp aceept successfully
err_t tcp_accept_success(void* arg, struct tcp_pcb* newpcb, err_t err) {
  struct file* f;
  int fd;

  printf("tcp accept successfully.\n");
  int* skfd = arg;

  f = filealloc();
  if (f == 0) {
    panic("fail to alloc struct file.\n");
  }
  f->type = FD_SOCKET;
  f->pcb = newpcb;
  f->readable = 1;
  f->writable = 1;

  fd = fdalloc(f);
  if (fd == -1) {
    panic("fail to alloc fd for struct file.\n");
  }
  *skfd = fd;

  wakeup(skfd);
  return err;
}

// Error callback function for tcp pcb block. This function will be called,
// when fatal errors happen in tcp connection.
void tcp_accept_failure(void* arg, err_t err) {
  printf("tcp accept fails because %d.\n", err);
  int* flag = arg;
  *flag = -1;
  wakeup(flag);
}

// Accept connection from other host, and return a new socket
// for each new connection. This function will block until
// new connection is accepted.
int socket_accept(struct file* f) {
  int skfd;

  struct tcp_pcb* pcb = f->pcb;
  acquire(&socket_lock);

  tcp_accept_fn success_fn = tcp_accept_success;

  // setup failure call back
  tcp_err_fn failure_fn = tcp_accept_failure;
  tcp_err(pcb, failure_fn);

  // setup args for all the call back function
  tcp_arg(pcb, &skfd);

  // call tcp_accept
  tcp_accept(pcb, success_fn);
  sleep(&skfd, &socket_lock);

  release(&socket_lock);

  if (skfd == -1) {
    printf("tcp accept err\n");
  }
  return skfd;
}

// Callback function when data has been received.
// This function will copy data from pbuf to buf,
// and wakeup proc to stop blocking. Also it will
// optionally free pbuf.
err_t tcp_recv_packet(void* arg, struct tcp_pcb* tpcb, struct pbuf* p,
                      err_t err) {
  struct recv_callback_args* args = arg;

  // Extract data to buf
  struct pbuf* ptr = p;
  int offset = 0;  // buf offset
  int copyLen = 0;
  while (ptr != NULL) {
    if (args->avail_buf_len >= ptr->len) {
      copyLen = ptr->len;
    } else {
      copyLen = args->avail_buf_len;
    }
    memmove((void*)args->buf + offset, (const void*)ptr->payload, copyLen);
    args->avail_buf_len -= copyLen;
    args->data_len += copyLen;
    offset += copyLen;
    if (args->avail_buf_len == 0)  // run out of buffer
      break;
    ptr = ptr->next;
  }

  // Wakeup proc, update status
  if (args->data_len == 0) {
    printf("connection is closed.\n");
    args->status = FAILURE;
  } else {
    args->status = SUCCESS;
  }
  wakeup(&(args->status));

  //TODO: free pbuf
  // struct pbuf* next = NULL;
  // ptr = p;
  // if (err == ERR_OK || err == ERR_ABRT) {
  //   while (ptr != NULL) {
  //     next = ptr->next;
  //     free(ptr);
  //     ptr = next;
  //   }
  // }
  return err;
}

// Read up to n bytes from socket to buf.
// This function will block until data is available.
// Return the num of bytes that it gets,
// Return -1 if error happens.
int socket_read(struct file* f, uint64 buf, int n) {
  struct tcp_pcb* pcb = f->pcb;
  struct recv_callback_args args;
  args.buf = buf;
  args.avail_buf_len = n;
  args.data_len = 0;
  args.status = BLOCK;
  tcp_recv_fn recv_fn = tcp_recv_packet;

  if (n <= 0) {
    panic("invalid length of buffer for socket_read().");
  }

  acquire(&socket_lock);
  tcp_arg(pcb, &args);
  tcp_recv(pcb, recv_fn);

  // block
  sleep(&(args.status), &socket_lock);
  tcp_recved(pcb, args.data_len);
  release(&socket_lock);

  if (args.status == FAILURE) {
    printf("fail to recv data.\n");
    return -1;
  } else if (args.status == BLOCK) {
    panic("status error.");
  }

  return args.data_len;
}

// Send n bytes from buf to socket. When the message does not fit into the
// send buffer of the socket, this function will normally block.
// Return the num of bytes that it sends,
// Return -1 if error happens.
int socket_write(struct file* f, uint64 data, int n) {
  // struct tcp_pcb* pcb = f->pcb;

  // if (n <= 0) {
  //   panic("invalid length of data for socket_write().");
  // }
  // TODO: implementation is not decided yet
  return 0;
}

// Close the connection of given socket.
// TCP_PCB block will be freed automatically.
int socket_close(struct file* f) {
  struct tcp_pcb* pcb = f->pcb;
  err_t res;

  while (1) {
    res = tcp_close(pcb);
    if (res == ERR_OK) {
      break;
    } else if (res == ERR_MEM) {
      // wait and re-try close the connection
      for (int i = 0; i < 10000000; i++) {}
    } else {
      printf("tcp_close() return %d\n", res);
      panic("tcp_close() fails.");
    }
  }

  return 0;
}

// MARK: Do we need to set up a new kernel buffer for each socekt
// to separate lWip and kernel OS. If so, we need to update socket_read(),
// socket(), tcp_accept_success(). Also, we needs to add a timer interrupt.

// TODO: buf is a user virtual address. copyout memory to user space?
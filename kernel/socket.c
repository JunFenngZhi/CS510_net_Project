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

/*-------------------- HELPER FUNCTION ---------------------------*/
void free_recv_buf_desc(struct file* f) {
  if (f->rbuf_size <= 0) {
    panic("wrongly free recv_buf_desc");
  }
  f->rbuf_size--;
  int idx = f->rbuf_head % BUF_SIZE;
  kfree((void*)f->rbuf[idx].addr);
  f->rbuf[idx].addr = 0;
  f->rbuf[idx].len = 0;
  f->rbuf_head++;
}

int alloc_recv_buf_desc(struct file* f) {
  if (f->rbuf_size >= BUF_SIZE) {
    printf("socket recv buf is full.\n");
    return -1;
  }
  int idx = f->rbuf_tail % BUF_SIZE;
  f->rbuf[idx].addr = (uint64)kalloc();
  f->rbuf[idx].len = PGSIZE;
  f->rbuf_tail++;

  return idx;
}

/*-------------------- SOCKET FUNCTION ---------------------------*/
void socket_init() { initlock(&socket_lock, "socket"); }

// Create a new socekt. Currently, socket only supports
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

  // allocate a file struct for socket
  f = filealloc();
  if (f == 0) {
    panic("fail to alloc struct file.\n");
  }
  f->type = FD_SOCKET;
  f->pcb = pcb;
  f->readable = 1;
  f->writable = 1;
  f->status = PENDING;

  // initialize socekt recv buf
  f->rbuf_size = 0;
  f->rbuf_head = 0;
  f->rbuf_tail = 0;

  // allocate a fd from current process and connect it to struct file f.
  fd = fdalloc(f);
  if (fd == -1) {
    panic("fail to alloc fd for struct file.\n");
  }

  return fd;
}

// Close the connection of given socket.
// TCP_PCB block will be freed automatically.
int socket_close(struct file* f) {
  struct tcp_pcb* pcb = f->pcb;
  err_t res;
  acquire(&socket_lock);

  // clean recv buffer
  while (f->rbuf_size != 0){
    free_recv_buf_desc(f);
  }

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
  printf("successfully close socket.\n");
  release(&socket_lock);
  return 0;
}

/*-------------------- IO FUNCTION ---------------------------*/
// Callback function when data has been received.
// This function will copy data from pbuf to socket recv buf,
// and wakeup proc to stop blocking. Also it will
// optionally free pbuf.
err_t tcp_recv_packet(void* arg, struct tcp_pcb* tpcb, struct pbuf* p,
                      err_t err) {
  struct file* f = arg;

  if (err != ERR_OK) {
    printf("tcp_recv callbacl Err = %d\n", err);
    if (err == ERR_ABRT) pbuf_free(p);
    f->status = FAILURE;
    wakeup(f);
    return err;
  }

  if (p == NULL) {
    printf("TCP connection is closed by remote host.\n");
    f->status = FAILURE;
    wakeup(f);
    return ERR_CLSD;
  }

  // Recv buf is full. Data loss here.
  int idx = alloc_recv_buf_desc(f);
  if (idx == -1) {
    f->status = FAILURE;
    wakeup(f);
    return ERR_BUF;
  }

  // Extract data from pbuf to recv_desc_buf
  if (p->tot_len > PGSIZE) {
    panic("packet is larger than one pagesize.");
  }
  f->rbuf[idx].len = p->tot_len;
  struct pbuf* ptr = p;
  int offset = 0;
  while (ptr != NULL) {
    memmove((void*)f->rbuf[idx].addr + offset, (const void*)ptr->payload,
            ptr->len);
    offset += ptr->len;
    ptr = ptr->next;
  }

  pbuf_free(p);
  f->status = SUCCESS;
  wakeup(f);

  return err;
}

// Read up to n bytes from socket to buf.
// This function will block until data is available.
// Return the num of bytes that it gets,
// Return -1 if error happens.
int socket_read(struct file* f, uint64 buf, int n) {
  acquire(&socket_lock);
  struct tcp_pcb* pcb = f->pcb;
  f->status = PENDING;

  // wait for available data
  while (f->rbuf_size == 0) {
    sleep(f, &socket_lock);
    if (f->status == FAILURE) {
      release(&socket_lock);
      return -1;
    }
  }

  // copyout data from socket kernel recv buf to buf
  int packet_len = f->rbuf[(f->rbuf_head) % BUF_SIZE].len;
  void* krecv_buf = (void*)f->rbuf[(f->rbuf_head) % BUF_SIZE].addr;
  int copy_len = packet_len > n ? n : packet_len;
  copyout(myproc()->pagetable, buf, krecv_buf, copy_len);

  // get all the data, free this recv_buf_desc
  if (copy_len == packet_len) {
    free_recv_buf_desc(f);
    tcp_recved(pcb, packet_len);  // notify lwip to allow new packet come in.
  } else {  // some data remains, update this recv_buf_desc
    memmove(krecv_buf, krecv_buf + copy_len, packet_len - copy_len);
    f->rbuf[(f->rbuf_head) % BUF_SIZE].len -= copy_len;
  }

  release(&socket_lock);
  return copy_len;
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

/*-------------------- CLIENT SIDE FUNCTION ---------------------------*/
// This function will be called when tcp connection is established.
// It will setup recv callback for this socekt.
err_t tcp_connect_success(void* arg, struct tcp_pcb* tpcb, err_t err) {
  printf("tcp connection builds successfully.\n");
  struct file* f = arg;

  if (f->pcb != tpcb) {
    panic("arg and tpcv do not match in callback fn.");
  }

  f->status = SUCCESS;
  tcp_arg(tpcb, f);
  tcp_recv(tpcb, tcp_recv_packet);

  wakeup(f);
  return err;
}

// Error callback function for tcp pcb block. This function will be called,
// when fatal errors happen in tcp connection.
void tcp_connect_failure(void* arg, err_t err) {
  printf("tcp connection fails because err = %d.\n", err);
  struct file* f = arg;
  f->status = FAILURE;
  wakeup(f);
}

// Create a connection to specific {ip,port}.
// It will keep blocking until connection is established.
// Return 0 when succeed, otherwise it will panic the error infomation.
int socket_connect(struct file* f, uint32 ip, uint16 prot) {
  struct tcp_pcb* pcb = f->pcb;
  tcp_connected_fn success_fn = tcp_connect_success;
  f->status = PENDING;
  acquire(&socket_lock);

  // setup failure call back
  tcp_err_fn failure_fn = tcp_connect_failure;
  tcp_err(pcb, failure_fn);

  // setup args for all the call back function
  tcp_arg(pcb, f);

  // Proc will sleep and wait until wakeup by callback fn.
  err_t res = tcp_connect(pcb, (ip_addr_t*)&ip, prot, success_fn);
  if (res != ERR_OK) {
    printf("err = %d", res);
    panic("Error when calling tcp_connect");
  }
  sleep(f, &socket_lock);
  release(&socket_lock);

  if (f->status == FAILURE) {  // TCP connect fails
    return -1;
  } else if (f->status == PENDING) {
    panic("incorrect status value.");
  }

  return 0;
}

/*-------------------- SERVER SIDE FUNCTION ---------------------------*/
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

// MARK: Do we need to set up a new kernel buffer for each socekt
// to separate lWip and kernel OS. If so, we need to update socket_read(),
// socket(), tcp_accept_success(). Also, we needs to add a timer interrupt.

// TODO: sleep variable is changed

// TODO: f->status == PENDING in the begining of every funciton 
//       thats needs to wakeup by callback
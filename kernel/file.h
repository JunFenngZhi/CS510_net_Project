#define BUF_SIZE 8

struct recv_buf_desc
{
  uint64 addr;
  int len;
};

struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE, FD_SOCKET } type;
  
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE and FD_DEVICE
  short major;       // FD_DEVICE
  short minor;       // FD_DEVICE
  struct tcp_pcb* pcb; // FD_SOCKET
  enum { SUCCESS, FAILURE, PENDING} status; // FD_SOCKET
  int rbuf_size;     // FD_SOCKET
  struct recv_buf_desc rbuf[BUF_SIZE]; // FD_SOCKET
  int rbuf_head;     // FD_SOCKET
  int rbuf_tail;     // FD_SOCKET
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// map major device number to device functions.
struct devsw {
  int (*read)(struct file *, int, uint64, int);
  int (*write)(struct file *, int, uint64, int);
};

extern struct devsw devsw[];

#define DISK 0
#define CONSOLE 1

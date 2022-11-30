#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

// 8 descriptors for net IO operation.
#define NUM 8
#define READ 0
#define SEND 1

struct info {
  struct buf *b;
  char status;
};

struct eth_buffer {
  char data[1514];
};

struct net {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;

  char free[NUM];   // keep trach of free status of each descriptor
  uint16 used_idx;  // previous index in used ring[]

  // net command headers.
  // one-for-one with descriptors, for convenience.
  // worked as a temp variable for each descriptor.
  // here will be used to save header for each operation.
  struct virtio_net_hdr ops[NUM/2];
  // ethernet packet buffers
  struct eth_buffer pbuf[NUM/2];
} net_send, net_recv;

struct spinlock vnettx_lock, vnetrx_lock;


/*------------------------ HELPER FUNCTION --------------------------*/
// find a free descriptor, mark it non-free, return its index.
static int alloc_desc(int send) {
  struct net *net = send == 1 ? &net_send : &net_recv;
  for (int i = 0; i < NUM; i++) {
    if (net->free[i]) {
      net->free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void free_desc(int i, int send) {
  struct net *net = send == 1 ? &net_send : &net_recv;
  if (i >= NUM) panic("virtio_net 1");
  if (net->free[i]) panic("virtio_net 2");
  net->desc[i].addr = 0;
  net->free[i] = 1;
  wakeup(&net->free[0]);
}

// free a chain of descriptors.
static void free_chain(int i, int send) {
  struct net *net = send == 1 ? &net_send : &net_recv;
  while (1) {
    free_desc(i, send);
    if (net->desc[i].flags & VIRTQ_DESC_F_NEXT)
      i = net->desc[i].next;
    else
      break;
  }
}

static int alloc2_desc(int *idx, int send) {
  for (int i = 0; i < 2; i++) {
    idx[i] = alloc_desc(send);
    if (idx[i] < 0) {
      for (int j = 0; j < i; j++)
        free_desc(idx[j], send);  // free those already allocated descriptors
      return -1;
    }
  }
  //printf("alloc %s desc: %d,%d\n",send?"Tx":"Rx",idx[0],idx[1]);
  return 0;
}

void initialize_queue(int queue_num) {
  struct net *net = queue_num == SEND ? &net_send : &net_recv;

  *R(VIRTIO_MMIO_QUEUE_SEL) = queue_num;
  if (*R(VIRTIO_MMIO_QUEUE_READY))  // avoid reseting queue repeatly
    panic("virtio disk ready not zero");
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0) panic("virtio net does not have this queue");
  if (max < NUM) panic("virtio net max queue too short");
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // point out the address to device. Specify high address and low address
  // separately. Disk will read/SEND these regions.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)net->desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)net->desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)net->avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)net->avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)net->used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)net->used >> 32;

  /* Queue ready. Initialization finished*/
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;
}

// place an empty recv block(2 descriptors) into virtio_recv queue
void place_recv_block() {
  int idx[2];
  while (1) {
    if (alloc2_desc(idx, READ) == 0) {
      break;
    }
    sleep(&net_recv.free[0], &vnetrx_lock);
  }

  // set the header for this operation
  struct virtio_net_hdr *buf0 = &net_recv.ops[idx[0]/2];
  buf0->flags = 0;
  buf0->gso_type = VIRTIO_NET_HDR_GSO_NONE;

  // set the first descriptor(header)
  net_recv.desc[idx[0]].addr = (uint64)buf0;
  net_recv.desc[idx[0]].len = sizeof(struct virtio_net_hdr);
  net_recv.desc[idx[0]].flags = VIRTQ_DESC_F_WRITE;
  net_recv.desc[idx[0]].flags |= VIRTQ_DESC_F_NEXT;
  net_recv.desc[idx[0]].next = idx[1];

  // set the secode descriptor(data)
  net_recv.desc[idx[1]].addr = (uint64)&(net_recv.pbuf[idx[0]/2].data);
  net_recv.desc[idx[1]].len = PGSIZE;
  net_recv.desc[idx[1]].flags = VIRTQ_DESC_F_WRITE;  // device SENDs b->data
  net_recv.desc[idx[1]].next = 0;

  // avail->idx tells the device how far to look in avail->ring.
  // avail->ring[...] are desc[] indices the device should process.
  // we only tell device the first index in our chain of descriptors.
  // add this new running operation to avail ring[]
  net_recv.avail->ring[net_recv.avail->idx % NUM] = idx[0];
  net_recv.avail->flags = 1;
  __sync_synchronize();
  net_recv.avail->idx += 1;

  if (net_recv.used->flags == 0) {
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = READ;  // value is queue number
  }
}

/*------------------------ DRIVER --------------------------*/
// initialize the NIC and store the MAC address 
void virtio_net_init(void *mac) {
  printf("virtio_net_init begin.\n");

  initlock(&vnettx_lock, "virtio_net Tx");
  initlock(&vnetrx_lock, "virtio_net Rx");

  uint32 status = 0;

  for (int i = 0; i < 2; i++) {
    struct net *net = i == SEND ? &net_send : &net_recv;

    net->desc = kalloc();
    net->avail = kalloc();
    net->used = kalloc();
    if (!net->desc || !net->avail || !net->used)
      panic("virtio net kalloc fail");
    memset(net->desc, 0, PGSIZE);
    memset(net->avail, 0, PGSIZE);
    memset(net->used, 0, PGSIZE);
  }

  if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
      *R(VIRTIO_MMIO_VERSION) != 2 || *R(VIRTIO_MMIO_DEVICE_ID) != 1 ||
      *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    panic("could not find virtio net");
  }

  // Reset the device.
  *R(VIRTIO_MMIO_STATUS) = status;

  // Set the ACKNOWLEDGE bit.
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // Set the DRIVER bit.
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // Negotiate features.
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= (1 << VIRTIO_NET_F_MAC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // Tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // Ensure the FEATURES_OK bit is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if (!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio net FEATURES_OK unset");

  printf("initialize virtio queue.\n");
  initialize_queue(READ);  // receive queue
  initialize_queue(SEND);  // transmit queue

  // all NUM descriptors start out unused.
  for (int i = 0; i < NUM; i++) {
    net_send.free[i] = 1;
    net_recv.free[i] = 1;
  }

  // Tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // pass out mac address(48bits)
  uint8 mac_ad[6] = {0};
  for (int i = 0; i < 6; i++) {
    mac_ad[i] = (uint8)*R(VIRTIO_MMIO_CONFIG + i * sizeof(uint8));
  }
  memmove(mac, mac_ad, 6);

  // Add initial block to recv queue
  //void *buf = (char*)&(net_send.pbuf[0].data);
  for (int i = 0; i < NUM/2; i++) {
    place_recv_block();
  }

  printf("virtio_net_init finished.\n");
}

// Send data. Free previous completed descriptors,
// and allocate new descriptors for current operation.
// Place the descriptor into queue, notify the device,
// and then exit immediately.
int virtio_net_send(const void *data, int len) {
  acquire(&vnettx_lock);

  // free the descriptors of send op that just complete.
  while ((net_send.used_idx % NUM) != (net_send.used->idx % NUM)) {
    int id = net_send.used->ring[net_send.used_idx].id;
    //int packet_id = net_send.desc[id].next;

    // update use index
    //kfree((void *)net_send.desc[packet_id].addr);

    // free the used descriptor
    net_send.used_idx = (net_send.used_idx + 1) % NUM;
    free_chain(id, SEND);
  }

  // two blocks: first for virtio net header; second the for packet
  int idx[2];
  if (alloc2_desc(idx, SEND)) {
    // device is busy
    release(&vnettx_lock);
    return -1;
  }

  // populate the two descriptors.
  struct virtio_net_hdr *buf0 = &net_send.ops[idx[0]/2];

  // set the header for this operation
  buf0->flags = 0;
  buf0->gso_type = VIRTIO_NET_HDR_GSO_NONE;

  // set the first descriptor(header)
  net_send.desc[idx[0]].addr = (uint64)buf0;
  net_send.desc[idx[0]].len = sizeof(struct virtio_net_hdr);
  net_send.desc[idx[0]].flags = VIRTQ_DESC_F_NEXT;
  net_send.desc[idx[0]].next = idx[1];

  char *packet_buffer = (char*)&(net_send.pbuf[idx[0]/2].data);
  memmove(packet_buffer, data, len);

  // set the second descriptor(data)
  net_send.desc[idx[1]].addr = (uint64)packet_buffer;
  net_send.desc[idx[1]].len = len;
  net_send.desc[idx[1]].flags = 0;  // device read b->data
  net_send.desc[idx[1]].next = 0;

  // avail->idx tells the device how far to look in avail->ring.
  // avail->ring[...] are desc[] indices the device should process.
  // we only tell device the first index in our chain of descriptors.
  // add this new running operation to avail ring[]
  net_send.avail->ring[net_send.avail->idx % NUM] = idx[0];
  net_send.avail->flags = 1;
  __sync_synchronize();
  net_send.avail->idx += 1;

  // put descriptors into queue, notify
  if (net_send.used->flags == 0) {
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = SEND;  // value is queue number
  }

  release(&vnettx_lock);
  return 0;
}

// Receive data. This function will first whether there is completed read block. 
// If so, it will load one read operation into buffer and return the number of
// bytes that it read. Then, it will generata a new empty recv block and place it into 
// recv virtio queue. If there is no completed read block, return 0 immediately.
int virtio_net_recv(void *data, int len) {
  acquire(&vnetrx_lock);

  // free the descriptors of read op that just complete.
  if ((net_recv.used_idx % NUM) != (net_recv.used->idx % NUM)) {
    int id = net_recv.used->ring[net_recv.used_idx].id;
    int packet_id = net_recv.desc[id].next;

    // copyout data from descriptor
    int packet_buf_len = net_recv.used->ring[net_recv.used_idx].len;
    int recv_len = packet_buf_len > len ? len : packet_buf_len;
    if (recv_len > 1514) {
      panic("packet is too large. Data loss!");
    }
    memmove(data, (void *)net_recv.desc[packet_id].addr, recv_len);


    // free the used descriptor
    net_recv.used_idx = (net_recv.used_idx + 1) % NUM;
    free_chain(id, READ);

    // Insert a new descriptor into the queue
    //uint64 recv_buf = net_recv.desc[packet_id].addr;
    place_recv_block();

    release(&vnetrx_lock);
    return recv_len;
  } else {  // nothing done, return immediately
    release(&vnetrx_lock);
    return 0;
  }
}




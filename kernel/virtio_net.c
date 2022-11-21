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

struct info {
  struct buf *b;
  char status;
};

struct net {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;

  char free[NUM];   // keep trach of free status of each descriptor
  uint16 used_idx;  // previous index in used ring[]

  //struct info info[NUM];
  // struct virtio_blk_req ops[NUM];

  // net command headers.
  // one-for-one with descriptors, for convenience.
  // worked as a temp variable for each descriptor.  
  // here will be used to save header for each operation.
  struct virtio_net_hdr ops[NUM];
  //struct spinlock vnet_lock;
} net_send, net_recv;

struct spinlock vnet_lock;

void initialize_queue(int queue_num);

/* initialize the NIC and store the MAC address */
void virtio_net_init(void *mac) {
  printf("virtio_net_init begin.\n");

  initlock(&vnet_lock, "virtio_net");
  initlock(&vnet_lock, "virtio_net");

  uint32 status = 0;

  for (int i = 0; i < 2; i++) {
    struct net *net = i == 1 ? &net_send : &net_recv;
    
    net->desc = kalloc();
    net->avail = kalloc();
    net->used = kalloc();
    if (!net->desc || !net->avail || !net->used) panic("virtio net kalloc fail");
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
  //TODO: check bits here
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
  initialize_queue(0);  // receive queue
  initialize_queue(1);  // transmit queue

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
  for(int i =0;i<6;i++){
    mac_ad[i]= (uint8)*R(VIRTIO_MMIO_CONFIG + i*sizeof(uint8));
  }
  memmove(mac, mac_ad, 6);

  printf("virtio_net_init finished.\n");
}


// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc(int send)
{
  struct net *net = send == 1 ? &net_send : &net_recv;
  for(int i = 0; i < NUM; i++){
    if(net->free[i]){
      net->free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(int i, int send)
{
  struct net *net = send == 1 ? &net_send : &net_recv;
  if(i >= NUM)
    panic("virtio_net 1");
  if(net->free[i])
    panic("virtio_net 2");
  net->desc[i].addr = 0;
  net->free[i] = 1;
  wakeup(&net->free[0]);
}

// free a chain of descriptors.
static void
free_chain(int i, int send)
{
  struct net *net = send == 1 ? &net_send : &net_recv;
  while(1){
    free_desc(i, send);
    if(net->desc[i].flags & VIRTQ_DESC_F_NEXT)
      i = net->desc[i].next;
    else
      break;
  }
}

static int
alloc2_desc(int *idx, int send)
{
  for(int i = 0; i < 2; i++){
    idx[i] = alloc_desc(send);
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j], send); // free those already allocated descriptors
      return -1;
    }
  }
  return 0;
}

/* send/receive data */
int virtio_net_sr(const void *data, int len, int send) {
  struct net *net = send == 1 ? &net_send : &net_recv;

  printf("virtio_net_sr: enter sr, send 1 recv 0: %d\n", send);
  acquire(&vnet_lock);
  // the spec says that legacy block operations use two
  // descriptors: one for package header, one for
  // the data

  // allocate the two descriptors. save their indexs in idx[2]
  // If there are not enough free descriptors, sleep and wait.
  int idx[2];
  while(1){
    if(alloc2_desc(idx, send) == 0) {
      break;
    }
    sleep(&net->free[0], &vnet_lock);
  }

  // format the two descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_net_hdr *buf0 = &net->ops[idx[0]];

  // set the header for this operation
  buf0->flags = 0; 
  buf0->gso_type = VIRTIO_NET_HDR_GSO_NONE;
  buf0->num_buffers = send ? 0 : 0;

  // set the first descriptor(header)
  net->desc[idx[0]].addr = (uint64) buf0;
  net->desc[idx[0]].len = sizeof(struct virtio_net_hdr);
  net->desc[idx[0]].flags = VIRTQ_DESC_F_NEXT;
  net->desc[idx[0]].next = idx[1];

  // set the secode descriptor(data)
  net->desc[idx[1]].addr = (uint64)data;
  net->desc[idx[1]].len = len;
  if (send)
    net->desc[idx[1]].flags = 0; // device read b->data
  else
    net->desc[idx[1]].flags = VIRTQ_DESC_F_WRITE; // device writes b->data
  net->desc[idx[1]].next = 0;

  // avail->idx tells the device how far to look in avail->ring.
  // avail->ring[...] are desc[] indices the device should process.
  // we only tell device the first index in our chain of descriptors.
  // add this new running operation to avail ring[]
  net->avail->ring[net->avail->idx % NUM] = idx[0];
  net->avail->flags = 1;
  __sync_synchronize();
  net->avail->idx += 1;

  printf("virtio_net_sr: device used id %d, user used id %d, used flags %d\n", net->used->idx % NUM, net->used_idx, net->used->flags);
  if (net->used->flags == 0) *R(VIRTIO_MMIO_QUEUE_NOTIFY) = send ? 1 : 0; // value is queue number
  for (int i = 0; i < 10000000; i++) {}
  printf("virtio_net_sr: device used id %d, user used id %d\n", net->used->idx % NUM, net->used_idx);

  int count = 0;
  while (1) {
    count++;
    //if (count % 1000000 == 0)
      //printf("virtio_net_sr: breaker\n");

    release(&vnet_lock);
    acquire(&vnet_lock);

    // Device will put the finished operation into used ring.
    // We need to handle latest finished operation.
    if((net->used_idx % NUM) != (net->used->idx % NUM)){
      int id = net->used->ring[net->used_idx].id;
      // make sure to handle its own id
      if (id != idx[0]) continue;
      // update use index and free the descriptor
      len = send ? len : net->used->ring[net->used_idx].len;
      printf("virtio_net_sr: len %d\n", len);
      net->used_idx = (net->used_idx + 1) % NUM;
      free_chain(idx[0], send);

      *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
      break;
    }
  }

  release(&vnet_lock);

  printf("virtio_net_sr: exit sr\n\n");
  return send ? 0 : len; 
}

/* send/receive data; return 0 on success */
int virtio_net_send(const void *data, int len) { 
  return virtio_net_sr(data, len, 1);
 }
/* receive data; return the number of bytes received */
int virtio_net_recv(void *data, int len) {
  return virtio_net_sr(data, len, 0);
}

void initialize_queue(int queue_num) {
  struct net *net = queue_num == 1 ? &net_send : &net_recv;

  *R(VIRTIO_MMIO_QUEUE_SEL) = queue_num;
  if (*R(VIRTIO_MMIO_QUEUE_READY))  // avoid reseting queue repeatly
    panic("virtio disk ready not zero");
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0) panic("virtio net does not have this queue");
  if (max < NUM) panic("virtio net max queue too short");
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // point out the address to device. Specify high address and low address
  // separately. Disk will read/write these regions.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)net->desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)net->desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)net->avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)net->avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)net->used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)net->used >> 32;

  /* Queue ready. Initialization finished*/
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;
}
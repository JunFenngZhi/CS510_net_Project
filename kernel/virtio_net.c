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

  struct info info[NUM];
  // struct virtio_blk_req ops[NUM];

  struct spinlock vnet_lock;
} net;

void initialize_queue(int queue_num);

/* initialize the NIC and store the MAC address */
void virtio_net_init(void *mac) {
  printf("net initialization begin.\n");

  initlock(&net.vnet_lock, "virtio_net");

  uint32 status = 0;

  net.desc = kalloc();
  net.avail = kalloc();
  net.used = kalloc();
  if (!net.desc || !net.avail || !net.used) panic("virtio net kalloc fail");
  memset(net.desc, 0, PGSIZE);
  memset(net.avail, 0, PGSIZE);
  memset(net.used, 0, PGSIZE);

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

  printf("net initialize queue.\n");
  initialize_queue(0);  // receive queue
  initialize_queue(1);  // transmit queue

  // all NUM descriptors start out unused.
  for (int i = 0; i < NUM; i++) net.free[i] = 1;

  // Tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // pass out mac address(48bits) 
  // TODO: fix here
  //memmove(mac, R(VIRTIO_MMIO_CONFIG), 6);

  printf("net initialized finished.\n");
}

/* send data; return 0 on success */
int virtio_net_send(const void *data, int len) { return 0; }

/* receive data; return the number of bytes received */
int virtio_net_recv(void *data, int len) { return 0; }

void initialize_queue(int queue_num) {
  *R(VIRTIO_MMIO_QUEUE_SEL) = queue_num;
  if (*R(VIRTIO_MMIO_QUEUE_READY))  // avoid reseting queue repeatly
    panic("virtio disk ready not zero");
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0) panic("virtio net does not have this queue");
  if (max < NUM) panic("virtio net max queue too short");
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // point out the address to device. Specify high address and low address
  // separately. Disk will read/write these regions.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)net.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)net.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)net.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)net.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)net.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)net.used >> 32;

  /* Queue ready. Initialization finished*/
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;
}
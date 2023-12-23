#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
// desc -> descriptor
// each descriptor contains an address in RAM where the E1000 can write a received packet.
// 用来存需要发送的数据
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
// 循环数组用以存储真正要放数据的空间的地址
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
// 注意到上面需要一个空间，这里多半就是了
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    // 指向可用内存！
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //

  // 真正存数据的位置是mbuf，这里需要做的是把存了需要发送的数据的mbuf的地址放入空闲的发送位置里
  // 而运行完毕之后就需要释放该mbuf

  acquire(&e1000_lock);

  uint32 r_index = regs[E1000_TDT];

  // 检查是否满了
  if ((tx_ring[r_index].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return -1;
  }

  // 已经发送的可以释放了
  if (tx_mbufs[r_index]) {
    mbuffree(tx_mbufs[r_index]);
  }

  // 原有的位置填上正确的需要传输的mbuf
  tx_ring[r_index].addr = (uint64) m->head;
  tx_ring[r_index].length = (uint64) m->len;
  tx_ring[r_index].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;

  tx_mbufs[r_index] = m;
  regs[E1000_TDT] = (regs[E1000_TDT] + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  // 网卡会用DMA的方式把数据放到合适的位置
  // 之后产生中断，进入该函数，该函数主要负责采用net_rx处理mbuf的数据
  // 要记得分配新的mbuf，原来的mbuf应该是给net_rx使用了
  while (1) {
    // 获取下标
    uint32 r_index = (regs[E1000_RDT] + 1) % RX_RING_SIZE;

    if ((rx_ring[r_index].status & E1000_RXD_STAT_DD) == 0) {
      return;
    }
    rx_mbufs[r_index]->len = (uint32)rx_ring[r_index].length;
    if (rx_mbufs[r_index]) {
      // 此时该指针的负责人被转向net_rx，由其负责释放，同时也避免了复制
      net_rx(rx_mbufs[r_index]);
    }
    rx_mbufs[r_index] = mbufalloc(0);
    rx_ring[r_index].addr = (uint64)rx_mbufs[r_index]->head;
    rx_ring[r_index].status = 0;
    regs[E1000_RDT] = r_index;
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}

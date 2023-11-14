// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct
{
  // struct spinlock lock; // 一把大锁保平安
  struct buf buf[NBUF];
  struct buf buckets[NBUCKET];
  struct spinlock locks[NBUCKET];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

void binit(void)
{
  struct buf *b;

  // initlock(&bcache.lock, "bcache");
  for (int i = 0; i < NBUCKET; i++)
  {
    initlock(&bcache.locks[i], "bcache");
    // 先自指之后可以偷男来偷一手
    bcache.buckets[i].prev = &bcache.buckets[i];
    bcache.buckets[i].next = &bcache.buckets[i];
  }

  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // 这里是借用了原有的方法，不过先都挂在第一个bucket上
    // 之后偷一偷分配出去
    b->next = bcache.buckets[0].next;
    b->prev = &bcache.buckets[0];
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].next->prev = b;
    bcache.buckets[0].next = b;
  }

  // Create linked list of buffers
  // 这里是想构成一个首尾相连的双向链表
  // 注意这里head不是指针，而是真的有
  // 下面这段代码的含义是首先让head自己构成一个特殊双向循环链表
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // 然后依次向这个双向循环链表中添加节点，添加位置是head的下一个
  // for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  // {
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b; // 让head的下一个指向正确位置
  // }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  int key = blockno % NBUCKET;

  acquire(&bcache.locks[key]);
  // 找找这个block是不是已经被缓存了
  for (b = bcache.buckets[key].next; b != &bcache.buckets[key]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.locks[key]);
      // 这里不能简单地用自旋锁，否则会导致自旋过久，睡眠锁比较好
      // 其他进程用完这个内存块之后，就会调用brelse，其中会wakeup这些睡眠者
      // 谁先第一个谁就可以返回
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 到这里的时候说明这个block没有被缓存，这个时候得先分配一个buffer给他
  uint minticks = ticks;
  struct buf *victim = 0;

  // 双向链表，回到原点说明遍历完毕
  for (b = bcache.buckets[key].next; b != &bcache.buckets[key]; b = b->next)
  {
    if (b->refcnt == 0 && b->time <= minticks)
    {
      minticks = b->time;
      victim = b;
      // 这里不返回的原因是会找不到真正最小的
    }
  }
  // 此时还需要判断是否找到了这样一个空闲的且是LRU的buffer
  if (victim)
  {
    // 找到了就正常覆盖整个victim
    // 标记这个buffer是属于哪个block的
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;  // 表明之前的数据是无效的
    victim->refcnt = 1; // 有一个引用计数
    release(&bcache.locks[key]);
    // 这个buffer可以直接使用
    // 但为了和之后的brelse中的release搭配还是使用sleep
    acquiresleep(&victim->lock);
    return victim;
  }
  else
  {
    // 说明这个bucket里没有空buffer了，只能从别的bucket里偷
    for (int i = 0; i < NBUCKET; i++)
    {
      if (i == key)
        continue;
      acquire(&bcache.locks[i]);
      minticks = ticks;
      // 找出别的bucket里LRU的那个buffer，给他偷过来
      for (b = bcache.buckets[i].next; b != &bcache.buckets[i]; b = b->next)
      {
        if (b->refcnt == 0 && b->time <= minticks)
        {
          minticks = b->time;
          victim = b;
        }
      }
      // 判断找没找到，找到继续，没找到就跳下一个bucket继续
      if (!victim)
      {
        release(&bcache.locks[i]);
        continue;
      }
      // 找到了之后就正常初始化先
      victim->dev = dev;
      victim->blockno = blockno;
      victim->valid = 0;  // 表明之前的数据是无效的
      victim->refcnt = 1; // 有一个引用计数

      // 下面逻辑比较绕，画个图就很好理解
      // 从这个bucket里取出来放到自己的bucket里
      victim->next->prev = victim->prev;
      victim->prev->next = victim->next;
      release(&bcache.locks[i]);

      victim->next = bcache.buckets[key].next;
      bcache.buckets[key].next->prev = victim;
      bcache.buckets[key].next = victim;
      victim->prev = &bcache.buckets[key];
      release(&bcache.locks[key]);
      acquiresleep(&victim->lock);
      return victim;
    }
  }
  // acquire(&bcache.lock);

  // // Is the block already cached?
  // for (b = bcache.head.next; b != &bcache.head; b = b->next)
  // {
  //   if (b->dev == dev && b->blockno == blockno)
  //   {
  //     b->refcnt++;
  //     // 释放bcache大锁，此时找到了目标块，需要获取目标块的锁了，没必要占着茅坑不拉屎
  //     release(&bcache.lock);
  //     // 尝试获取睡眠锁，获取不了就会睡眠，但最终会返回
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for (b = bcache.head.prev; b != &bcache.head; b = b->prev)
  // {
  //   if (b->refcnt == 0)
  //   {
  //     // 从链表中找到linked.list，找到LRU的buffer块，使之从硬盘中读取先
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  // 如果上述过程都不能获取一个正确的buffer那就报错
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    // valid表示数据是否从磁盘读入了
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  // 检查是否是相同的进程掌握该锁
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // 这里就hash了一手，取模也是hash
  acquire(&bcache.locks[b->blockno % NBUCKET]);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    b->time = ticks;
  }
  release(&bcache.locks[b->blockno % NBUCKET]);

  // 当且仅当没有任何等待该block
  // 此时就把这个block放到LRU侧，给之后的需要缓存的block用
  // if (b->refcnt == 0)
  // {
  //   // no one is waiting for it.
  //   b->next->prev = b->prev;
  //   b->prev->next = b->next;
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }

  // release(&bcache.lock);
}

void bpin(struct buf *b)
{
  acquire(&bcache.locks[b->blockno % NBUCKET]);
  b->refcnt++;
  release(&bcache.locks[b->blockno % NBUCKET]);
}

void bunpin(struct buf *b)
{
  acquire(&bcache.locks[b->blockno % NBUCKET]);
  b->refcnt--;
  release(&bcache.locks[b->blockno % NBUCKET]);
}

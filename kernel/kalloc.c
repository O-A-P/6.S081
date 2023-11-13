// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  // for 初始化所有cpu内存锁
  for (int i = 0; i < NCPU; i++) {
    // 这里第二个就算了，懒得改了
    initlock(&kmem[i].lock, "kmem");
  }
  // initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  // 获取cpuid
  int id = cpuid();
  // 根据cpuid获取锁
  // 感觉没有必要获取锁了这里，又没有并发问题
  // 妈的不对，仔细想想steal的时候会有并发问题。。
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  // 这里需要思考一下，kfree一定能snyx

  push_off();
  int id = cpuid();


  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  else {
    // 如果r已经空了就会很坏很坏
    // 只能偷点
    for(int i = 0; i < NCPU; i++) {
      // 不要重复去搞
      if(id == i) continue;
      acquire(&kmem[i].lock);
      if (kmem[i].freelist) {
        // 如果别人的有货，就偷一片过来
        r = kmem[i].freelist;
        kmem[i].freelist = kmem[i].freelist->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }
  release(&kmem[id].lock);
  pop_off();


  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

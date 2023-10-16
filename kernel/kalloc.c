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
} kmem;

struct mem_ref
{
    struct spinlock lock;
    int cnt;
};

struct mem_ref mem_ref[PHYSTOP/PGSIZE];

int
get_mem_ref(uint64 pa)
{
  acquire(&(mem_ref[pa/PGSIZE].lock));
  int res = mem_ref[(uint64)pa/PGSIZE].cnt;
  release(&(mem_ref[pa/PGSIZE].lock));
  return res;
}

void
kinit()
{
  for(int i = 0; i < PHYSTOP/PGSIZE; ++i)
  {
    // initialize the lock
    initlock(&(mem_ref[i].lock), "kmem_ref");
  }
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    // initialize the count
    mem_ref[(uint64)p/PGSIZE].cnt = 1;
    kfree(p);
  }
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

  // only free pa when memory reference is 0
  uint64 pa_idx = (uint64)pa / PGSIZE;
  acquire(&(mem_ref[pa_idx].lock));

  mem_ref[pa_idx].cnt--;
  if (mem_ref[pa_idx].cnt > 0)
  {
    release(&(mem_ref[pa_idx].lock));
    return;
  }
  release(&(mem_ref[pa_idx].lock));

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    uint64 pa_idx = (uint64)r / PGSIZE;
    acquire(&(mem_ref[pa_idx].lock));
    mem_ref[pa_idx].cnt = 1;
    release(&(mem_ref[pa_idx].lock));

    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

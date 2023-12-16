#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
    int n; // 记录需要写的block数量
    int block
        [LOGSIZE]; // 记录哪些block是需要被重新写入到硬盘的log区，记录其编号
};

struct log {
    struct spinlock lock;
    int start;       // 起始位置
    int size;        // 大小
    int outstanding; // how many FS sys calls are executing.
    int committing;  // in commit(), please wait.
    int dev;
    struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

void initlog(int dev, struct superblock* sb) {
    if(sizeof(struct logheader) >= BSIZE)
        panic("initlog: too big logheader");

    initlock(&log.lock, "log");
    log.start = sb->logstart; // superblock负责记录元数据
    log.size = sb->nlog;
    log.dev = dev;
    recover_from_log(); // 恢复是由CPU来做，而不是之前以为的磁盘自己恢复
}

// Copy committed blocks from log to their home location
static void install_trans(int recovering) {
    int tail;

    for(tail = 0; tail < log.lh.n; tail++) {
        struct buf* lbuf =
            bread(log.dev, log.start + tail + 1);              // read log block
        struct buf* dbuf = bread(log.dev, log.lh.block[tail]); // read dst
        memmove(dbuf->data, lbuf->data, BSIZE); // copy block to dst
        // 在这里才真正把log block写入到真正的位置
        bwrite(dbuf); // write dst to disk
        // 如果不是recovering流程，unpin即可
        if(recovering == 0)
            bunpin(dbuf); // 之前第一次的时候pin过这个buffer
        brelse(lbuf);
        brelse(dbuf);
    }
}

// Read the log header from disk into the in-memory log header
static void read_head(void) {
    // 如果crash了，那只能从disk获取head信息
    struct buf* buf = bread(log.dev, log.start);
    struct logheader* lh = (struct logheader*) (buf->data);
    int i;
    log.lh.n = lh->n;
    for(i = 0; i < log.lh.n; i++) {
        log.lh.block[i] = lh->block[i];
    }
    brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void write_head(void) {
    // 刚刚的write_log并不算真正的完成提交
    // 还需要将log header block从内存写入磁盘
    // header中记录了log需要写入的block数量和具体哪些block
    // 当然在最后面也会起到擦除的作用
    struct buf* buf = bread(log.dev, log.start);
    struct logheader* hb = (struct logheader*) (buf->data);
    int i;
    hb->n = log.lh.n;
    for(i = 0; i < log.lh.n; i++) {
        hb->block[i] = log.lh.block[i];
    }
    bwrite(buf);
    brelse(buf);
}

static void recover_from_log(void) {
    read_head();
    install_trans(1); // if committed, copy from log to disk
    log.lh.n = 0;
    write_head(); // clear the log
}

// 开始操作的函数
// called at the start of each FS system call.
void begin_op(void) {
    acquire(&log.lock);
    while(1) {
        // 如果log这个结构体正在被提交，那就等等
        if(log.committing) {
            sleep(&log, &log.lock);
        } else if(log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
            // log.lh.n也确保了零散的log_write能被成功记录！
            // 这里outstanding记录的是目前已有要写磁盘的系统调用数量
            // 基于的假设是每个系统调用都会使用MAXOPBLOCKS大小的块，反正是预留了
            // 如果剩余空间不够，那就等等，等别人的事务先用完：end_op
            // this op might exhaust log space; wait for commit.
            sleep(&log, &log.lock);
        } else {
            // 相当于为这个系统调用的写blocks预留log空间
            // 不急着写入磁盘
            log.outstanding += 1;
            release(&log.lock);
            break;
        }
    }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
// 这里才是真正写的地方
void end_op(void) {
    int do_commit = 0;

    acquire(&log.lock);
    log.outstanding -= 1;
    if(log.committing)
        panic("log.committing");
    if(log.outstanding == 0) {
        // 以组为单位提交！
        do_commit = 1;
        log.committing = 1;
    } else {
        // begin_op() may be waiting for log space,
        // and decrementing log.outstanding has decreased
        // the amount of reserved space.
        wakeup(&log);
    }
    release(&log.lock);

    if(do_commit) {
        // call commit w/o holding locks, since not allowed
        // to sleep with locks.
        commit();
        acquire(&log.lock);
        log.committing = 0;
        wakeup(&log);
        release(&log.lock);
    }
}

// Copy modified blocks from cache to log.
static void write_log(void) {
    int tail;

    // printf("log.lh.n: %d\n", log.lh.n);
    // 遍历所有标记的需要写入log的blocks的buffer都写入到log buffer里
    for(tail = 0; tail < log.lh.n; tail++) {
        // 从log block cache中获取buffer
        struct buf* to = bread(log.dev, log.start + tail + 1); // log block
        // 从正常的data block cache中获取buffer
        struct buf* from = bread(log.dev, log.lh.block[tail]); // cache block
        memmove(to->data, from->data, BSIZE);
        // 在这里真正把log从内存写入磁盘的log区
        bwrite(to); // write the log
        brelse(from);
        brelse(to);
    }
}

static void commit() {
    if(log.lh.n > 0) {
        write_log();      // Write modified blocks from cache to log
        write_head();     // Write header to disk -- the real commit
        install_trans(0); // Now install writes to home locations
        log.lh.n = 0;
        write_head(); // Erase the transaction from the log
    }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
// 相当于一个代理，不写入磁盘，而是标记需要写的buffer到log里
void log_write(struct buf* b) {
    int i;

    if(log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
        panic("too big a transaction");
    if(log.outstanding < 1)
        panic("log_write outside of trans");

    acquire(&log.lock);
    for(i = 0; i < log.lh.n; i++) {
        // 如果block已经被标记
        // 这时候什么都不用做
        // buffer cache机制保证最后一次写的block在buffer里
        if(log.lh.block[i] == b->blockno) // log absorbtion
            break;
    }
    // 这里完成了标记
    log.lh.block[i] = b->blockno;
    if(i == log.lh.n) { // Add new block to log?
        // 保证该新的block缓冲区不会被释放
        bpin(b);
        log.lh.n++;
    }
    release(&log.lock);
}

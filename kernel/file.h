#pragma once
#include "fs.h"
#include "sleeplock.h"
#include "types.h"
#include "types.h"

struct file {
    enum {
        FD_NONE,
        FD_PIPE, // pipe没必要到硬盘上去，但是可以抽象成file descriptor
        FD_INODE, // 这里的inode只会是目录和文件
        FD_DEVICE
    } type;
    int ref; // reference count
    char readable;
    char writable;
    struct pipe* pipe; // FD_PIPE
    struct inode* ip;  // FD_INODE and FD_DEVICE
    uint off;          // FD_INODE
    short major;       // FD_DEVICE，根据major寻找对应读写函数
};

#define major(dev) ((dev) >> 16 & 0xFFFF)
#define minor(dev) ((dev) &0xFFFF)
#define mkdev(m, n) ((uint) ((m) << 16 | (n)))

// in-memory copy of an inode
// 和block buffer一样的缓存机制
struct inode {
    uint dev;  // Device number
    uint inum; // Inode number
    int ref;   // Reference
             // count，这里是内存中的引用计数，即在内存中引用此文件的指针数量，如果ref=0，则将其在内存中释放（写回到磁盘上）
    struct sleeplock lock; // protects everything below here
    int valid;             // inode has been read from disk?

    short type; // copy of disk inode
    short major;
    short minor;
    short
        nlink; // 统计有多少文件目录引用到此inode以确定什么时候释放（硬盘上释放）
    uint size; // 统计文件大小
    uint addrs[NDIRECT +
        1]; // 这个数组记录data blocks的number，毕竟inode大小是相同的
};

// map major device number to device functions.
struct devsw {
    int (*read)(int, uint64, int);
    int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1

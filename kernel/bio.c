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

//定义桶的数量
#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct spinlock lock;   //全局锁
  struct buf buf[NBUF];
  struct bucket buckets[NBUCKET];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
} bcache;

void
binit(void)
{

  initlock(&bcache.lock, "bcache");
  for(int i = 0; i < NBUCKET; i++)
  {
    initlock(&bcache.buckets[i].lock, "bcache_bucket");
  }

  for(int i = 0; i < NBUCKET; i++)
  {
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  for(int i = 0; i < NBUF; i++)
  {
    struct buf *tmp = &bcache.buf[i];
    //注意这里要进行初始化，因为每个缓存blockno初始化为0，不初始化可能导致后面找不到
    //tmp->blockno = i;
    int n = i % NBUCKET;
    tmp->next = bcache.buckets[n].head.next;
    tmp->prev = &bcache.buckets[n].head;
    initsleeplock(&tmp->lock, "buffer");
    bcache.buckets[n].head.next->prev = tmp;
    bcache.buckets[n].head.next = tmp;
  }

  // Create linked list of buffers
  /*bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }*/
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int n = blockno % NBUCKET;

retry:
  struct buf *vict = 0;
  int t = 0xffffffff;
  acquire(&bcache.buckets[n].lock);

  // Is the block already cached?
  for(b = bcache.buckets[n].head.next; b != &bcache.buckets[n].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->timestamp=ticks;
      release(&bcache.buckets[n].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.buckets[n].lock);
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  acquire(&bcache.lock);
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    if( t > b->timestamp && b->refcnt == 0)
    {
      t = b->timestamp;
      vict = b;
    }
  }
  release(&bcache.lock);
  if(vict == 0) 
  {
    release(&bcache.lock);
    panic("bget: no buffers");
  }

  if(vict){
    // 获取旧桶和新桶的锁，注意顺序避免死锁
    int oldn = vict->blockno % NBUCKET;
    if (oldn < n) {
        acquire(&bcache.buckets[oldn].lock);
        acquire(&bcache.buckets[n].lock);
    } else if (oldn > n) {
        acquire(&bcache.buckets[n].lock);
        acquire(&bcache.buckets[oldn].lock);
    } else {
        acquire(&bcache.buckets[n].lock);
    }

    // 再次检查我们要找的 blockno 是否已经被别人加载了
    for(b = bcache.buckets[n].head.next; b != &bcache.buckets[n].head; b = b->next){
        if(b->dev == dev && b->blockno == blockno){
            // 别人已经加载好了！放弃 vict
            b->refcnt++;
            b->timestamp=ticks;
            // 释放所有锁，返回 b
            if (oldn < n) {
              release(&bcache.buckets[oldn].lock);
              release(&bcache.buckets[n].lock);
            } else if (oldn > n) {
              release(&bcache.buckets[n].lock);
              release(&bcache.buckets[oldn].lock);
            } else {
              release(&bcache.buckets[n].lock);
            }
            //release(&bcache.lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // 2. 【修复2】二次检查：选中的 vict 是否在我们等待锁的期间被其他进程抢走了？
  if (vict->refcnt != 0) {
    // 如果被抢走，释放刚拿到的锁，回到最上面重新走整个流程
    if (oldn < n) {
      release(&bcache.buckets[oldn].lock);
      release(&bcache.buckets[n].lock);
    } else if (oldn > n) {
      release(&bcache.buckets[n].lock);
      release(&bcache.buckets[oldn].lock);
    } else {
      release(&bcache.buckets[n].lock);
    }
    goto retry; 
  }

    // 正式执行迁移
    if(oldn != n){
        // 从旧链表断开
        vict->next->prev = vict->prev;
        vict->prev->next = vict->next;
        // 挂入新链表
        vict->next = bcache.buckets[n].head.next;
        vict->prev = &bcache.buckets[n].head;
        bcache.buckets[n].head.next->prev = vict;
        bcache.buckets[n].head.next = vict;
    }
    vict->timestamp=ticks;
    vict->dev = dev;
    vict->blockno = blockno;
    vict->refcnt = 1;
    vict->valid = 0; // 必须标记为无效，后续 bread 会触发磁盘读取
    // 释放锁并返回 vict
    if (oldn < n) {
      release(&bcache.buckets[oldn].lock);
      release(&bcache.buckets[n].lock);
    } else if (oldn > n) {
      release(&bcache.buckets[n].lock);
      release(&bcache.buckets[oldn].lock);
    } else {
      release(&bcache.buckets[n].lock);
    }
    acquiresleep(&vict->lock);
    return vict;
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.buckets[(b->blockno%NBUCKET)].lock);
  b->refcnt--;
  if(b->refcnt == 0)
  {
    b->timestamp = ticks;
  }
  
  release(&bcache.buckets[(b->blockno%NBUCKET)].lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.buckets[(b->blockno%NBUCKET)].lock);
  b->refcnt++;
  release(&bcache.buckets[(b->blockno%NBUCKET)].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.buckets[(b->blockno%NBUCKET)].lock);
  b->refcnt--;
  release(&bcache.buckets[(b->blockno%NBUCKET)].lock);
}



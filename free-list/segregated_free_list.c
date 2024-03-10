#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "dbg.h"

/* Three lowest bits set for good measure. */
#define TRUE 7
#define FALSE 0

typedef struct BlockHdr BlockHdr;
struct BlockHdr {
  size_t size;    /* The number of bytes in this block. */
  BlockHdr *next; /* Linked list of blocks. */
  int used;       /* Flag if the block is used. */
};

typedef uint64_t word_t;

enum {
  TINY = 1,
  SMALL = 16,
  MID = 32,
  BIG = 64,
  HUGE = 128,
  TINY_IDX = 0,
  SMALL_IDX = 1,
  MID_IDX = 2,
  BIG_IDX = 3,
  HUGE_IDX = 4,
};

static BlockHdr *global_buckets[] = {
    [TINY_IDX] = NULL,  /* >= TINY words */
    [SMALL_IDX] = NULL, /* >= SMALL words */
    [MID_IDX] = NULL,   /* >= MID words */
    [BIG_IDX] = NULL,   /* >= BIG words */
    [HUGE_IDX] = NULL,  /* >= HUGE words */
};

static void *heap_base_addr = NULL;

void reset_heap(void) {
  if (heap_base_addr != NULL) {
    brk(heap_base_addr);
    heap_base_addr = NULL;
    global_buckets[TINY_IDX] = NULL;
    global_buckets[SMALL_IDX] = NULL;
    global_buckets[MID_IDX] = NULL;
    global_buckets[BIG_IDX] = NULL;
    global_buckets[HUGE_IDX] = NULL;
  }
}

BlockHdr *hdr(word_t *ptr) { return (BlockHdr *)(ptr)-1; }

BlockHdr *find_block(size_t size) { return NULL; }

BlockHdr *request_block_from_os(size_t size) {
  BlockHdr *blk = sbrk(0); /* Return current break. */

  /*
   * Safe the start address of the heap before changing
   * it for the first time. This is only needed to allow
   * implementing RESET_HEAP.
   */
  if (heap_base_addr == NULL)
    heap_base_addr = (void *)blk;

  /* Size of the actual allocation that is performed on heap. */
  size_t real_size = sizeof(BlockHdr) + size;
  if (sbrk(real_size) == (void *)-1) {
    return NULL; /* Out of memory. */
  }

  return blk;
}

/* Insert a block into the free list that best fits its size. */
void insert_blk(BlockHdr *blk) {
  int idx = 0;
  size_t words = blk->size / sizeof(word_t);
  if (words >= HUGE) {
    idx = HUGE_IDX;
  } else if (words >= BIG) {
    idx = BIG_IDX;
  } else if (words >= MID) {
    idx = MID_IDX;
  } else if (words >= SMALL) {
    idx = SMALL_IDX;
  } else if (words >= TINY) {
    idx = TINY_IDX;
  }

  blk->next = global_buckets[idx];
  global_buckets[idx] = blk;
}

/* Align the given size to word boundaries. */
size_t align(size_t size) {
  return (size + (sizeof(word_t) - 1)) & ~(sizeof(word_t) - 1);
}

word_t *alloc(ptrdiff_t ssize) {
  if (ssize <= 0)
    return NULL;

  /*
   * From now on, we use an unsigned size to be
   * compatible with standard C functions.
   */
  size_t size = (size_t)ssize;
  size = align(size);

  BlockHdr *blk = NULL;
  if ((blk = find_block(size)) != NULL) {
    return (word_t *)(blk + 1);
  } else {
    blk = request_block_from_os(size);
    blk->size = size;
    blk->used = TRUE;
    insert_blk(blk);
    return (word_t *)(blk + 1);
  }
}

void wfree(word_t *ptr) {
  if (ptr == NULL)
    return;

  hdr(ptr)->used = FALSE;
}

int main(void) {
  dbg("TEST: Alignment\n");
  assert(align(0) == 0);
  assert(align(1) == 8);
  assert(align(7) == 8);
  assert(align(43) == 48);

  {
    reset_heap();
    dbg("TEST: Allocating\n");
    assert(alloc(0) == NULL);
    assert(alloc(-1) == NULL);
    /* Make a tiny allocation. */
    word_t *a1 = alloc(8);
    assert(hdr(a1)->size == 8);
    assert(global_buckets[TINY_IDX] == hdr(a1));
    /* Make a small allocation. */
    word_t *a2 = alloc(125);
    assert(hdr(a2)->size == 128);
    assert(global_buckets[SMALL_IDX] == hdr(a2));
    /* Make a huge allocation. */
    word_t *a3 = alloc(sizeof(word_t) * HUGE);
    assert(hdr(a3)->size == sizeof(word_t) * HUGE);
    assert(global_buckets[HUGE_IDX] == hdr(a3));
    /* Make another allocation in a non-empty bucket. */
    word_t *a4 = alloc(8);
    assert(hdr(a4)->size == 8);
    assert(global_buckets[TINY_IDX] == hdr(a4));
    assert(global_buckets[TINY_IDX]->next == hdr(a1));
  }

  {
    reset_heap();
    dbg("TEST: Freeing\n");
    /* Freeing an NULL should be OK. */
    wfree(NULL);
    wfree(alloc(0));

    word_t *a1 = alloc(8);
    assert(hdr(a1)->used == TRUE);
    assert(hdr(a1)->size == 8);
    assert(global_buckets[TINY_IDX] == hdr(a1));

    wfree(a1);
    assert(hdr(a1)->used == FALSE);
    assert(hdr(a1)->size == 8);
    assert(global_buckets[TINY_IDX] == hdr(a1));

    word_t *a2 = alloc(256);
    assert(hdr(a2)->used == TRUE);
    assert(hdr(a2)->size == 256);
    assert(global_buckets[MID_IDX] == hdr(a2));

    wfree(a2);
    assert(hdr(a2)->used == FALSE);
    assert(hdr(a2)->size == 256);
    assert(global_buckets[MID_IDX] == hdr(a2));
  }

  return 0;
}

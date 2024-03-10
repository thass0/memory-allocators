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
    [TINY_IDX] = NULL,  /* >= TINY # of words */
    [SMALL_IDX] = NULL, /* >= SMALL # of words */
    [MID_IDX] = NULL,   /* >= MID # of words */
    [BIG_IDX] = NULL,   /* >= BIG # of words */
    [HUGE_IDX] = NULL,  /* >= HUGE # of words */
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

/*
 * Return the index of the bucket that stores blocks
 * that have a size greater or equal to SIZE.
 */
int bucket_idx(size_t size) {
  int idx = 0;
  size_t words = size / sizeof(word_t);
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
  return idx;
}

BlockHdr *find_block(size_t size) {
  int idx = bucket_idx(size);
  BlockHdr *blk = global_buckets[idx];
  BlockHdr *best = NULL;

  while (blk != NULL) {
    if (blk->used == FALSE && blk->size >= size) {
      if (best == NULL || blk->size < best->size) {
        best = blk;
      }
    }
    blk = blk->next;
  }

  return best;
}

BlockHdr *request_block_from_os(size_t size) {
  BlockHdr *blk = sbrk(0); /* Returns current break. */

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
    blk->used = TRUE;
    return (word_t *)(blk + 1);
  } else {
    blk = request_block_from_os(size);
    blk->size = size;
    blk->used = TRUE;

    /* Insert the block into the right bucket. */
    int idx = bucket_idx(blk->size);
    blk->next = global_buckets[idx];
    global_buckets[idx] = blk;

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

  {
    reset_heap();
    dbg("TEST: Re-using block\n");
    /* Re-use the same block for a1 and a2. */
    word_t *a1 = alloc(64);
    wfree(a1);
    assert(hdr(a1)->used == FALSE);
    assert(global_buckets[TINY_IDX] == hdr(a1));
    word_t *a2 = alloc(64);
    assert(hdr(a1) == hdr(a2));
    assert(hdr(a1)->used == TRUE);
    wfree(a2);

    /* Allocate a new block for an allocation larger than a1. */
    word_t *a3 = alloc(65);
    assert(hdr(a3) != hdr(a1));
    assert(hdr(a3)->size == 72);
    assert(hdr(a3)->used == TRUE);
    assert(global_buckets[TINY_IDX] == hdr(a3));
    assert(global_buckets[TINY_IDX]->next == hdr(a1));
    wfree(a3);

    /* Re-use the smaller of the two free blocks in the TINY bucket. */
    word_t *a4 = alloc(64);
    assert(hdr(a4) == hdr(a1));
    assert(hdr(a4)->used == TRUE);
    assert(hdr(a4)->size == 64);
    assert(global_buckets[TINY_IDX] == hdr(a3));
    assert(global_buckets[TINY_IDX]->next == hdr(a4));

    /*
     * Create a free block in the SMALL bucket. For the subsequent SMALL
     * allocations, that block should be re-used.
     */
    word_t *a5 = alloc(128);
    assert(global_buckets[SMALL_IDX] == hdr(a5));
    assert(hdr(a5)->used == TRUE);
    assert(hdr(a5)->size == 128);
    wfree(a5);
    word_t *a6 = alloc(128);
    assert(hdr(a6) == hdr(a5));
    assert(hdr(a6)->used == TRUE);
    assert(hdr(a6)->size == 128);
    assert(global_buckets[SMALL_IDX] == hdr(a6));
    wfree(a6);

    /*
     * Now there are free blocks in two buckets, TINY and SMALL.
     * If we allocate another block, the smallest possible bucket
     * should be picked.
     */
    assert(global_buckets[TINY_IDX]->used == FALSE);
    word_t *a7 = alloc(65);
    assert(hdr(a7) == hdr(a3));
    assert(hdr(a7)->used == TRUE);
    assert(hdr(a7)->size == 72);
    assert(global_buckets[TINY_IDX] == hdr(a7));
    wfree(a7);
  }

  return 0;
}

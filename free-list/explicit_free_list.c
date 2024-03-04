/*
 * A heap allocator based on an explicit free list of all free blocks.
 *
 * Thassilo Schulze, 03/03/2024
 */

#include <assert.h> /* assert */
#include <stddef.h> /* ptrdiff_t, size_t, NULL */
#include <stdint.h> /* intptr_t */
#include <string.h> /* memcpy */
#include <unistd.h> /* sbrk */

typedef intptr_t word_t;

typedef struct BlockHdr BlockHdr;
struct BlockHdr {
  ptrdiff_t size; /* Size of the allocation in bytes. */
  BlockHdr *next;
  BlockHdr *prev;
  /*
   * NEXT points in the direction of the block that was added
   * most recently. This means, "it points towards GLOBAL_FREE_LIST"
   * (so to speak) since that's where blocks are added. If a block
   * is the first one in the list, NEXT is NULL. If it's the last
   * one, PREV is NULL.
   *
   * To illustrate:
   *
   *   prev   next     prev   next     prev   next
   * +------+------+ +------+------+ +------+------+
   * | NULL |      | |      |      | |      | NULL |  <- GLOBAL_FREE_LIST
   * +------+------+ +------+------+ +------+------+
   *            ^-------^       ^-------^
   */
};

/* Doubly linked list of unused blocks. */
static BlockHdr *global_free_list = NULL;
/* The first block on the heap. */
static BlockHdr *init = NULL;

void reset_heap(void) {
  if (init != NULL)
    brk(init);
  init = NULL;
  global_free_list = NULL;
}

/* Return a pointer to the memory allocated for the given block header. */
word_t *user_mem(BlockHdr *blk) {
  /* Return the first address after the header. */
  return (word_t *)(blk + 1);
}

/* Return a pointer to the block header of the given memory pointer. */
BlockHdr *mem_hdr(word_t *mem) { return ((BlockHdr *)mem) - 1; }

/* Add a block to the front of a list. */
void add_block(BlockHdr *blk, BlockHdr **list) {
  assert(blk != NULL);
  assert(list != NULL);

  if (*list == NULL) {
    /*
     * This block will be the only one in the list.
     * Make it point to nothing else.
     */
    blk->prev = NULL;
  } else {
    /*
     * Link this block which will become the new start of
     * the list and the previous start.
     */
    (*list)->next = blk;
    blk->prev = *list;
  }

  /* Make this block the start of the list. */
  blk->next = NULL;
  *list = blk;
}

/* Remove a block from a list. */
void remove_block(BlockHdr *blk, BlockHdr **list) {
  assert(blk != NULL);
  assert(list != NULL);

  /*
   * Update the list pointer if the block to remove
   * is at the start of the list.
   */
  if (*list == blk)
    *list = blk->prev;

  /* Link the blocks on the two sides of this block to each other. */
  BlockHdr *next = blk->next;
  BlockHdr *prev = blk->prev;
  if (next != NULL)
    next->prev = prev;
  if (prev != NULL)
    prev->next = next;
}

/*
 * Find a free block that is large enough for an allocation
 * of SIZE bytes. Use the best fit method: the smallest block
 * that has enough bytes is returned. If there is no such block,
 * NULL is returned.
 */
BlockHdr *find_block(ptrdiff_t size) {
  BlockHdr *blk = global_free_list;
  BlockHdr *best_blk = NULL;

  while (blk != NULL) {
    if (blk->size == size) {
      return blk;
    } else if (blk->size > size) {
      if (best_blk == NULL || blk->size < best_blk->size) {
        best_blk = blk;
      }
    }
    blk = blk->prev;
  }

  return best_blk;
}

/*
 * Split a block in two parts so that the first half
 * contains SIZE bytes. If splitting is not possible
 * because BLK is not big enough to hold another full
 * block, nothing happens and BLK stays untouched.
 */
void split_block(BlockHdr *blk, ptrdiff_t size) {
  assert(blk != NULL);
  assert(blk->size >= size);

  ptrdiff_t real_size = sizeof(BlockHdr) + size;
  if ((size_t)blk->size < real_size + sizeof(word_t))
    return;

  BlockHdr *rem = (BlockHdr *)(((ptrdiff_t)blk) + real_size);
  rem->size = blk->size - real_size;
  blk->size = size;

  /*
   * This way around, we don't need to change anything
   * if BLK is the start of a list.
   */
  rem->prev = blk->prev;
  blk->prev = rem;
  rem->next = blk;
}

/*
 * Request memory from the OS to allocate SIZE bytes plus
 * the bytes that are occupied by the block metadata.
 * Return that memory or NULL to signal "Out of memory".
 */
BlockHdr *request_block_from_os(ptrdiff_t size) {
  assert((size_t)size >= sizeof(word_t));

  /* We need to allocate memory for the block's header and its content. */
  ptrdiff_t real_size = sizeof(BlockHdr) + size;

  BlockHdr *blk = sbrk(0);

  if (init == NULL)
    init = blk;

  if (sbrk(real_size) == (void *)-1) {
    return NULL; /* Out of memory. */
  } else {
    return blk;
  }
}

/* Align the given size by rounding it up to the nearest word boundary. */
ptrdiff_t align(ptrdiff_t size) {
  return (size + (sizeof(word_t) - 1)) & ~(sizeof(word_t) - 1);
}

/*
 * Allocate a contiguous block of heap memory that has
 * a size of at least SIZE bytes.
 * Return NULL if the size is less than or equal to 0
 * or if the allocation has failed.
 */
word_t *alloc(ptrdiff_t size) {
  if (size <= 0)
    return NULL;

  size = align(size);

  /*
   * Either we find and re-use a block that has already
   * been allocated or we request new memory from the OS.
   */
  BlockHdr *blk = NULL;
  if ((blk = find_block(size)) != NULL) {
    split_block(blk, size);
    remove_block(blk, &global_free_list);
    return user_mem(blk);
  } else {
    blk = request_block_from_os(size);
    blk->size = size;
    /* Links point nowhere while the block is used. */
    blk->prev = NULL;
    blk->next = NULL;
    return user_mem(blk);
  }

  return NULL;
}

/*
 * Check if BLKB is adjacent in memory to BLKA.
 * I.e., BLKA comes and is followed by BLKB.
 */
int is_adjacent(BlockHdr *blka, BlockHdr *blkb) {
  assert(blka != NULL);
  assert(blkb != NULL);
  return ((ptrdiff_t)blka) + sizeof(BlockHdr) + blka->size == (size_t)blkb;
}

/* Merge a block into adjacent blocks. */
void merge_block(BlockHdr *blk, BlockHdr **list) {
  assert(blk != NULL);

  /*
   * We don't need to consider BLK->NEXT, since that's always
   * NULL here. Merges occur right after adding the given block
   * to the free list. This means that this block is always the
   * first one (and thus BLK->NEXT == NULL).
   */

  if (blk->prev != NULL && is_adjacent(blk, blk->prev)) {
    blk->size += sizeof(BlockHdr) + blk->prev->size;
    remove_block(blk->prev, list);
  }

  if (blk->prev != NULL && is_adjacent(blk->prev, blk)) {
    blk->prev->size += sizeof(BlockHdr) + blk->size;
    remove_block(blk, list);
  }
}

/* Free a pointer to some words of memory. "wfree" <=> "word free". */
void wfree(word_t *mem) {
  if (mem == NULL)
    return;

  BlockHdr *blk = mem_hdr(mem);
  add_block(blk, &global_free_list);
  merge_block(blk, &global_free_list);
}

void *malloc(size_t size) { return alloc(size); }

void free(void *mem) { return wfree(mem); }

void *realloc(void *mem, size_t size) {
  if (mem == NULL)
    return malloc(size);

  BlockHdr *blk = mem_hdr(mem);
  if ((size_t)blk->size >= size) {
    return mem;
  } else {
    void *new_mem = malloc(size);
    if (new_mem == NULL)
      return NULL;
    memcpy(new_mem, mem, blk->size);
    free(mem);
    return new_mem;
  }
}

void *calloc(size_t n, size_t size) {
  /*
   * If N and SIZE are suitably small, skip the division-based
   * overflow test (for speed). Assuming the minimum width of
   * size_t is 32 bits, two factors that are both less than 2^16
   * will never overflow. If that cannot be guaranteed, we used
   * division to check if the product would overflow.
   * See https://drj11.wordpress.com/2008/06/04/calloc-when-multiply-overflows/
   */
  if ((n > 65535 || size > 65535) && (size_t)-1 / n < size)
    return NULL;

  void *mem = malloc(size * n);
  if (mem == NULL)
    return mem;

  memset(mem, 0, size);
  return mem;
}

/* Helper to print without doing formatting on the heap. */
void print(char *s) {
  assert(s != NULL);
  write(STDOUT_FILENO, s, strlen(s));
}

int main(void) {
  print("TEST: Aligning allocations\n");
  assert(align(0) == 0);
  assert(align(1) == 8);
  assert(align(3) == 8);
  assert(align(6) == 8);
  assert(align(8) == 8);
  assert(align(9) == 16);
  assert(align(15) == 16);
  assert(align(16) == 16);
  assert(align(121) == 128);

  print("TEST: Allocating blocks\n");
  word_t *a1 = alloc(1);
  assert(mem_hdr(a1)->size == 8);
  assert(mem_hdr(a1)->next == NULL);
  word_t *a2 = alloc(3);
  assert(mem_hdr(a2)->size == 8);
  word_t *a3 = alloc(14);
  assert(mem_hdr(a3)->size == 16);

  print("TEST: Freeing blocks\n");
  wfree(alloc(0));

  wfree(a1);
  assert(global_free_list == mem_hdr(a1));

  wfree(a3);
  assert(global_free_list == mem_hdr(a3));
  assert(mem_hdr(a3)->next == NULL);
  assert(mem_hdr(a3)->prev == mem_hdr(a1));
  assert(mem_hdr(a1)->next == mem_hdr(a3));
  assert(mem_hdr(a1)->prev == NULL);

  print("TEST: Re-using blocks\n");
  word_t *a4 = alloc(8);
  assert(mem_hdr(a4) == mem_hdr(a1));

  reset_heap();
  print("TEST: Splitting blocks\n");
  word_t *a5 = alloc(2 * 64 + sizeof(BlockHdr));
  wfree(a5);
  /* a5 should be re-used and split twice. */
  word_t *a6 = alloc(64);
  assert(mem_hdr(a5) == mem_hdr(a6));
  assert(mem_hdr(a6)->size == 64);
  word_t *a7 = alloc(64);
  assert(mem_hdr(a6)->size == 64);
  assert(((ptrdiff_t)a5) + 64 == (ptrdiff_t)mem_hdr(a7));

  reset_heap();
  print("TEST: Merging blocks\n");
  /*
   * No matter the order, allocations that live next
   * to each other on heap should be merged on free.
   */
  word_t *m1 = alloc(8);
  word_t *m2 = alloc(8);
  wfree(m2);
  wfree(m1);
  assert(mem_hdr(m1)->size == 16 + sizeof(BlockHdr));
  assert(global_free_list == mem_hdr(m1)); /* m1 is first in the free list. */
  assert(mem_hdr(m1)->prev ==
         NULL); /* m1 is the only block in the free list. */

  alloc(8); /* Block merges */
  /*
   * From the free block m1, the first 8 bytes have been allocated.
   * The rest remains in the free list.
   */
  assert((size_t)global_free_list ==
         (size_t)mem_hdr(m1) + 8 + sizeof(BlockHdr));
  alloc(8); /* Use up all free blocks. */
  assert(global_free_list == NULL);

  reset_heap();
  m1 = alloc(8);
  m2 = alloc(8);
  wfree(m1);
  assert(global_free_list == mem_hdr(m1));
  wfree(m2);
  assert(global_free_list == mem_hdr(m1));
  assert(mem_hdr(m1)->prev == NULL);
  assert(mem_hdr(m1)->size == 16 + sizeof(BlockHdr));

  /*
   * An allocation of 64 bytes doen't fit the one free block
   * we have now (which is 16 bytes + sizeof(BlockHdr) bytes
   * large). So, an entirely new block is requested (of size
   * 32). When this 32 byte block is free'd,it's memory is
   * again merged into m1.
   */
  word_t *m3 = alloc(64);
  assert(mem_hdr(m3)->size == 64);
  /* m3 is new memory, so m1 stays in the free list. */
  assert(global_free_list == mem_hdr(m1));
  assert(mem_hdr(m1)->size == 16 + sizeof(BlockHdr));
  wfree(m3);
  assert(mem_hdr(m1)->size == 64 + 16 + 2 * sizeof(BlockHdr));

  reset_heap();
  /*
   * Add a 8 bytes + 8 bytes + sizeof(BlockHdr) to the heap
   * by adding and freeing to blocks of size 8 bytes. Since
   * they are merged, the result is a block larger than 8 bytes.
   * (This slight increase in the size of merged blocks should
   * always be kept in mind!)
   */
  word_t *m4 = alloc(8);
  word_t *m5 = alloc(8);
  wfree(m4);
  wfree(m5);
  word_t *m6 = alloc(16 + sizeof(BlockHdr));
  assert(mem_hdr(m6)->size == 16 + sizeof(BlockHdr));
  assert(global_free_list == NULL);

  return 0;
}


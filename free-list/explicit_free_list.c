/*
 * A heap allocator based on an explicit free list of all free blocks.
 *
 * Thassilo Schulze, 03/03/2024
 */

#include <assert.h> /* assert */
#include <stddef.h> /* ptrdiff_t */
#include <stdint.h> /* intptr_t */
#include <string.h> /* memcpy */
#include <unistd.h> /* sbrk */

#include <stdio.h> /* Tests only */

typedef intptr_t word_t;

typedef struct BlockHdr BlockHdr;
struct BlockHdr {
  ptrdiff_t size; /* Size of the allocation in bytes. */
  BlockHdr *next; /* Pointer to the next block header. */
  BlockHdr *prev; /* Pointer to the previous block header. */
};

/* Doubly linked list of unused blocks. */
static BlockHdr *global_free_list = NULL;

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
  if (blk->size < real_size + sizeof(word_t))
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

/* Free a pointer to some words of memory. "wfree" <=> "word free". */
void wfree(word_t *mem) {
  if (mem == NULL)
    return;

  BlockHdr *blk = mem_hdr(mem);
  
  add_block(mem_hdr(mem), &global_free_list);
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

int main(void) {
  printf("TEST: Aligning works\n");
  assert(align(0) == 0);
  assert(align(1) == 8);
  assert(align(3) == 8);
  assert(align(6) == 8);
  assert(align(8) == 8);
  assert(align(9) == 16);
  assert(align(15) == 16);
  assert(align(16) == 16);
  assert(align(121) == 128);

  printf("TEST: Allocating blocks works\n");
  word_t *a1 = alloc(1);
  assert(mem_hdr(a1)->size == 8);
  assert(mem_hdr(a1)->next == NULL);
  word_t *a2 = alloc(3);
  assert(mem_hdr(a2)->size == 8);
  word_t *a3 = alloc(14);
  assert(mem_hdr(a3)->size == 16);

  printf("TEST: Freeing works\n");
  wfree(alloc(0));

  wfree(a1);
  assert(global_free_list == mem_hdr(a1));

  wfree(a3);
  assert(global_free_list == mem_hdr(a3));
  assert(mem_hdr(a3)->next == NULL);
  assert(mem_hdr(a3)->prev == mem_hdr(a1));
  assert(mem_hdr(a1)->next == mem_hdr(a3));
  assert(mem_hdr(a1)->prev == NULL);

  printf("TEST: Re-using blocks works\n");
  word_t *a4 = alloc(8);
  assert(mem_hdr(a4) == mem_hdr(a1));

  printf("TEST: Splitting blocks works\n");
  word_t *a5 = alloc(256);
  wfree(a5);
  /* a5 should be re-used and split twice. */
  word_t *a6 = alloc(64);
  assert(mem_hdr(a5) == mem_hdr(a6));
  assert(mem_hdr(a6)->size == 64);
  word_t *a7 = alloc(64);
  assert(mem_hdr(a6)->size == 64);
  assert(((ptrdiff_t)a5) + 64 == (ptrdiff_t)mem_hdr(a7));

  return 0;
}

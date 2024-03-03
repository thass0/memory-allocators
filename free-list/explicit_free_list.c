/*
 * A heap allocator based on an explicit free list of all free blocks.
 *
 * Thassilo Schulze, 03/03/2024
 */

#include <assert.h> /* assert */
#include <stddef.h> /* ptrdiff_t */
#include <stdint.h> /* intptr_t */
#include <unistd.h> /* sbrk */

#include <stdio.h> /* Tests only */

typedef intptr_t word_t;

typedef struct BlockHdr BlockHdr;
struct BlockHdr {
  ptrdiff_t size; /* Size of the allocation in bytes. */
  BlockHdr *next; /* Pointer to the next block header. */
  BlockHdr *prev; /* Pointer to the previous block header. */
};

typedef struct {
  BlockHdr *used; /* Doubly linked list of used blocks. */
  BlockHdr *free; /* Doubly linked list of unused block. */
} Heap;

static Heap global_heap = {0};

/* Return a pointer to the memory allocated for the given block header. */
word_t *user_mem(BlockHdr *blk) {
  /* Return the first address after the header. */
  return (word_t *)(blk + 1);
}

/* Return a pointer to the block header of the given memory pointer. */
BlockHdr *mem_hdr(word_t *mem) { return ((BlockHdr *)mem) - 1; }

/*
 * Find a free block that is large enough for an allocation
 * of SIZE bytes. Use the best fit method: the smallest block
 * that has enough bytes is returned. If there is no such block,
 * NULL is returned.
 */
BlockHdr *find_block(ptrdiff_t size) {
  BlockHdr *blk = global_heap.free;
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
 * Request memory from the OS to allocate SIZE bytes plus
 * the bytes that are occupied by the block metadata.
 * Return that memory or NULL to signal "Out of memory".
 */
BlockHdr *request_block_from_os(ptrdiff_t size) {
  assert(size >= sizeof(word_t));

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
  if (*list == blk) {
    *list = blk->prev;
  }

  /* Link the blocks on the two sides of this block to each other. */
  BlockHdr *next = blk->next;
  BlockHdr *prev = blk->prev;
  if (next != NULL) {
    next->prev = prev;
  }
  if (prev != NULL) {
    prev->next = next;
  }
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
    remove_block(blk, &global_heap.free);
    add_block(blk, &global_heap.used);
    return user_mem(blk);
  } else {
    blk = request_block_from_os(size);
    blk->size = size;
    add_block(blk, &global_heap.used);
    return user_mem(blk);
  }

  return NULL;
}

void free_(word_t *mem) {
  if (mem == NULL)
    return;

  BlockHdr *blk = mem_hdr(mem);
  remove_block(blk, &global_heap.used);
  add_block(blk, &global_heap.free);
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
  assert(global_heap.used == mem_hdr(a1));
  word_t *a2 = alloc(3);
  assert(mem_hdr(a2)->size == 8);
  word_t *a3 = alloc(14);
  assert(mem_hdr(a3)->size == 16);
  assert(global_heap.used == mem_hdr(a3));
  assert(mem_hdr(a3)->next == NULL);
  assert(mem_hdr(a3)->prev == mem_hdr(a2));
  assert(mem_hdr(a2)->next == mem_hdr(a3));
  assert(mem_hdr(a2)->prev == mem_hdr(a1));
  assert(mem_hdr(a1)->next == mem_hdr(a2));
  assert(mem_hdr(a1)->prev == NULL);

  printf("TEST: Freeing works\n");
  free_(alloc(0));

  free_(a1);
  assert(global_heap.free == mem_hdr(a1));
  /* a1 shouldn't be part of the used list anymore. */
  assert(global_heap.used == mem_hdr(a3));
  assert(mem_hdr(a3)->next == NULL);
  assert(mem_hdr(a3)->prev == mem_hdr(a2));
  assert(mem_hdr(a2)->next == mem_hdr(a3));
  assert(mem_hdr(a2)->prev == NULL);

  free_(a3);
  assert(global_heap.free == mem_hdr(a3));
  assert(mem_hdr(a3)->next == NULL);
  assert(mem_hdr(a3)->prev == mem_hdr(a1));
  assert(mem_hdr(a1)->next == mem_hdr(a3));
  assert(mem_hdr(a1)->prev == NULL);
  /* a3 shouldn't be part of the used list anymore. */
  assert(global_heap.used == mem_hdr(a2));
  assert(mem_hdr(a2)->next == NULL);
  assert(mem_hdr(a2)->prev == NULL);

  printf("TEST: Re-using blocks works\n");
  word_t *a4 = alloc(8);
  assert(mem_hdr(a4) == mem_hdr(a1));
  assert(global_heap.used == mem_hdr(a4));
  assert(mem_hdr(a4)->next == NULL);
  assert(mem_hdr(a4)->prev == mem_hdr(a2));
  assert(mem_hdr(a2)->next == mem_hdr(a4));
  assert(mem_hdr(a2)->prev == NULL);
  assert(global_heap.free == mem_hdr(a3));
  assert(mem_hdr(a3)->next == NULL);
  assert(mem_hdr(a3)->prev == NULL);

  return 0;
}

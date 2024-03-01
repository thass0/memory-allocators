/*
 * A free-list heap allocator that uses sbrk to allocate memory.
 * Based on http://dmitrysoshnikov.com/compilers/writing-a-memory-allocator/
 *
 * Thassilo Schulze, 03/01/2024
 */

#include <stddef.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* A boolean. */
typedef enum { true = 7, false = 0 } bool;

/* A single word; The smallest unit the allocator works with. */
typedef uint64_t word_t;

typedef struct Block Block;

struct Block {
  /* Block object header. */
  long size;
  bool used;
  Block *next;

  /*
   * User data. This is the initial word in that data.
   * It continues at the end of this struct with a total
   * of SIZE bytes.
   */
  word_t data;
};

/*
 * The first node in the free list. This is where
 * the search for free nodes starts in FIRST_FIT.
 */
static Block *free_list_start = NULL;

/*
 * The top node in the free list. This is where
 * new allocations are added.
 */
static Block *free_list_top = NULL;

/*
 * Implementation of FIND_BLOCK using the "first fit" method.
 */
Block *first_fit(ptrdiff_t size) {
  Block *blk = free_list_start;

  while (blk != NULL) {
    if (blk->used || blk->size < size) {
      blk = blk->next;
      continue;
    }

    return blk;
  }

  return NULL;
}

/*
 * Find a block of allocated by unused memory.
 * The block must have at least a size of SIZE bytes.
 * Return NULL if there is no such block.
 */
Block *find_block(ptrdiff_t size) {
  return first_fit(size);
}

/*
 * Calculate the total number of bytes to request for
 * an allocation that gives the user SIZE bytes.
 */
ptrdiff_t alloc_size(ptrdiff_t size) {
  /*
   * Add the number of bytes needed for the block's meta
   * data to the number of bytes that the user receives.
   */
  return size + sizeof(Block) - sizeof(word_t);
}

/* Allocate a new block by requesting more heap memory from the OS */
Block *request_block(ptrdiff_t size) {
  /* Pointer to the start of this new block. */
  Block *blk = sbrk(0);

  /*
   * Bump the program break pointer so that there
   * is enough memory from the new block.
   */
  ptrdiff_t bytes_needed = alloc_size(size);
  if (sbrk(bytes_needed) == (void*) -1) {
    return NULL;
  }

  return blk;
}

/* Round up the size to the next multiple of the word size. */
ptrdiff_t align(ptrdiff_t size) {
  return (size + sizeof(word_t) - 1) & ~(sizeof(word_t) - 1);
}

/*
 * Allocate SIZE bytes in a word aligned, contiguous buffer.
 * Return NULL if
 *  (a) SIZE is less than or equal to 0;
 *  (b) the program is out of memory (OOM).
 */
word_t *alloc(ptrdiff_t size) {
  if (size <= 0) {
    return NULL;
  }

  /* Align the size to the machine word */
  size = align(size);

  Block *blk;
  if ((blk = find_block(size)) != NULL) {
    blk->used = true;
    return &blk->data;
  } else {
    blk = request_block(size);
    if (blk == NULL) {
      return NULL;
    }

    /* Initialize the new block. */
    blk->size = size;
    blk->used = true;
    blk->next = NULL;

    /* Initialize the heap if this is the first call. */
    if (free_list_start == NULL) {
      free_list_start = blk;
    }

    /*
     * Update the free list: make the current top
     * point to the new block and make the new block
     * the top of the list.
     */
    if (free_list_top != NULL) {
      free_list_top->next = blk;
    }

    free_list_top = blk;

    /* Give the user their memory. */
    return &blk->data;
  }
}

/*
 * Return the header of an allocation. DATA must be a
 * pointer that was returned by ALLOC.
 */
Block *block_header(word_t *data) {
  size_t hdr_size = sizeof(Block) - sizeof(word_t);
  char *p = ((char *) data) - hdr_size;
  return (Block *) p;
}

/* Free memory that was allocated by ALLOC. */
void free_(word_t *data) {
  Block *blk = block_header(data);
  blk->used = false;
}

int main(void) {
  /* An allocation of three bytes is align to the 8 byte minimum. */
  word_t *p1 = alloc(3);
  Block *p1_blk = block_header(p1);
  assert(p1_blk->size == sizeof(word_t));

  /* An allocation of 8 bytes stays at 8 bytes. */
  word_t *p2 = alloc(8);
  Block *p2_blk = block_header(p2);
  assert(p2_blk->size == sizeof(word_t));

  /* An allocation should be free-able. */
  free_(p2);
  assert(p2_blk->used == false);

  /* If free'd, the same block will be returned again. */
  word_t *p3 = alloc(5);
  Block *p3_blk = block_header(p3);
  assert(p3_blk == p2_blk);
  assert(p3 == p2);

  printf("All assertions passed\n");
} 

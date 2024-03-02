/*
 * A free-list heap allocator that uses sbrk to allocate memory.
 * Based on http://dmitrysoshnikov.com/compilers/writing-a-memory-allocator/
 *
 * Thassilo Schulze, 03/01/2024 - 03/02/2024
 */

#include <stddef.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>

/* For tests. */
#include <stdio.h>

#define FIRST_FIT 0
#define NEXT_FIT 1
#define BEST_FIT 2

#ifndef SEARCH_MODE
#define SEARCH_MODE NEXT_FIT
#endif

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

#if SEARCH_MODE == NEXT_FIT
/*
 * The last block that was successfully found by NEXT_FIT.
 * It's the starting point of the next search.
 */
static Block *next_fit_start = NULL;
#endif


/***********************/
/* Finding free blocks */
/***********************/

#if SEARCH_MODE == FIRST_FIT
/* Implementation of FIND_BLOCK using the "first fit" algorithm. */
Block *first_fit(ptrdiff_t size) {
  Block *blk = free_list_start;

  while (blk != NULL) {
    if (blk->used || blk->size < size) {
      blk = blk->next;
    } else {
      return blk;
    }
  }

  return NULL;
}
#endif

#if SEARCH_MODE == NEXT_FIT
/* Implementation of FIND_BLOCK using the "next fit" algorithm. */
Block *next_fit(ptrdiff_t size) {
  Block *blk = next_fit_start;

  while (blk != NULL) {
    if (blk->used || blk->size < size) {
      if (blk->next == NULL) {
	/* At the end of the free list, wrap around to the start. */
	blk = free_list_start;
      } else {
	blk = blk->next;
      }

      if (blk == next_fit_start) {
	/* Stop after one full loop. */
	return NULL;
      }
    } else {
      /*
       * The next time this function is called,
       * start at the block that is now returned.
       */
      next_fit_start = blk;
      return blk;
    }
  }

  /* In case NEXT_FIT_START is NULL: */
  return NULL;
}
#endif

#if SEARCH_MODE == BEST_FIT
/* Implementation of FIND_BLOCK using the "best fit" algorithm. */
Block *best_fit(ptrdiff_t size) {
  /* The free block that fits the size best. */
  Block *best_block = NULL;

  for (Block *blk = free_list_start; blk != NULL; blk = blk->next) {
    if (blk->used == false) {
      if (blk->size == size) {
	/* Cannot find a better fit. */
	return blk;
      } else if (blk->size > size) {
	if (best_block == NULL || blk->size < best_block->size) {
	  /*
	   * If this block fits the size better than the
	   * best fit so far, update the best fit.
	   */
	  best_block = blk;
	}
      }
    }
  }

  return best_block;
}
#endif

/*
 * Find a block of allocated by unused memory.
 * The block must have at least a size of SIZE bytes.
 * Return NULL if there is no such block.
 */
Block *find_block(ptrdiff_t size) {
  #if SEARCH_MODE == FIRST_FIT
  return first_fit(size);
  #elif SEARCH_MODE == NEXT_FIT
  return next_fit(size);
  #else
  return best_fit(size);
  #endif
}


/*************************************/
/* Allocating new memory from the OS */
/*************************************/

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


/***************************************/
/* Allocation and free implementations */
/***************************************/

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
      #if SEARCH_MODE == NEXT_FIT
      next_fit_start = blk;
      #endif
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


/*********/
/* Tests */
/*********/

int main(void) {
  printf("Test alloc and free\n");
  /* Align an allocation of 3 bytes to the 8 byte minimum. */
  word_t *p1 = alloc(3);
  Block *p1_blk = block_header(p1);
  assert(p1_blk->size == 8);

  /* Don't change the size of allocations that happen to be aligned. */
  word_t *p2 = alloc(8);
  Block *p2_blk = block_header(p2);
  assert(p2_blk->size == 8);

  /* Free the last allocation. */
  free_(p2);
  assert(p2_blk->used == false);

  /* Reuse the last free'd allocation. */
  word_t *p3 = alloc(5);
  assert(p3 == p2);

  #if SEARCH_MODE == NEXT_FIT
  printf("Test next fit\n");
  alloc(8);
  alloc(8);
  alloc(8);
  word_t *o1 = alloc(16);
  word_t *o2 = alloc(16);
  free_(o1);
  free_(o2);
  word_t *o3 = alloc(16);
  assert(next_fit_start == block_header(o3));
  word_t *o4 = alloc(16);
  assert(next_fit_start == block_header(o4));
  #endif

  #if SEARCH_MODE == BEST_FIT
  printf("Test best fit\n");
  alloc(8);
  word_t *z1 = alloc(64);
  alloc(8);
  word_t *z2 = alloc(16);
  free_(z2);
  free_(z1);
  word_t *z3 = alloc(16);
  assert(z3 == z2);
  word_t *z4 = alloc(64);
  assert(z4 == z1);
  #endif

  printf("All assertions passed\n");
} 

# How does `malloc` work?

This is a question I was wondering about. So I [read some posts](#References), and decided I should sketch possible implementation techniques to understand what's going on.

So far, this repository contains toy implementations of different free list heap allocators. A free list is a linked list of blocks in heap memory. An allocator creates those blocks, tracks if they are used, and re-uses blocks for new allocations if they have been freed.

- `free_list.c` uses a singly linked free list that contains all blocks, both used and unused. Each block has a flag indicating whether it is in use. Different searching strategies (first fit, next fit, etc.) can be used to search for free blocks in the list. In addition, large blocks are split when re-used and on free, blocks are merged into their neighbors to form bigger blocks.

- `explicit_free_list.c` uses a doubly linked free list that contains only free blocks. When an unused block is allocated, it is removed from the free list entirely. To perform this operation, it's helpful that the list is doubly linked. On `free`, the given block is inserted at the start of the free list. In contrast to `free_list.c`, this implementation's free list can be in any order. Adjacent blocks in the list must not be contiguous in memory. This file also implements the standard C `malloc` interface.

- `segregated_free_list.c` uses an array of singly linked free lists. Each of the lists (called a bucket) contains blocks of a specific minimum size (counted in words, not bytes). When a block is requested, the allocator first picks the right bucket for the given size. Then it searches the bucket for the block that fits the size best. An alternate strategy would be to keep all blocks in the list down to the same size. This would use more memory, but allocations would be quicker, too.

I'm sure there are bugs in the code, and the allocators are slow, but on a high level, they work! In `explicit_free_list.c`, I added wrappers around the allocator to be compatible with the `malloc`, `calloc`, `realloc`, `free` interface. So, we can use it as a drop-in replacement for the system allocator:

``` shell
./use-malloc.sh explicit_free_list.c ls
```

The script will compile `explicit_free_list.c` into `malloc.so` and then execute `ls` by overriding the system allocator with `LD_PRELOAD=./malloc.so`. In addition to the output of `ls` itself, it prints the allocator calls that `ls` makes, *on my machine* at least. 😅

`test-modes.sh` will compile and run `free_list.c` will three different modes of searching for free blocks in the free list.

# Alignment bit magic 🪄

All three files share the `align` function to align allocations to word boundaries. The `BlockHdr` struct is word-aligned by default, but the size of the allocation may be given in bytes. So, to keep consecutive allocations word-aligned, the size must be rounded up to the next word boundary.

``` c
size_t align(size_t size) {
  return (size + (sizeof(word_t) - 1)) & ~(sizeof(word_t) - 1);
}
```

`sizeof(word_t)` is a multiple of 2. Say it's $2^3$, then `sizeof(word_t) - 1` is a bit string with only the three least significant bits set: `00 ... 00111`. `~(sizeof(word_t) - 1)` then becomes a bit  string of only ones, except for the three least significant bits: `11 ... 11000`. So, `x & ~(sizeof(word_t) - 1)` sets the three least significant bits of `x` to `0`. This guarantees that the output of the function is word-aligned.

This alone would round down, however. To round up, the same bit string of $2^3 - 1$ ones is added to the size. If the size is not already word-aligned, it will have some of its three least significant bits set. In that case, adding `00 ... 00111` will carry some bits into the more significant part of `size`. This way, the size is rounded up.

# References

- [Memory Allocation](https://samwho.dev/memory-allocation/) by Sam Rose. Includes awesome animations, a great starting point I find.
- [Writing a Memory Allocator](http://dmitrysoshnikov.com/compilers/writing-a-memory-allocator/) by Dmitry Soshnikov. 
- [Malloc tutorial](https://danluu.com/malloc-tutorial/) by Dan Luu. 
- [Anatomy of a Program in Memory](https://manybutfinite.com/post/anatomy-of-a-program-in-memory/). Gives an overview of the virtual memory space of a process on Linux.

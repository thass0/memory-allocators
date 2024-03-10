#ifndef __DBG_H_
#define __DBG_H_

#include <stdarg.h> /* valist etc. */
#include <stdio.h> /* vsnprintf */
#include <unistd.h> /* write, STDERR_FILENO, etc. */
#include <string.h> /* strlen */

/* Tiny helper for heap allocator testing. */
/*
 * Calling printf and friends might mess with the heap.
 * To bypass any random errors, use this function to
 * print using write(2) and vsnprintf(3).
 * It also prints to stderr instead of stdout.
 */
void dbg(char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  char buf[0x1000] = {0};
  vsnprintf(buf, 0x1000, fmt, argp);
  write(STDERR_FILENO, buf, strlen(buf));
  va_end(argp);
}

#endif	/* __DBG_H_ */

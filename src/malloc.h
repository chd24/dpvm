/* dpvm: malloc: self implementation; T20.048-T20.157; $DVS:time$ */

#ifndef DPVM_MALLOC
#define DPVM_MALLOC

#include <stddef.h>

#define DPVM_PAGE_SHIFT	12
#define DPVM_PAGE_SIZE	(1 << DPVM_PAGE_SHIFT)

struct mallinfo2
{
  size_t arena;    /* non-mmapped space allocated from system */
  size_t ordblks;  /* number of free chunks */
  size_t smblks;   /* number of fastbin blocks */
  size_t hblks;    /* number of mmapped regions */
  size_t hblkhd;   /* space in mmapped regions */
  size_t usmblks;  /* always 0, preserved for backwards compatibility */
  size_t fsmblks;  /* space available in freed fastbin blocks */
  size_t uordblks; /* total allocated space */
  size_t fordblks; /* total free space */
  size_t keepcost; /* top-most, releasable (via malloc_trim) space */
};

extern void *dpvm_mmap(size_t size, int exec);
extern void *dpvm_mmap_file(size_t size, int fd);
extern int dpvm_munmap(void *mem, size_t size, int exec);
extern void *dpvm_malloc(size_t size);
extern void dpvm_free(void *mem, size_t size);
extern void *dpvm_calloc(size_t nmemb, size_t size);
extern void *dpvm_realloc(void *ptr, size_t old_size, size_t new_size);
extern struct mallinfo2 mallinfo2(void);

#endif

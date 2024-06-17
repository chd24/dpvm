/* dpvm: malloc */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <sys/mman.h>
#include "list.h"
#include "rbtree.h"
#include "malloc.h"

static char malloc_version[] = "dpvm malloc implementation T20.048-T20.157"; /* $DVS:time$

History:

T20.157 - added function dpvm_mmap_file

T20.051 - stable version, added dpvm_calloc

T20.050 - added malloc_usable_size() library function implementation, fixed crash, enhanced memory protection

T20.049 - first compiled version with malloc/calloc/realloc/free/mallinfo2 redefined

T20.048 - started implementation of dpvm self malloc module

*/

//#define MEM_DEBUG

#define N_PAGE_TREES	0x100			/* quantity of page trees corresponding to page address hashes */
#define ALLOC_ALIGN	0x10			/* minimum alignment of allocated region */
#define ALLOC_MIN	0x30			/* minimum size of allocated region */
#define ALLOC_MAX	(DPVM_PAGE_SIZE >> 1)	/* maximum size of allocated region inside page */

/* hash of dpvm string "dpvm malloc" */
#define MAGIC0		0x3d521f8696ef4c05ll
#define MAGIC1		0xf7ea7dcdf0b5e57cll
#define MAGIC2		0xe04ffb9016bacfb4ll
#define MAGIC3		0x9684a0d3744f9939ll

#define STATIC_ASSERT(test) _Static_assert((test), "(" #test ") failed")

/* free region on the page */
struct alloc_region {
	struct ldus_rbtree tree;	/* node in tree of pages with the same page address hash */
	uint64_t magic;			/* quantity of free regions in the page encoded in magic field */
	struct alloc_region *next;	/* next free region on the same page */
	struct list pages;		/* node in list of pages with the same size of allocated regions
					   and the same quantity of free regions */
};

STATIC_ASSERT(sizeof(struct alloc_region) <= ALLOC_MIN);

/* header page for pages with free regions of the same size */
struct alloc_head_page {
	struct list pages[DPVM_PAGE_SIZE / ALLOC_MIN + 1]; /* lists of pages with the same quantity of free regions */
};

STATIC_ASSERT(sizeof(struct alloc_head_page) <= DPVM_PAGE_SIZE);

#define INI0 {0, PTHREAD_MUTEX_INITIALIZER},
#define INI2 INI0 INI0 INI0 INI0
#define INI4 INI2 INI2 INI2 INI2
#define INI6 INI4 INI4 INI4 INI4
#define INI8 INI6 INI6 INI6 INI6

static struct alloc_head {
	struct alloc_head_page *head_page;
	pthread_mutex_t mutex;
} g_alloc_head[ALLOC_MAX / ALLOC_ALIGN + 1] = {
	INI6 INI6 INI0
};

static struct page_tree {
	struct ldus_rbtree *root;
	pthread_mutex_t mutex;
} g_page_tree[N_PAGE_TREES] = {
	INI8
};

static struct mallinfo2 g_mallinfo = {0};

#define lessthen(x, y) ((x) < (y))
ldus_rbtree_define(lessthen)

void *dpvm_mmap(size_t size, int exec) {
	size_t size0 = size;
	void *mem;
	if (!size)
		return 0;
	size = ((size - 1) | (DPVM_PAGE_SIZE - 1)) + 1;

	mem = mmap(0, size, PROT_READ | PROT_WRITE | (exec >= 0 ? PROT_EXEC : 0), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED)
		return 0;

	__sync_add_and_fetch(&g_mallinfo.hblkhd, size);
	__sync_add_and_fetch(&g_mallinfo.hblks, 1l);
	if (exec)
		__sync_add_and_fetch(&g_mallinfo.arena, size0);

	return mem;
}

void *dpvm_mmap_file(size_t size, int fd) {
	size_t size0 = size;
	void *mem;
	if (!size)
		return 0;
	size = ((size - 1) | (DPVM_PAGE_SIZE - 1)) + 1;

	mem = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED)
		return 0;

/*
	__sync_add_and_fetch(&g_mallinfo.hblkhd, size);
	__sync_add_and_fetch(&g_mallinfo.hblks, 1l);
*/

	return mem;
}

int dpvm_munmap(void *mem, size_t size, int exec) {
	size_t size0 = size;
	int res;
	if (!size)
		return -1;
	size = ((size - 1) | (DPVM_PAGE_SIZE - 1)) + 1;

	res = munmap(mem, size);

	if (!res) {
		__sync_add_and_fetch(&g_mallinfo.hblkhd, -size);
		__sync_add_and_fetch(&g_mallinfo.hblks, -1l);
		if (exec)
			__sync_add_and_fetch(&g_mallinfo.arena, -size0);
	}

	return res;
}

static inline size_t page_hash(void *addr) {
	size_t n = (size_t)addr >> DPVM_PAGE_SHIFT, res = 0;
	while (n) {
		res *= 43;
		res += n & 0xff;
		n >>= 8;
	}
	return res & (N_PAGE_TREES - 1);
}

static inline void set_reg_magic(struct alloc_region *reg, int nfree) {
	reg->magic = (nfree ? MAGIC2 : MAGIC3) + ((size_t)reg >> 4) + (unsigned)nfree;
}

static inline int test_reg_magic(struct alloc_region *reg, size_t size, int master) {
	ssize_t nfree;
	if (!reg)
		return -1;
	if (((size_t)reg - ((size_t)reg & -(size_t)DPVM_PAGE_SIZE)) % size)
		return -2;
	nfree = reg->magic - (master ? MAGIC2 : MAGIC3) - ((size_t)reg >> 4);
	if (master) {
		if (nfree <= 0)
			return -3;
		if (nfree >= DPVM_PAGE_SIZE / size)
			return -4;
	} else if (nfree)
		return -5;
	if (reg->next) {
		if ((size_t)reg->next >> DPVM_PAGE_SHIFT != (size_t)reg >> DPVM_PAGE_SHIFT)
			return -6;
		if (((ssize_t)reg->next - (ssize_t)reg) % (ssize_t)size)
			return -7;
		if (reg->next == reg)
			return -8;
	}
	return master ? (int)nfree : 0;
}

void *dpvm_malloc(size_t size) {
	struct alloc_head *head;
	size_t i, max_regions, size0;
	void *mem;
	int err = 0;

	if (!size)
		return 0;

	if (size > ALLOC_MAX)
		return dpvm_mmap(size, -1);

	size0 = size;
	size = ((size - 1) | (ALLOC_ALIGN - 1)) + 1;
	if (size < ALLOC_MIN)
		size = ALLOC_MIN;

	head = &g_alloc_head[size / ALLOC_ALIGN];
	max_regions = DPVM_PAGE_SIZE / size;
	mem = 0;

	pthread_mutex_lock(&head->mutex);

	if (!head->head_page) {
		head->head_page = (struct alloc_head_page *)dpvm_mmap(DPVM_PAGE_SIZE, 0);
		if (!head->head_page)
			goto end;
		for (i = 1; i < max_regions; i++)
			list_init(&head->head_page->pages[i]);
	}

	for (i = 1; i < max_regions; i++) {
		if (!list_is_empty(&head->head_page->pages[i])) {
			struct alloc_region *reg = (struct alloc_region *)container_of(head->head_page->pages[i].next,
					struct alloc_region, pages);

			mem = reg;
			if ((err = test_reg_magic(reg, size, 1)) != i) {
				if (err < 0)
					err = 10 - err;
				else
					err = -err;
				goto end;
			} else
				err = 0;

			list_remove(&reg->pages);

			if (i == 1) {
				struct page_tree *tree = &g_page_tree[page_hash(reg)];

				pthread_mutex_lock(&tree->mutex);
				ldus_rbtree_remove(&tree->root, &reg->tree);
				pthread_mutex_unlock(&tree->mutex);

				reg->magic = 0;
			} else {
				mem = reg->next;
				if ((err = test_reg_magic(reg->next, size, 0)) < 0) {
					err = 20 - err;
					goto end;
				} else
					err = 0;
				reg->next->magic = 0;
				reg->next = reg->next->next;
				set_reg_magic(reg, i - 1);
				list_insert(&head->head_page->pages[i - 1], &reg->pages);
			}

			goto end;
		}
	}

	{
		struct alloc_region *reg = (struct alloc_region *)dpvm_mmap(DPVM_PAGE_SIZE, 0);
		struct page_tree *tree = &g_page_tree[page_hash(reg)];
		uint8_t *ptr = (uint8_t *)reg;

		if (!reg)
			goto end;

		list_insert(&head->head_page->pages[max_regions - 1], &reg->pages);

		for (i = 0; i < max_regions - 2; i++, ptr += size) {
			((struct alloc_region *)ptr)->next = (struct alloc_region *)(ptr + size);
			set_reg_magic((struct alloc_region *)ptr, i ? 0 : max_regions - 1);
		}

		((struct alloc_region *)ptr)->next = 0;
		set_reg_magic((struct alloc_region *)ptr, max_regions == 2);
		mem = ptr + size;
		((struct alloc_region *)mem)->magic = 0;

		pthread_mutex_lock(&tree->mutex);
		ldus_rbtree_insert(&tree->root, &reg->tree);
		pthread_mutex_unlock(&tree->mutex);
	}

end:
	pthread_mutex_unlock(&head->mutex);

	if (err) {
		struct alloc_region *reg = (struct alloc_region *)mem;
		fprintf(stderr, "\r\n!!! %s: Internal error %d at dpvm_malloc(%lx): "
			"reg = %p, reg->magic = %lx, reg->next = %p\r\n",
			malloc_version, err, size0, reg, reg->magic, reg->next);
		fflush(stderr);
		mem = 0;
	} else {
#ifdef MEM_DEBUG
		fprintf(stderr, "dpvm_malloc(%lx) -> %p\r\n", size0, mem);
		fflush(stderr);
#endif
	}

	if (mem)
		__sync_add_and_fetch(&g_mallinfo.arena, size0);
	return mem;
}

void dpvm_free(void *mem, size_t size) {
	struct alloc_head *head;
	struct alloc_region *reg, *page, *prev;
	struct ldus_rbtree *upper;
	struct page_tree *tree;
	size_t i, max_regions, size0;
	int err = 0;

	if (!mem || !size)
		return;

	if (size > ALLOC_MAX) {
		if ((size_t)mem & (DPVM_PAGE_SIZE - 1)) {
			err = 30;
			goto fin;
		} else
			dpvm_munmap(mem, size, -1);
		return;
	}

	size0 = size;
	size = ((size - 1) | (ALLOC_ALIGN - 1)) + 1;
	if (size < ALLOC_MIN)
		size = ALLOC_MIN;

	reg = (struct alloc_region *)mem;
	page = (struct alloc_region *)((size_t)mem & -(size_t)DPVM_PAGE_SIZE);

	if (((uint8_t *)reg - (uint8_t *)page) % size) {
		err = 31;
		goto fin;
	}

	head = &g_alloc_head[size / ALLOC_ALIGN];
	max_regions = DPVM_PAGE_SIZE / size;
	tree = &g_page_tree[page_hash(reg)];

	pthread_mutex_lock(&head->mutex);

	pthread_mutex_lock(&tree->mutex);
	upper = ldus_rbtree_upper_bound(tree->root, &page->tree);
	pthread_mutex_unlock(&tree->mutex);

	prev = container_of(upper, struct alloc_region, tree);

	if (upper && (uint8_t *)prev - (uint8_t *)page < DPVM_PAGE_SIZE) {
		int nfree;

		if (prev < page || ((uint8_t *)prev - (uint8_t *)page) % size) {
			err = 32;
			goto end;
		}

		if (prev == reg) {
			err = 33;
			goto end;
		}

		if ((nfree = test_reg_magic(prev, size, 1)) < 0) {
			err = 40 - nfree;
			goto end;
		}

		list_remove(&prev->pages);

		if (nfree == max_regions - 1) {
			pthread_mutex_lock(&tree->mutex);
			ldus_rbtree_remove(&tree->root, &prev->tree);
			pthread_mutex_unlock(&tree->mutex);

			dpvm_munmap(page, DPVM_PAGE_SIZE, 0);
		} else {
			reg->next = prev->next;
			prev->next = reg;
			set_reg_magic(prev, nfree + 1);
			set_reg_magic(reg, 0);
			list_insert(&head->head_page->pages[nfree + 1], &prev->pages);
		}
	} else {
		reg->next = 0;
		set_reg_magic(reg, 1);
		list_insert(&head->head_page->pages[1], &reg->pages);

		pthread_mutex_lock(&tree->mutex);
		ldus_rbtree_insert(&tree->root, &reg->tree);
		pthread_mutex_unlock(&tree->mutex);
	}

end:
	pthread_mutex_unlock(&head->mutex);

fin:
	if (err) {
		fprintf(stderr, "\r\n!!! %s: Internal error %d at dpvm_free(%p, %lx): prev = %p\r\n",
			malloc_version, err, mem, size0, prev);
		fflush(stderr);
		mem = 0;
	} else {
#ifdef MEM_DEBUG
		fprintf(stderr, "dpvm_free(%p, %lx): prev = %p\r\n", mem, size0, prev);
		fflush(stderr);
#endif
		__sync_add_and_fetch(&g_mallinfo.arena, -size0);
	}
}

void *dpvm_calloc(size_t nmemb, size_t size) {
	void *mem = dpvm_malloc(nmemb * size);
	if (mem)
		memset(mem, 0, nmemb * size);
	return mem;
}

static inline size_t usable_size(size_t size) {
	if (!size)
		return 0;
	if (size > ALLOC_MAX)
		return ((size - 1) | (DPVM_PAGE_SIZE - 1)) + 1;
	size = ((size - 1) | (ALLOC_ALIGN - 1)) + 1;
	if (size < ALLOC_MIN)
		size = ALLOC_MIN;
	return size;
}

void *dpvm_realloc(void *ptr, size_t old_size, size_t new_size) {
	void *mem;

	if (!new_size) {
		dpvm_free(ptr, old_size);
		return 0;
	}

	if (!ptr || !old_size)
		return dpvm_malloc(new_size);

	if (usable_size(old_size) == usable_size(new_size))
		return ptr;

	mem = dpvm_malloc(new_size);
	if (!mem)
		return 0;

	memcpy(mem, ptr, (old_size < new_size ? old_size : new_size));

	dpvm_free(ptr, old_size);
	return mem;
}

struct magic_header {
	uint64_t magic0;
	uint64_t magic1;
};

static inline void set_magic_header(struct magic_header *mem, size_t size) {
	mem->magic0 = MAGIC0 + (size_t)mem + size;
	mem->magic1 = MAGIC1 + ((size_t)mem >> 4) + size;
}

static inline size_t test_magic_header(struct magic_header *mem) {
	size_t size0 = mem->magic0 - MAGIC0 - (size_t)mem,
	       size1 = mem->magic1 - MAGIC1 - ((size_t)mem >> 4);

	return size0 == size1 ? size0 : 0;
}

void *malloc(size_t size) {
	struct magic_header *mem = (struct magic_header *)dpvm_malloc(size + sizeof(struct magic_header));
	if (!mem)
		return 0;

	set_magic_header(mem, size);

	return mem + 1;
}

void free(void *ptr) {
	struct magic_header *mem;
	size_t size;

	if (!ptr)
		return;

	if ((size_t)ptr & (ALLOC_ALIGN - 1)) {
		fprintf(stderr, "\r\n!!! %s Internal error 40 at free(%p)\r\n", malloc_version, ptr);
		fflush(stderr);
		return;
	}

	mem = ((struct magic_header *)ptr) - 1;
	if (!(size = test_magic_header(mem))) {
		fprintf(stderr, "\r\n!!! %s Internal error 41 at free(%p)\r\n", malloc_version, ptr);
		fflush(stderr);
		return;
	}

	mem->magic0 = 0;
	mem->magic1 = 0;

	dpvm_free(mem, size + sizeof(struct magic_header));
}

void *calloc(size_t nmemb, size_t size) {
	void *mem = malloc(nmemb * size);
	if (mem)
		memset(mem, 0, nmemb * size);
	return mem;
}

void *realloc(void *ptr, size_t size) {
	struct magic_header *mem;
	size_t size0;

	if (!size) {
		free(ptr);
		return 0;
	}

	if (!ptr)
		return malloc(size);

	if ((size_t)ptr & (ALLOC_ALIGN - 1)) {
		fprintf(stderr, "\r\n!!! %s Internal error 50 at realloc(%p, %lx)\r\n",
			malloc_version, ptr, size);
		fflush(stderr);
		return 0;
	}

	mem = ((struct magic_header *)ptr) - 1;
	if (!(size0 = test_magic_header(mem))) {
		fprintf(stderr, "\r\n!!! %s Internal error 51 at realloc(%p, %lx)\r\n",
			malloc_version, ptr, size);
		fflush(stderr);
		return 0;
	}

	mem = (struct magic_header *)dpvm_realloc(mem, size0 + sizeof(struct magic_header),
		size + sizeof(struct magic_header));

	if (!mem)
		return 0;

	set_magic_header(mem, size);

	return mem + 1;
}

size_t malloc_usable_size(const void *ptr) {
	struct magic_header *mem;
	size_t size;

	if (!ptr)
		return 0;

	if ((size_t)ptr & (ALLOC_ALIGN - 1)) {
		fprintf(stderr, "\r\n!!! %s Internal error 60 at malloc_usable_size(%p)\r\n",
			malloc_version, ptr);
		fflush(stderr);
		return 0;
	}

	mem = ((struct magic_header *)ptr) - 1;
	if (!(size = test_magic_header(mem))) {
		fprintf(stderr, "\r\n!!! %s Internal error 61 at malloc_usable_size(%p)\r\n",
			malloc_version, ptr);
		fflush(stderr);
		return 0;
	}

	return usable_size(size + sizeof(struct magic_header)) - sizeof(struct magic_header);
}

struct mallinfo2 mallinfo2(void) {
	return g_mallinfo;
}

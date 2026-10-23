#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <pmm.h>
#include <list.h>

/*
 * Number of fixed-size object caches.  The caches are intentionally small:
 * size requests are rounded up to one of 8, 16, 32, 64, 128 or 256 bytes.
 */
#define SLUB_CACHE_COUNT 6

/*
 * One kmem_cache describes all slabs that serve the same object size.
 *
 * Slabs are single pages obtained from the page allocator.  The first
 * "offset" bytes of a slab page hold the slab metadata, and the rest of the
 * page is divided into objects_per_slab equally sized objects.
 *
 * free_list is an intrusive singly linked list: the first word of every free
 * object stores the address of the next free object.  This works even for
 * 8-byte objects, where a full list_entry_t would not fit.
 */
struct kmem_cache {
    const char *name;
    size_t object_size;       // size of one object, in bytes
    size_t offset;            // offset of the first object inside a slab page
    size_t objects_per_slab;  // number of objects in one slab page
    void *free_list;          // head of the free-object list
    size_t nr_free;           // number of free objects in all slabs
    size_t nr_total;          // number of objects in all slabs
    size_t nr_slabs;          // number of slab pages currently owned
    list_entry_t slab_list;   // list of struct slub_slab, linked by slab_link
};

/* Initialize the fixed caches.  slub_alloc() also calls this lazily. */
void slub_init(void);

/*
 * Allocate one object whose usable size is at least "size" bytes.
 * Returns NULL for size == 0, for requests larger than the largest cache,
 * or when the page allocator cannot provide another slab.
 */
void *slub_alloc(size_t size);

/*
 * Return an object to its cache.  NULL, unknown pointers and double frees are
 * rejected without modifying the free lists.
 */
void slub_free(void *ptr);

/* Self-test for the SLUB layer.  Requires pmm_init() to have completed. */
void slub_check(void);

#endif /* !__KERN_MM_SLUB_H__ */

#include <slub.h>
#include <assert.h>
#include <defs.h>
#include <memlayout.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>

/*
 * SLUB has two layers:
 *
 *   page layer : alloc_pages(1) / free_pages(page, 1) in pmm.c
 *   object layer: the caches below
 *
 * A slab is exactly one page.  It begins with a struct slub_slab, followed by
 * a naturally aligned array of objects.  Objects in a cache are all the same
 * size, so a pointer can be mapped back to its slab by looking for the page
 * range that contains it.
 */

#define SLUB_MIN_OBJECT_SIZE 8
#define SLUB_MAX_OBJECTS     (PGSIZE / SLUB_MIN_OBJECT_SIZE)
#define SLUB_STATE_BYTES     ((SLUB_MAX_OBJECTS + 7) / 8)

struct slub_slab {
    list_entry_t slab_link;       // link in kmem_cache.slab_list
    struct kmem_cache *cache;     // owning cache
    struct Page *page;            // page returned by alloc_pages(1)
    uint8_t *base;                // first object in this slab
    size_t nr_objects;            // objects in this slab
    size_t nr_free;               // free objects in this slab
    uint8_t state[SLUB_STATE_BYTES];  // 1 = allocated, 0 = free
};

#define le2slab(le) to_struct((le), struct slub_slab, slab_link)

static struct kmem_cache slub_caches[SLUB_CACHE_COUNT] = {
    {.name = "slub-8", .object_size = 8},
    {.name = "slub-16", .object_size = 16},
    {.name = "slub-32", .object_size = 32},
    {.name = "slub-64", .object_size = 64},
    {.name = "slub-128", .object_size = 128},
    {.name = "slub-256", .object_size = 256},
};

static bool slub_initialized = 0;

void slub_init(void) {
    if (slub_initialized) {
        return;
    }

    for (size_t i = 0; i < SLUB_CACHE_COUNT; i++) {
        struct kmem_cache *cache = &slub_caches[i];

        // Keep every object aligned to its own size.  The page itself is
        // page-aligned, so an offset that is a multiple of object_size is
        // sufficient for all objects in the slab.
        cache->offset = (size_t)ROUNDUP(sizeof(struct slub_slab),
                                        cache->object_size);
        if (cache->offset >= PGSIZE) {
            panic("slub_init: %s metadata does not fit in a page",
                  cache->name);
        }
        cache->objects_per_slab =
            (PGSIZE - cache->offset) / cache->object_size;
        if (cache->objects_per_slab == 0 ||
            cache->objects_per_slab > SLUB_MAX_OBJECTS) {
            panic("slub_init: invalid object count for %s", cache->name);
        }

        cache->free_list = NULL;
        cache->nr_free = 0;
        cache->nr_total = 0;
        cache->nr_slabs = 0;
        list_init(&cache->slab_list);
    }

    slub_initialized = 1;
}

// Return the smallest cache that can hold a request of "size" bytes.
static struct kmem_cache *slub_cache_for(size_t size) {
    for (size_t i = 0; i < SLUB_CACHE_COUNT; i++) {
        if (size <= slub_caches[i].object_size) {
            return &slub_caches[i];
        }
    }
    return NULL;
}

// Convert a Page descriptor to the kernel virtual address of its page.
// The kernel maps all physical memory at va_pa_offset.
static inline void *slub_page_kva(struct Page *page) {
    return (void *)(page2pa(page) + (uintptr_t)va_pa_offset);
}

static inline bool slub_bit_test(const uint8_t *bitmap, size_t index) {
    return (bitmap[index / 8] & (uint8_t)(1u << (index % 8))) != 0;
}

static inline void slub_bit_set(uint8_t *bitmap, size_t index) {
    bitmap[index / 8] |= (uint8_t)(1u << (index % 8));
}

static inline void slub_bit_clear(uint8_t *bitmap, size_t index) {
    bitmap[index / 8] &= (uint8_t)~(1u << (index % 8));
}

// Find the slab in this cache that contains "obj".
static struct slub_slab *slub_slab_of(struct kmem_cache *cache, void *obj) {
    uintptr_t addr = (uintptr_t)obj;
    list_entry_t *head = &cache->slab_list;

    for (list_entry_t *le = list_next(head); le != head; le = list_next(le)) {
        struct slub_slab *slab = le2slab(le);
        uintptr_t start = (uintptr_t)slab->base;
        uintptr_t end =
            start + (uintptr_t)slab->nr_objects * cache->object_size;
        if (addr >= start && addr < end) {
            return slab;
        }
    }
    return NULL;
}

// Obtain one page from the page layer and turn it into a new slab.  The new
// objects are prepended to the cache's free-object list as one chain.
static struct slub_slab *slub_grow(struct kmem_cache *cache) {
    if (pmm_manager == NULL) {
        return NULL;
    }

    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        return NULL;
    }

    uint8_t *page_base = (uint8_t *)slub_page_kva(page);
    struct slub_slab *slab = (struct slub_slab *)page_base;
    memset(slab, 0, sizeof(*slab));

    slab->cache = cache;
    slab->page = page;
    slab->base = page_base + cache->offset;
    slab->nr_objects = cache->objects_per_slab;
    slab->nr_free = slab->nr_objects;

    list_init(&slab->slab_link);
    list_add(&cache->slab_list, &slab->slab_link);

    size_t object_size = cache->object_size;
    for (size_t i = 0; i < slab->nr_objects; i++) {
        void *obj = (void *)(slab->base + i * object_size);
        void *next;
        if (i + 1 < slab->nr_objects) {
            next = (void *)(slab->base + (i + 1) * object_size);
        } else {
            next = cache->free_list;
        }
        *(void **)obj = next;
    }

    cache->free_list = slab->base;
    cache->nr_free += slab->nr_objects;
    cache->nr_total += slab->nr_objects;
    cache->nr_slabs++;
    return slab;
}

// Locate the cache, slab and object index for a user pointer.  A pointer that
// falls in the metadata area, is not object-aligned, or is otherwise unknown
// is rejected.
static bool slub_locate(void *ptr, struct kmem_cache **cache_out,
                        struct slub_slab **slab_out, size_t *index_out) {
    uintptr_t addr = (uintptr_t)ptr;

    for (size_t i = 0; i < SLUB_CACHE_COUNT; i++) {
        struct kmem_cache *cache = &slub_caches[i];
        list_entry_t *head = &cache->slab_list;

        for (list_entry_t *le = list_next(head); le != head;
             le = list_next(le)) {
            struct slub_slab *slab = le2slab(le);
            uintptr_t start = (uintptr_t)slab->base;
            uintptr_t end =
                start + (uintptr_t)slab->nr_objects * cache->object_size;

            if (addr < start || addr >= end) {
                continue;
            }

            uintptr_t delta = addr - start;
            if (delta % cache->object_size != 0) {
                return 0;
            }

            *cache_out = cache;
            *slab_out = slab;
            *index_out = (size_t)(delta / cache->object_size);
            return 1;
        }
    }
    return 0;
}

// Remove every object of "slab" from the cache free list.  This is called
// only when all objects in the slab are free, so the whole page can then be
// returned to the page allocator.
static void slub_purge_slab(struct kmem_cache *cache,
                            struct slub_slab *slab) {
    uintptr_t start = (uintptr_t)slab->base;
    uintptr_t end = start + (uintptr_t)slab->nr_objects * cache->object_size;
    void **link = &cache->free_list;
    size_t removed = 0;

    while (*link != NULL) {
        void *obj = *link;
        if ((uintptr_t)obj >= start && (uintptr_t)obj < end) {
            *link = *(void **)obj;
            removed++;
        } else {
            // The first word of a free object is its next pointer.
            link = (void **)obj;
        }
    }

    if (removed != slab->nr_objects) {
        panic("slub: slab free-list accounting is inconsistent");
    }
}

// Return an empty slab to the page layer after unlinking it.
static void slub_evict_slab(struct kmem_cache *cache,
                            struct slub_slab *slab) {
    size_t nr_objects = slab->nr_objects;
    struct Page *page = slab->page;

    slub_purge_slab(cache, slab);
    list_del(&slab->slab_link);

    cache->nr_free -= nr_objects;
    cache->nr_total -= nr_objects;
    cache->nr_slabs--;

    free_pages(page, 1);
}


void *slub_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    slub_init();

    struct kmem_cache *cache = slub_cache_for(size);
    if (cache == NULL) {
        return NULL;
    }

    if (cache->free_list == NULL) {
        if (slub_grow(cache) == NULL) {
            return NULL;
        }
    }

    void *obj = cache->free_list;
    if (obj == NULL) {
        return NULL;
    }
    cache->free_list = *(void **)obj;
    cache->nr_free--;

    struct slub_slab *slab = slub_slab_of(cache, obj);
    if (slab == NULL) {
        panic("slub_alloc: free object is not inside any slab");
    }

    uintptr_t delta = (uintptr_t)obj - (uintptr_t)slab->base;
    if (delta % cache->object_size != 0) {
        panic("slub_alloc: misaligned free object");
    }
    size_t index = (size_t)(delta / cache->object_size);
    if (index >= slab->nr_objects || slub_bit_test(slab->state, index)) {
        panic("slub_alloc: invalid free object state");
    }
    if (slab->nr_free == 0) {
        panic("slub_alloc: slab has no free object");
    }

    slub_bit_set(slab->state, index);
    slab->nr_free--;
    return obj;
}

void slub_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    slub_init();

    struct kmem_cache *cache;
    struct slub_slab *slab;
    size_t index;

    if (!slub_locate(ptr, &cache, &slab, &index)) {
        warn("slub_free: invalid pointer %p", ptr);
        return;
    }

    // state == 0 means the object is already on a free list.  Rejecting the
    // second free keeps the free list and the slab counters consistent.
    if (!slub_bit_test(slab->state, index)) {
        warn("slub_free: double free of %p", ptr);
        return;
    }

    slub_bit_clear(slab->state, index);
    *(void **)ptr = cache->free_list;
    cache->free_list = ptr;
    cache->nr_free++;
    slab->nr_free++;

    if (slab->nr_free > slab->nr_objects) {
        panic("slub_free: slab free count overflow");
    }

    if (slab->nr_free == slab->nr_objects) {
        slub_evict_slab(cache, slab);
    }
}
// ---------------------------------------------------------------------------
// slub_check - exercise allocation, slab growth, reuse and boundary handling.
// ---------------------------------------------------------------------------

#define SLUB_CHECK_BULK_MAX (PGSIZE / 64 + 1)

static bool slub_check_overlap(void *a, size_t a_size, void *b,
                               size_t b_size) {
    uintptr_t a_start = (uintptr_t)a;
    uintptr_t a_end = a_start + a_size;
    uintptr_t b_start = (uintptr_t)b;
    uintptr_t b_end = b_start + b_size;

    return a_start < b_end && b_start < a_end;
}

void slub_check(void) {
    if (pmm_manager == NULL) {
        warn("slub_check: pmm is not initialized");
        return;
    }

    slub_init();

    size_t pages_before = nr_free_pages();

    // 1. Requests of different sizes must all succeed and not overlap.
    void *p8 = slub_alloc(8);
    void *p64 = slub_alloc(64);
    void *p200 = slub_alloc(200);
    assert(p8 != NULL);
    assert(p64 != NULL);
    assert(p200 != NULL);
    assert(!slub_check_overlap(p8, 8, p64, 64));
    assert(!slub_check_overlap(p8, 8, p200, 200));
    assert(!slub_check_overlap(p64, 64, p200, 200));
    assert((uintptr_t)p8 % 8 == 0);
    assert((uintptr_t)p64 % 64 == 0);
    assert((uintptr_t)p200 % 256 == 0);

    slub_free(p8);
    slub_free(p64);
    slub_free(p200);
    assert(nr_free_pages() == pages_before);
    cprintf("  [slub_check] size classes/non-overlap: passed\n");

    // 2. One object more than a slab holds must allocate a second page.
    struct kmem_cache *cache = slub_cache_for(64);
    assert(cache != NULL);
    assert(cache->free_list == NULL);
    assert(cache->nr_slabs == 0);

    size_t nr_objects = cache->objects_per_slab + 1;
    assert(nr_objects <= SLUB_CHECK_BULK_MAX);

    static void *bulk[SLUB_CHECK_BULK_MAX];
    size_t slabs_before = cache->nr_slabs;

    for (size_t i = 0; i < nr_objects; i++) {
        bulk[i] = slub_alloc(64);
        assert(bulk[i] != NULL);
    }

    size_t pages_after = nr_free_pages();
    assert(pages_before - pages_after == 2);
    assert(cache->nr_slabs == slabs_before + 2);
    cprintf("  [slub_check] slab growth: used %lu page(s): passed\n",
            (unsigned long)(pages_before - pages_after));

    for (size_t i = 0; i < nr_objects; i++) {
        slub_free(bulk[i]);
    }
    assert(nr_free_pages() == pages_before);
    assert(cache->nr_slabs == slabs_before);
    cprintf("  [slub_check] bulk free/page reclaim: passed\n");

    // 3. A freed object must be reused by the next same-size allocation.
    void *first = slub_alloc(32);
    void *second = slub_alloc(32);
    assert(first != NULL);
    assert(second != NULL);
    assert(first != second);

    slub_free(first);
    void *reused = slub_alloc(32);
    assert(reused == first);
    cprintf("  [slub_check] free-list reuse: passed\n");

    slub_free(second);
    slub_free(reused);
    assert(nr_free_pages() == pages_before);

    // 4. A zero-sized request may fail or use the smallest cache.
    void *zero = slub_alloc(0);
    if (zero != NULL) {
        slub_free(zero);
    }
    cprintf("  [slub_check] size == 0: handled (%s)\n",
            zero == NULL ? "NULL" : "minimum object");
    assert(nr_free_pages() == pages_before);

    cprintf("slub_check() succeeded!\n");
}



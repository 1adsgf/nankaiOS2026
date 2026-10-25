/* Host-side behavioral checks, compiled with the real kernel headers/module. */
#include <buddy_pmm.h>
#include <stdarg.h>

extern int printf(const char *, ...);
extern int vprintf(const char *, va_list);
extern void exit(int) __attribute__((noreturn));

static struct Page arena[512];
struct Page *pages = arena;
/* An odd base catches implementations that XOR descriptor indices. */
const size_t nbase = 0x80003;
size_t npage = 0x80003 + 512;
uint64_t va_pa_offset;

void __panic(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    printf("assertion at %s:%d: ", file, line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    exit(2);
}

int cprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

static const struct pmm_manager *manager = &buddy_pmm_manager;

static void reset(void) {
    manager->init();
    for (size_t i = 0; i < 512; ++i) {
        arena[i].flags = 0;
        SetPageReserved(arena + i);
        arena[i].property = 0;
        arena[i].ref = 0;
        list_init(&arena[i].page_link);
    }
}

static size_t rounded(size_t n) {
    size_t count = 1;
    while (count < n) {
        count *= 2;
    }
    return count;
}

static bool disjoint(struct Page *a, size_t na, struct Page *b, size_t nb) {
    ppn_t pa = page2ppn(a), pb = page2ppn(b);
    return pa + na <= pb || pb + nb <= pa;
}

static struct Page *aligned_start(void) {
    return arena + ((32 - (nbase & 31)) & 31);
}

static void check_irregular_range(void) {
    struct Page *allocated[101];
    bool seen[512] = {0};
    reset();
    manager->init_memmap(arena + 5, 101);
    assert(manager->nr_free_pages() == 101);
    for (size_t i = 0; i < 101; ++i) {
        allocated[i] = manager->alloc_pages(1);
        assert(allocated[i] >= arena + 5 && allocated[i] < arena + 106);
        size_t index = allocated[i] - arena;
        assert(!seen[index]);
        seen[index] = 1;
    }
    assert(manager->nr_free_pages() == 0);
    assert(manager->alloc_pages(1) == NULL);
    for (size_t i = 0; i < 101; i += 2) {
        manager->free_pages(allocated[i], 1);
    }
    for (size_t i = 1; i < 101; i += 2) {
        manager->free_pages(allocated[i], 1);
    }
    assert(manager->nr_free_pages() == 101);
    assert(PageReserved(arena + 4) && PageReserved(arena + 106));
    manager->check();
}

static void check_round_split_merge(void) {
    reset();
    struct Page *start = aligned_start();
    manager->init_memmap(start, 32);
    struct Page *a = manager->alloc_pages(3);
    assert(a != NULL && (page2ppn(a) & 3) == 0);
    assert(manager->nr_free_pages() == 28);
    struct Page *b = manager->alloc_pages(5);
    assert(b != NULL && (page2ppn(b) & 7) == 0);
    assert(disjoint(a, 4, b, 8));
    assert(manager->nr_free_pages() == 20);
    manager->free_pages(a, 3);
    manager->free_pages(b, 5);
    assert(manager->nr_free_pages() == 32);
    struct Page *whole = manager->alloc_pages(32);
    assert(whole == start && manager->nr_free_pages() == 0);
    manager->free_pages(whole, 32);
    manager->check();
}

static void check_adjacent_non_buddies(void) {
    reset();
    struct Page *start = aligned_start();
    struct Page *by_offset[4] = {NULL};
    manager->init_memmap(start, 4);
    for (size_t i = 0; i < 4; ++i) {
        struct Page *page = manager->alloc_pages(1);
        assert(page >= start && page < start + 4);
        size_t index = page - start;
        assert(by_offset[index] == NULL);
        by_offset[index] = page;
    }
    manager->free_pages(by_offset[1], 1);
    manager->free_pages(by_offset[2], 1);
    assert(manager->nr_free_pages() == 2);
    assert(manager->alloc_pages(2) == NULL);
    assert(manager->nr_free_pages() == 2);
    manager->free_pages(by_offset[0], 1);
    struct Page *pair = manager->alloc_pages(2);
    assert(pair == start);
    manager->free_pages(pair, 2);
    manager->free_pages(by_offset[3], 1);
    struct Page *whole = manager->alloc_pages(4);
    assert(whole == start);
    manager->free_pages(whole, 4);
    manager->check();
}

static void check_regions_and_holes(void) {
    reset();
    struct Page *start = aligned_start();
    manager->init_memmap(start + 8, 8);
    manager->init_memmap(start, 8);
    struct Page *whole = manager->alloc_pages(16);
    assert(whole == start);
    manager->free_pages(whole, 16);
    reset();
    manager->init_memmap(start, 4);
    manager->init_memmap(start + 8, 4);
    assert(manager->alloc_pages(8) == NULL);
    assert(manager->nr_free_pages() == 8);
    assert(PageReserved(start + 4) && PageReserved(start + 7));
    manager->check();
}

static void check_mixed_workload(void) {
    struct Page *live[64] = {NULL};
    size_t requests[64] = {0};
    uint32_t random = 0x2411584;
    reset();
    manager->init_memmap(arena + 7, 499);
    for (size_t step = 0; step < 3000; ++step) {
        random = random * 1664525U + 1013904223U;
        size_t slot = (random >> 16) % 64;
        size_t before = manager->nr_free_pages();
        if (live[slot] != NULL) {
            manager->free_pages(live[slot], requests[slot]);
            assert(manager->nr_free_pages() == before + rounded(requests[slot]));
            live[slot] = NULL;
        } else {
            size_t n = ((random >> 8) % 25) + 1;
            struct Page *page = manager->alloc_pages(n);
            if (page == NULL) {
                assert(manager->nr_free_pages() == before);
            } else {
                size_t count = rounded(n);
                assert(page >= arena + 7 && page + count <= arena + 506);
                assert((page2ppn(page) & (count - 1)) == 0);
                assert(manager->nr_free_pages() == before - count);
                for (size_t i = 0; i < 64; ++i) {
                    if (live[i] != NULL) {
                        assert(disjoint(page, count, live[i], rounded(requests[i])));
                    }
                }
                live[slot] = page;
                requests[slot] = n;
            }
        }
        if (step % 100 == 0) {
            manager->check();
        }
    }
    for (size_t i = 0; i < 64; ++i) {
        if (live[i] != NULL) {
            manager->free_pages(live[i], requests[i]);
        }
    }
    assert(manager->nr_free_pages() == 499);
    assert(manager->alloc_pages(0) == NULL);
    assert(manager->alloc_pages(500) == NULL);
    assert(manager->alloc_pages((size_t)-1) == NULL);
    assert(manager->nr_free_pages() == 499);
    manager->check();
}

int main(int argc, char **argv) {
    if (argc > 1) {
        reset();
        manager->init_memmap(aligned_start(), 32);
        struct Page *page = manager->alloc_pages(3);
        assert(page != NULL);
        if (argv[1][0] == 'd') {
            manager->free_pages(page, 3);
            manager->free_pages(page, 3);
        } else if (argv[1][0] == 'm') {
            manager->free_pages(page, 2);
        } else if (argv[1][0] == 'i') {
            manager->free_pages(page + 1, 1);
        } else if (argv[1][0] == 'n') {
            manager->free_pages(NULL, 1);
        }
        return 3;  /* A negative test must stop in __panic instead. */
    }
    check_irregular_range();
    check_round_split_merge();
    check_adjacent_non_buddies();
    check_regions_and_holes();
    check_mixed_workload();
    printf("Host Buddy behavior checks: PASS (including 3000 mixed operations).\n");
    return 0;
}

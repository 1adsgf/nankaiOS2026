#include <buddy_pmm.h>
#include <list.h>
#include <stdio.h>

// lab2 function2-3 2411584
/* property is the page count on a free or allocated head; free heads alone
 * have PageProperty set. Interior pages have property == 0. */
#define BUDDY_MAX_ORDER (sizeof(((struct Page *)0)->property) * 8 - 1)
static list_entry_t free_lists[BUDDY_MAX_ORDER + 1];
static size_t nr_free;

static size_t block_pages(unsigned int order) {
    assert(order <= BUDDY_MAX_ORDER);
    return (size_t)1 << order;
}

static bool request_order(size_t n, unsigned int *order) {
    if (n == 0 || n > block_pages(BUDDY_MAX_ORDER)) {
        return 0;
    }
    *order = 0;
    while (block_pages(*order) < n) {
        ++*order;
    }
    return 1;
}

static bool valid_range(struct Page *base, size_t n) {
    if (pages == NULL || npage <= nbase || n == 0 ||
        (uintptr_t)base < (uintptr_t)pages) {
        return 0;
    }
    uintptr_t offset = (uintptr_t)base - (uintptr_t)pages;
    size_t index = offset / sizeof(struct Page);
    return offset % sizeof(struct Page) == 0 &&
           index < npage - nbase && n <= npage - nbase - index;
}

static void unlink_block(struct Page *page) {
    list_del_init(&page->page_link);
    ClearPageProperty(page);
    page->property = 0;
}

/* Merge by physical page number, never by descriptor-array index. */
static void release_block(struct Page *base, unsigned int order) {
    while (order < BUDDY_MAX_ORDER) {
        size_t count = block_pages(order);
        ppn_t partner_ppn = page2ppn(base) ^ count;
        if (partner_ppn < nbase || partner_ppn >= npage ||
            count > npage - partner_ppn) {
            break;
        }
        struct Page *partner = pages + (partner_ppn - nbase);
        if (PageReserved(partner) || !PageProperty(partner) ||
            partner->property != count) {
            break;
        }
        unlink_block(partner);
        if (partner_ppn < page2ppn(base)) {
            base = partner;
        }
        ++order;
    }
    base->property = block_pages(order);
    SetPageProperty(base);
    list_add(&free_lists[order], &base->page_link);
}

static void buddy_init(void) {
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        list_init(&free_lists[order]);
    }
    nr_free = 0;
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(valid_range(base, n));
    assert(nr_free <= npage - nbase - n);
    for (size_t i = 0; i < n; ++i) {
        assert(PageReserved(base + i));
    }
    for (size_t i = 0; i < n; ++i) {
        struct Page *page = base + i;
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }
    nr_free += n;
    while (n != 0) {
        unsigned int order = 0;
        ppn_t ppn = page2ppn(base);
        while (order < BUDDY_MAX_ORDER &&
               block_pages(order + 1) <= n &&
               (ppn & (block_pages(order + 1) - 1)) == 0) {
            ++order;
        }
        size_t count = block_pages(order);
        release_block(base, order);
        base += count;
        n -= count;
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    unsigned int wanted;
    if (n > nr_free || !request_order(n, &wanted)) {
        return NULL;
    }
    size_t count = block_pages(wanted);
    if (count > nr_free) {
        return NULL;
    }
    unsigned int order = wanted;
    while (order <= BUDDY_MAX_ORDER && list_empty(&free_lists[order])) {
        ++order;
    }
    if (order > BUDDY_MAX_ORDER) {
        return NULL;
    }
    struct Page *base = le2page(list_next(&free_lists[order]), page_link);
    assert(valid_range(base, block_pages(order)));
    assert(PageProperty(base) && base->property == block_pages(order));
    unlink_block(base);
    while (order > wanted) {
        --order;
        struct Page *right = base + block_pages(order);
        right->property = block_pages(order);
        SetPageProperty(right);
        list_add(&free_lists[order], &right->page_link);
    }
    base->property = count;
    nr_free -= count;
    return base;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    unsigned int order;
    assert(request_order(n, &order));
    size_t count = block_pages(order);
    assert(valid_range(base, count));
    assert(!PageReserved(base) && !PageProperty(base));
    assert(base->property == count);
    assert((page2ppn(base) & (count - 1)) == 0);
    assert(nr_free <= npage - nbase - count);
    for (size_t i = 0; i < count; ++i) {
        struct Page *page = base + i;
        assert(!PageReserved(page) && !PageProperty(page));
        assert(i == 0 || page->property == 0);
    }
    for (size_t i = 0; i < count; ++i) {
        struct Page *page = base + i;
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
    }
    nr_free += count;
    release_block(base, order);
}

static size_t buddy_nr_free_pages(void) {
    return nr_free;
}

static void check_invariants(void) {
    size_t total = 0;
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        size_t count = block_pages(order);
        list_entry_t *head = &free_lists[order];
        for (list_entry_t *le = list_next(head); le != head;
             le = list_next(le)) {
            struct Page *base = le2page(le, page_link);
            assert(valid_range(base, count));
            assert(PageProperty(base) && base->property == count);
            assert((page2ppn(base) & (count - 1)) == 0);
            assert(list_next(list_prev(le)) == le);
            assert(list_prev(list_next(le)) == le);
            assert(total <= npage - nbase - count);
            total += count;
            for (size_t i = 0; i < count; ++i) {
                struct Page *page = base + i;
                assert(!PageReserved(page) && page_ref(page) == 0);
                assert(i == 0 || (!PageProperty(page) && page->property == 0));
            }
            ppn_t partner_ppn = page2ppn(base) ^ count;
            if (order < BUDDY_MAX_ORDER && partner_ppn >= nbase &&
                partner_ppn < npage) {
                struct Page *partner = pages + (partner_ppn - nbase);
                assert(!PageProperty(partner) || partner->property != count);
            }
        }
    }
    assert(total == nr_free);
}

/* Self-tests use private descriptors, never memory owned by a live caller.
 * Only metadata is exercised: no physical address from this arena is read.
 * Keep the arrays off the small kernel stack. */
#define BUDDY_TEST_PAGES 64
static struct Page buddy_test_pages[BUDDY_TEST_PAGES];
static struct Page buddy_test_snapshot[BUDDY_TEST_PAGES];

/* Move a ring and repair its sentinel links; copying a head alone is unsafe. */
static void buddy_test_move_lists(list_entry_t *dest, list_entry_t *source) {
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        assert(list_empty(&dest[order]));
        if (!list_empty(&source[order])) {
            dest[order].next = source[order].next;
            dest[order].prev = source[order].prev;
            dest[order].next->prev = &dest[order];
            dest[order].prev->next = &dest[order];
            list_init(&source[order]);
        }
    }
}

static struct Page *buddy_test_reset(size_t count) {
    /* Align by physical page number, including when nbase itself is odd. */
    size_t offset = (16 - (nbase & 15)) & 15;
    assert(count <= 16 && buddy_nr_free_pages() == 0);
    buddy_init();
    for (size_t i = 0; i < BUDDY_TEST_PAGES; ++i) {
        struct Page *page = &buddy_test_pages[i];
        page->flags = 0;
        SetPageReserved(page);
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);
    }
    struct Page *base = buddy_test_pages + offset;
    buddy_init_memmap(base, count);
    assert(buddy_nr_free_pages() == count);
    check_invariants();
    return base;
}

/* All scenario allocations must be released before retiring the test arena. */
static void buddy_test_finish(size_t count) {
    assert(buddy_nr_free_pages() == count);
    check_invariants();
    buddy_init();
}

static void buddy_test_expect_failure(size_t request) {
    list_entry_t saved_heads[BUDDY_MAX_ORDER + 1];
    size_t before = buddy_nr_free_pages();
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        saved_heads[order] = free_lists[order];
    }
    for (size_t i = 0; i < BUDDY_TEST_PAGES; ++i) {
        buddy_test_snapshot[i] = buddy_test_pages[i];
    }
    assert(buddy_alloc_pages(request) == NULL);
    assert(buddy_nr_free_pages() == before);
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        assert(free_lists[order].next == saved_heads[order].next);
        assert(free_lists[order].prev == saved_heads[order].prev);
    }
    for (size_t i = 0; i < BUDDY_TEST_PAGES; ++i) {
        struct Page *page = &buddy_test_pages[i];
        struct Page *saved = &buddy_test_snapshot[i];
        assert(page->ref == saved->ref && page->flags == saved->flags);
        assert(page->property == saved->property);
        assert(page->page_link.next == saved->page_link.next);
        assert(page->page_link.prev == saved->page_link.prev);
    }
    check_invariants();
}

static void buddy_check(void) {
    check_invariants();
    struct Page *saved_pages = pages;
    size_t saved_npage = npage;
    size_t before = buddy_nr_free_pages();
    list_entry_t saved_lists[BUDDY_MAX_ORDER + 1];
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; ++order) {
        list_init(&saved_lists[order]);
    }
    buddy_test_move_lists(saved_lists, free_lists);
    pages = buddy_test_pages;
    npage = nbase + BUDDY_TEST_PAGES;
    buddy_init();

    /* 1. Basic allocation and exact accounting. */
    struct Page *base = buddy_test_reset(16);
    struct Page *page = buddy_alloc_pages(1);
    assert(page != NULL && page->property == 1 && !PageProperty(page));
    assert(page >= base && page < base + 16);
    assert(buddy_nr_free_pages() == 15);
    buddy_free_pages(page, 1);
    buddy_test_finish(16);

    /* 2. Free with the original request, not the rounded block size. */
    base = buddy_test_reset(16);
    page = buddy_alloc_pages(3);
    assert(page != NULL && page->property == 4 && !PageProperty(page));
    assert((page2ppn(page) & 3) == 0);
    assert(buddy_nr_free_pages() == 12);
    buddy_free_pages(page, 3);
    buddy_test_finish(16);

    /* 3. A single 16-page block must split and recursively reassemble. */
    base = buddy_test_reset(16);
    struct Page *left = buddy_alloc_pages(1);
    struct Page *right = buddy_alloc_pages(1);
    assert(left == base && right == base + 1);
    assert(buddy_nr_free_pages() == 14);
    buddy_free_pages(left, 1);
    assert(buddy_nr_free_pages() == 15);
    buddy_free_pages(right, 1);
    assert(buddy_nr_free_pages() == 16);
    page = buddy_alloc_pages(16);
    assert(page == base && buddy_nr_free_pages() == 0);
    buddy_free_pages(page, 16);
    buddy_test_finish(16);

    /* 4. Pages 1 and 2 are adjacent, but their buddies are 0 and 3. */
    base = buddy_test_reset(4);
    struct Page *singles[4] = {NULL, NULL, NULL, NULL};
    for (size_t i = 0; i < 4; ++i) {
        page = buddy_alloc_pages(1);
        assert(page != NULL && page >= base && page < base + 4);
        size_t offset = page - base;
        assert(singles[offset] == NULL);
        singles[offset] = page;
    }
    assert(buddy_nr_free_pages() == 0);
    buddy_free_pages(singles[1], 1);
    buddy_free_pages(singles[2], 1);
    assert(buddy_nr_free_pages() == 2);
    buddy_test_expect_failure(2);
    buddy_free_pages(singles[0], 1);
    page = buddy_alloc_pages(2);
    assert(page == base && buddy_nr_free_pages() == 1);
    buddy_free_pages(page, 2);
    buddy_free_pages(singles[3], 1);
    page = buddy_alloc_pages(4);
    assert(page == base && buddy_nr_free_pages() == 0);
    buddy_free_pages(page, 4);
    buddy_test_finish(4);

    /* 5. Failed requests must preserve every descriptor and list link. */
    base = buddy_test_reset(4);
    buddy_test_expect_failure(0);
    buddy_test_expect_failure((size_t)-1);
    buddy_test_expect_failure(5);
    for (size_t i = 0; i < 4; ++i) {
        singles[i] = buddy_alloc_pages(1);
        assert(singles[i] != NULL);
    }
    assert(buddy_nr_free_pages() == 0);
    buddy_test_expect_failure(1);
    for (size_t i = 0; i < 4; ++i) {
        buddy_free_pages(singles[i], 1);
    }
    buddy_test_finish(4);

    /* 6. Mixed blocks coexist without overlap and tolerate unordered frees. */
    base = buddy_test_reset(16);
    const size_t requests[] = {1, 8, 4, 2, 1};
    const size_t release_order[] = {3, 0, 4, 1, 2};
    struct Page *blocks[5];
    size_t remaining = 16;
    for (size_t i = 0; i < 5; ++i) {
        blocks[i] = buddy_alloc_pages(requests[i]);
        assert(blocks[i] != NULL && !PageProperty(blocks[i]));
        assert(blocks[i]->property == requests[i]);
        assert(blocks[i] >= base && blocks[i] + requests[i] <= base + 16);
        assert((page2ppn(blocks[i]) & (requests[i] - 1)) == 0);
        for (size_t j = 0; j < i; ++j) {
            assert(blocks[i] + requests[i] <= blocks[j] ||
                   blocks[j] + requests[j] <= blocks[i]);
        }
        remaining -= requests[i];
        assert(buddy_nr_free_pages() == remaining);
        check_invariants();
    }
    for (size_t i = 0; i < 5; ++i) {
        size_t index = release_order[i];
        buddy_free_pages(blocks[index], requests[index]);
        remaining += requests[index];
        assert(buddy_nr_free_pages() == remaining);
        check_invariants();
    }
    page = buddy_alloc_pages(16);
    assert(page == base && buddy_nr_free_pages() == 0);
    buddy_free_pages(page, 16);
    buddy_test_finish(16);

    pages = saved_pages;
    npage = saved_npage;
    buddy_test_move_lists(free_lists, saved_lists);
    nr_free = before;
    assert(buddy_nr_free_pages() == before);
    check_invariants();
    cprintf("buddy_check: all 6 scenarios passed; allocator state restored.\n");
    cprintf("buddy_check() succeeded!\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

# 操作系统实验报告

## 实验基本信息

| 项目       | 内容                          |
| -------- | --------------------------- |
| **实验名称** | Lab 2: [物理内存和页表]            |
| **小组成员** | 2411189-李梓宸、2411584-马锐、2412483-李梦涛 |
| **完成日期** | 2026-10-25                  |

### 小组分工

练习如何分工？

| 成员      | 负责的练习/模块  |
| ------- | --------- |
| 2411189-李梓宸 | 功能模块二（First-Fit 分析、Best-Fit 实现）与 Challenge |
| 2411584-马锐 | Buddy System、完整工程与页表查询调试 |
| 2412483-李梦涛 | 分支任务：TLB 查询、回填、命中路径及与真实 CPU TLB 的对比 |

实验报告如何分工？

---

## 一、实验目的

<!-- 说明本次实验的主要目标和预期成果 -->

本实验的主要目的是：

1. [目的1：例如，理解用户态进程如何通过系统调用进入内核]
2. [目的2：例如，掌握进程创建、执行、退出的完整生命周期]
3. [目的3：例如，学习使用 AI 辅助开发操作系统核心功能]

---

## 二、实验环境

你们使用的 AI 工具

| 成员      | AI 编程工具                                                           | 底层模型                                                  | 备注         |
| ------- | ----------------------------------------------------------------- | ----------------------------------------------------- | ---------- |
| 学号1-姓名1 | [例如：Cline（VS Code插件） / Claude Code（终端 Agent） / DeepSeek Harness等] | [例如：Claude Sonnet 4.5 / GPT-5.5 / DeepSeek-V4-Flash等] | [可选：特殊配置等] |
| 学号2-姓名2 |                                                                   |                                                       |            |
| 学号3-姓名3 |                                                                   |                                                       |            |

**说明：**

- **AI 编程工具**：指具体使用的终端工具、编辑器插件、桌面应用或浏览器界面
- **底层模型**：指该工具使用的大语言模型及版本

---

## 三、实验整体逻辑分析

### 2.1 本章节的逻辑主线

本章围&#x7ED5;__&#x7269;理内存管&#x7406;__&#x8FD9;一核心主题展开，解决的问题是：操作系统启动后，如何把硬件提供的物理内存转化为内核可管理、可分配、可回收的资源，并在页级分配的基础上进一步支持小对象的高效分配。

整章的逻辑主线可以概括为：

1. __先建立物理内存的描述体系__：通过 `page_init()` 解析设备树报告的内存范围，建立 `struct Page` 描述符数组，区分内核已占用区域和真正可用的空闲页，为后续所有分配器提供统一的页管理基础。
2. __在页级实现 Best-Fit 分配器__：有了空闲页之后，需要一套具体的分配和回收策略。Best-Fit 通过“选择满足请求的最小空闲块”，减少大块内存被小请求浪费的情况，并完成空闲块的拆分与相邻合并。
3. __进一步实现 Buddy System 分配器__：在相同的 `pmm_manager` 接口下，用 `2^k` 页的空闲块组织方式实现按 order 拆分和合并。它和 Best-Fit 是两种不同的页分配策略，但共享同一套页级抽象，因此可以在不改变上层调用方式的情况下切换。
4. __最后实现 SLUB 对象分配器__：页分配器解决的是“按页分配”，但内核中的很多对象远小于一页。SLUB 在页分配器之上增加第二层对象缓存，用 slab 管理固定大小的小对象，并为超过缓存上限的请求提供页级大对象回退，从而形成“页级分配 → 对象级分配”的完整层次。

因此，本章不是孤立地实现几个分配算法，而是按照“__物理内存初始化 → 页级分配策略 → 对象级分配策略__”的顺序，逐步构建一个分层的物理内存管理体系。这一章也为后续虚拟内存管理、进程创建和内核对象管理提供了基础。

### 2.2 功能的逐步实现

本章实验按照以下顺序逐步实现各功能：

1. __首先实现 `page_init()` 物理页初始化__\
   这是整个内存管理的前提。只有先根据 DTB 获取物理内存范围，建立 `struct Page` 数组，并把内核镜像、OpenSBI、页描述符数组等区域标记为保留页，才能得到安全、准确的空闲页集合。没有这一步，后续 Best-Fit 和 Buddy 都没有可以管理的物理页。

2. __接着实现 Best-Fit 页分配器__\
   在 `page_init()` 建立空闲页链表之后，需要提供具体的页分配和释放能力。Best-Fit 通过维护按地址有序的空闲块链表，在分配时选择“容量最小且满足请求”的空闲块，并在释放时与前后相邻空闲块合并。这样既完成了 Lab2 的基础页分配功能，也通过 `best_fit_check()` 验证了页初始化结果和分配器行为是否正确。

3. __然后实现 Buddy System 页分配器__\
   Best-Fit 验证通过后，页级 `pmm_manager` 接口已经稳定，因此可以在同一接口下实现另一种页分配策略。Buddy 按 `2^k` 页组织空闲块，利用物理页号的异或关系寻找 buddy，在分配时逐层拆分，在释放时逐层合并。把它放在 Best-Fit 之后实现，是为了在已有页级抽象和测试框架的基础上，专注于 order 管理、地址对齐和拆分/合并逻辑，而不必重复处理物理内存初始化问题。

4. __最后实现 SLUB 对象分配器__\
   SLUB 依赖前两层提供的 `alloc_pages()` 和 `free_pages()`，因此必须放在页分配器之后实现。它先为 8、16、32、64、128、256 字节建立固定大小缓存，每个 slab 从页分配器申请一页，用空闲对象链表和状态位图管理对象；当整个 slab 空闲时再把页归还给页分配器。对于超过 256 字节的请求，则通过连续多页的大对象路径处理。这样就在页级分配之上补齐了对象级分配，使整个内存管理模块形成从物理页到内核对象的完整闭环。

5. __在每一层完成后同步进行测试__\
   `page_init()` 完成后通过 `[Test 1] Page Init` 检查内存范围和保留页；Best-Fit 完成后通过 `best_fit_check()` 和 `make grade` 验证；Buddy 完成后通过 `buddy_check()` 验证拆分、合并、对齐和 OOM；SLUB 完成后通过 `slub_check()` 验证大小类、slab 扩展、页回收、对象复用和大对象分配。按照“实现一层、验证一层”的顺序推进，可以尽早发现底层页管理错误，避免问题累积到上层对象分配阶段。

---

## 四、实验内容与实现

<!-- 
说明：本部分按照 exercises 文档中的练习顺序组织
每个功能模块或者练习中可能包含多项任务，请根据实际情况调整
-->

# 四、实验内容与实现

## 模块一：物理页初始化 `page_init`

负责人：`2411189-李梓宸`

### 模块功能描述

**需要实现/修改的函数：**

```c
static void page_init(void);

/* 调用链中涉及的相关函数 */
static void init_pmm_manager(void);
static void init_memmap(struct Page *base, size_t n);
static void check_alloc_page(void);
void pmm_init(void);
```

本次主要实现 `page_init()`，并将 `init_pmm_manager()` 中的页分配器切换为：

```c
pmm_manager = &best_fit_pmm_manager;
```

**功能说明：**

1. **在系统中的作用**

   `page_init()` 负责根据设备树 DTB 报告的物理内存范围，建立 `struct Page` 描述符数组，并把内核、OpenSBI、页描述符数组之外的内存登记为空闲页，供 Best-Fit 或 Buddy 页分配器管理。

2. **需要处理的主要场景**

   - 从 DTB 获取物理内存基址和大小；
   - 检查物理内存范围是否非法或溢出；
   - 计算 `npage`、`va_pa_offset`；
   - 将 `pages` 数组放在内核 `end` 之后，并保证页对齐；
   - 将 OpenSBI、内核镜像、`pages` 数组本身标记为 `PageReserved`；
   - 只将内核和页描述符数组之后的完整页登记为空闲内存；
   - 保证 `page2pa()` / `pa2page()` 往返转换正确。

3. **与其他模块的交互关系**

   ```text
   DTB
    ↓
   page_init()
    ↓
   pmm_manager->init_memmap()
    ↓
   Best-Fit / Buddy 空闲页链表
    ↓
   alloc_pages() / free_pages()
    ↓
   SLUB 通过页分配器申请 slab 页
   ```

### 最终提示词

```text
[ROLE]
你是一名 uCore/RISC-V 内核开发者。请实现 Lab2 的物理页初始化函数 page_init()。

[CONTEXT]
代码位于 kern/mm/pmm.c。系统运行在 RISC-V 的 virt 机器上，物理内存起始地址为
DRAM_BASE=0x80000000。内核镜像加载在 KERNEL_BEGIN_PADDR，内核虚拟地址与物理地址
之间存在 PHYSICAL_MEMORY_OFFSET 偏移。已有 pages、npage、nbase、va_pa_offset 等全局变量，
以及 init_memmap()、alloc_pages()、free_pages()、nr_free_pages() 等封装函数。

[SPECIFICATION]
请实现 page_init()，要求：
1. 使用 get_memory_base() 和 get_memory_size() 获取 DTB 报告的物理内存范围；
2. 校验 mem_size 非 0、mem_base 位于 DRAM_BASE 之后、mem_end 不溢出；
3. 设置 npage = PPN(mem_end)；
4. 设置 va_pa_offset = PHYSICAL_MEMORY_OFFSET；
5. 将 pages 数组放在内核 end 之后，并向上按 PGSIZE 对齐；
6. 计算 pages 数组占用的页数和结束物理地址，确保它不超出物理内存末尾；
7. 初始时把 [nbase, npage) 范围内所有 Page 描述符都标记为 PageReserved；
8. 第一个可空闲页从 pages 数组结束地址开始，不能把内核镜像、OpenSBI 或 pages 数组交给分配器；
9. 如果存在至少一个完整空闲页，调用 init_memmap(pa2page(free_start_pa), free_pages)；
10. 对非法物理内存范围、页描述符数组放不下等情况使用 panic() 报错；
11. 输出物理内存范围、npage、pages 地址、空闲内存范围等信息。

[RELY]
可以使用：
- get_memory_base() / get_memory_size()
- DRAM_BASE、KERNEL_BEGIN_PADDR、PHYSICAL_MEMORY_OFFSET
- PPN()、PADDR()、ROUNDUP()、PGSIZE
- pages、npage、nbase、va_pa_offset
- pa2page()、init_memmap()
- SetPageReserved()、PageReserved()

[CONSTRAINTS]
- 不要修改 alloc_pages/free_pages 的封装逻辑；
- 不要修改 basic_check/best_fit_check；
- 必须处理空内存、内存范围溢出、页描述符数组越界等边界；
- 不要使用硬编码的物理内存大小，必须以 DTB 信息为准。

[TEST]
测试函数 test_page_init() 会检查：
- va_pa_offset 是否正确；
- DTB 内存范围是否非 0；
- npage 是否等于 PPN(mem_end)；
- pages 是否非空、页对齐且位于内核 end 之后；
- 空闲页数量是否合理；
- 内核尾部页和 pages 数组页是否保持 Reserved；
- page2pa/pa2page 是否可往返转换。

[OUTPUT]
只输出对 kern/mm/pmm.c 的完整修改，不要省略错误处理。
```

### 实现迭代过程

本模块的实现经历了 **2 次迭代**，过程如下：

---

#### 第一次迭代

**遇到的问题：**

- 问题 1：初版提示词只强调“初始化物理页”，生成代码直接使用固定内存范围，没有以 DTB 的 `get_memory_base()` / `get_memory_size()` 为准，导致 `npage` 和实际物理内存不一致。
- 问题 2：没有明确区分“内核结束地址”和“页描述符数组结束地址”，存在把 `pages` 数组所在页错误登记为空闲页的风险。
- 问题 3：没有强调内核镜像、OpenSBI 和 `pages` 数组必须保持 `PageReserved`，Test 1 的保留页检查无法保证通过。

**问题解决策略：**

- 针对问题 1：在 `[SPECIFICATION]` 中明确要求从 DTB 获取内存范围，并计算 `mem_end`、`npage`、`free_start_pa`。
- 针对问题 2：在 `[SPECIFICATION]` 中分别定义 `pages_va`、`pages_end_va`、`pages_end_pa`，明确第一个空闲页从 `pages_end_pa` 开始。
- 针对问题 3：在 `[RELY]` 和 `[TEST]` 中补充 `PageReserved`、内核尾部页、`pages` 数组页的检查要求。

---

#### 第二次迭代

第二次提示词明确加入了：

- DTB 内存范围校验；
- `pages` 数组的位置和大小计算；
- 内核、OpenSBI、页描述符数组的保留策略；
- 空闲页起始地址的计算方法；
- `page2pa` / `pa2page` 往返检查。

**最终结果：**

经过 **2 次迭代**，该模块最终：

- ✅ 编译通过；
- ✅ `[Test 1] Page Init: PASS`；
- ✅ 后续 `best_fit_check()` 和 SLUB 都能正常使用页分配器。

**关键改进点总结：**

1. 在 `[SPECIFICATION]` 中补充了**物理内存范围校验和溢出检查**；
2. 在 `[RELY]` 中明确列出 `PPN`、`PADDR`、`ROUNDUP`、`pa2page` 等依赖；
3. 在 `[TEST]` 中把 `va_pa_offset`、`npage`、`pages` 对齐、保留页、地址往返转换全部写成可验证条件。

---

## 模块二：Best-Fit 页分配器

负责人：`2411189-李梓宸`

### 模块功能描述

**需要实现/修改的函数：**

```c
static void best_fit_init(void);

static void best_fit_init_memmap(struct Page *base, size_t n);

static struct Page *best_fit_alloc_pages(size_t n);

static void best_fit_free_pages(struct Page *base, size_t n);

static size_t best_fit_nr_free_pages(void);

/* 框架测试函数，本次未修改 */
static void basic_check(void);
static void best_fit_check(void);

/* 页分配器注册对象 */
const struct pmm_manager best_fit_pmm_manager;
```

**功能说明：**

1. **在系统中的作用**

   Best-Fit 负责管理所有空闲物理页。所有空闲块按物理页地址顺序挂在 `free_area.free_list` 上，每个空闲块的头页通过 `property` 记录块大小。

2. **需要处理的主要场景**

   - 初始化空闲链表；
   - 将一段连续空闲内存登记为若干空闲块；
   - 分配时扫描所有空闲块，选择**容量最小且满足 `property >= n`** 的块；
   - 如果块比请求大，则从块首分配 `n` 页，剩余部分形成新的空闲块；
   - 释放时按地址顺序插入空闲链表；
   - 释放后与前后相邻空闲块合并；
   - 维护 `nr_free` 总量，保证 `sum(property) == nr_free_pages()`。

3. **与其他模块的交互关系**

   ```text
   pmm_manager
      ↓
   best_fit_pmm_manager
      ↓
   best_fit_alloc_pages() / best_fit_free_pages()
      ↓
   struct Page.property、page_link、flags
      ↓
   SLUB 通过 alloc_pages() 申请 slab 页
   ```

4. **Best-Fit 的关键选择逻辑**

   测试中有如下场景：

   ```text
   空闲块： [p0+1, 2页]  [p0+4, 1页]
   ```

   此时请求 1 页，Best-Fit 必须选择 `p0+4` 的 1 页块，而不是地址更靠前的 2 页块，因此测试要求：

   ```c
   assert((p1 = alloc_pages(1)) != NULL);
   assert(alloc_pages(2) != NULL);
   assert(p0 + 4 == p1);
   ```

### 最终提示词

```text
[ROLE]
你是一名 uCore 内核开发者。请在 kern/mm/best_fit_pmm.c 中实现 Best-Fit 物理页分配器。

[CONTEXT]
已有 free_area_t、free_list、nr_free、struct Page、page_link、property、flags 等基础设施。
struct Page 的头页用 property 表示空闲块包含的页数，PageProperty(page) 表示该页是否是
一个空闲块的头页。空闲链表必须按物理页地址升序排列。

[SPECIFICATION]
请实现以下函数：

1. best_fit_init()
   - 初始化 free_list；
   - nr_free = 0。

2. best_fit_init_memmap(struct Page *base, size_t n)
   - 断言 n > 0；
   - 对 [base, base+n) 中每一页断言 PageReserved(p)；
   - 清除 flags、property，设置 ref=0；
   - 将 base 作为空闲块头页，property=n，SetPageProperty(base)；
   - 按物理地址顺序插入 free_list；
   - nr_free += n。

3. best_fit_alloc_pages(size_t n)
   - 断言 n > 0；
   - 如果 n > nr_free，返回 NULL；
   - 遍历整个 free_list，选择 property >= n 且 property 最小的块；
   - 如果没有合适块，返回 NULL；
   - 从链表中删除选中块；
   - 如果块大于 n，将 page+n 开始的剩余部分作为新空闲块，property = 原大小 - n，
     按地址顺序插回原位置之后；
   - nr_free -= n；
   - 清除分配块头页的 PageProperty，property=0，flags=0，ref=0；
   - 返回分配块的头页。

4. best_fit_free_pages(struct Page *base, size_t n)
   - 断言 n > 0；
   - 对 [base, base+n) 中每一页断言 !PageReserved(p) 且 !PageProperty(p)；
   - 清除 flags，设置 ref=0；
   - 设置 base->property=n，SetPageProperty(base)；
   - nr_free += n；
   - 按物理地址顺序插入 free_list；
   - 尝试与前一个空闲块合并：若 p + p->property == base，则合并；
   - 尝试与后一个空闲块合并：若 base + base->property == p，则合并；
   - 合并时必须正确清除被合并块头页的 PageProperty 并从链表删除。

5. best_fit_nr_free_pages()
   - 返回 nr_free。

[RELY]
可以使用：
- struct Page、page_link、property、flags、ref
- PageProperty()、SetPageProperty()、ClearPageProperty()
- PageReserved()
- list_init、list_empty、list_next、list_prev、list_add、list_add_before、list_del
- le2page()
- assert()
- nr_free_pages()

[CONSTRAINTS]
- 不要修改 basic_check() 和 best_fit_check()；
- 空闲链表必须保持物理地址升序；
- 分配必须选择“最小满足块”，不能退化成 first-fit；
- 释放后必须同时尝试向前和向后合并；
- 注意维护 nr_free，不能只修改链表不修改计数；
- 不要引入额外的页分配层级。

[TEST]
best_fit_check() 会检查：
- sum(property) 是否等于 nr_free_pages()；
- 基本 alloc/free 是否正确；
- 空闲块不足时 alloc 是否返回 NULL；
- 释放 p0+1 的 2 页和 p0+4 的 1 页后，alloc_pages(1) 是否返回 p0+4；
- alloc_pages(2) 是否成功；
- 释放 p0 开始 5 页后能否重新分配 5 页；
- 释放后 nr_free 和空闲块总页数是否恢复。

[OUTPUT]
输出 best_fit_pmm.c 的完整实现，保持现有框架和测试函数不变。
```

### 实现迭代过程

本模块的实现经历了 **3 次迭代**，过程如下：

---

#### 第一次迭代

**遇到的问题：**

- 问题 1：初版提示词只写了“实现 Best-Fit”，生成代码实际按链表顺序选择第一个满足条件的块，效果接近 First-Fit；
- 问题 2：分配时没有正确拆分剩余空闲块，导致 `property` 和 `nr_free` 不一致；
- 问题 3：释放时只插入了链表，没有与前后空闲块合并，`best_fit_check()` 中关于连续内存恢复的断言失败。

**问题解决策略：**

- 针对问题 1：在 `[SPECIFICATION]` 中明确写出“遍历全部空闲块，选择 `property >= n` 且 `property` 最小的块”。
- 针对问题 2：补充“从 `page+n` 开始拆分剩余部分，并插回原块位置之后”的规则。
- 针对问题 3：补充向前合并和向后合并两个条件，并要求同步维护 `nr_free`。

---

#### 第二次迭代

第二次生成代码后，分配路径基本正确，但仍存在：

- 问题 1：释放块插入位置不正确，破坏了空闲链表的地址顺序；
- 问题 2：向前合并后，`base` 指针没有更新为前一个块，导致后续向后合并判断错误；
- 问题 3：清除 `PageProperty` 的位置不完整，测试中的 `PageProperty` 断言失败。

**问题解决策略：**

- 在 `[SPECIFICATION]` 中明确要求“空闲链表按物理地址升序”；
- 在 `[RELY]` 中列出 `list_add_before`、`list_prev`、`list_next`、`list_del` 的正确使用方式；
- 在 `[TEST]` 中增加“释放后 `sum(property)` 必须等于 `nr_free`”的约束。

---

#### 第三次迭代

第三次迭代后，Best-Fit 算法和测试均通过。随后发现测试环境问题：

- `best_fit_check()` 本身通过，但 `make grade` 一度显示 0 分；
- 原因是本机 QEMU/OpenSBI 对 `tools/grade.sh` 中的 `-device loader,...,addr=0x80200000` 启动方式不兼容；
- 将评分脚本改为：

```bash
-kernel $osimg
```

之后 `make grade` 正常得到：

```text
-check page init:                          OK
-check best fit:                           OK
Total Score: 20/20
```

**最终结果：**

经过 **3 次迭代**，该模块最终：

- ✅ 编译通过；
- ✅ `best_fit_check() succeeded!`；
- ✅ `[Test 1] Page Init: PASS`；
- ✅ `[Test 2] Best-Fit: PASS`；
- ✅ `make grade`：**20/20**。

**关键改进点总结：**

1. 在 `[SPECIFICATION]` 中把“Best-Fit”细化成**最小满足块选择、拆分、双向合并、地址有序**四个可验证条件；
2. 在 `[RELY]` 中补充 `PageProperty`、`list_prev`、`list_add_before` 等链表和页标志接口；
3. 在 `[TEST]` 中显式写出 `p0+4 == p1` 的最小适配测试场景，避免模型生成 First-Fit。

---

## 模块三：Buddy System 页分配器

负责人：`2411189-李梓宸`

### 模块功能描述

**需要实现/修改的函数：**

```c
static unsigned int buddy_order(size_t n);

static struct Page *buddy_of(struct Page *page, unsigned int order);

static void buddy_list_insert(struct Page *base, unsigned int order);

static void buddy_add_free_block(struct Page *base, unsigned int order);

static void buddy_remove_free_block(struct Page *base, unsigned int order);

static void buddy_insert_and_merge(struct Page *base, unsigned int order);

static void buddy_init(void);

static void buddy_init_memmap(struct Page *base, size_t n);

static struct Page *buddy_alloc_pages(size_t n);

static void buddy_free_pages(struct Page *base, size_t n);

static size_t buddy_nr_free_pages(void);

static void buddy_check_lists(void);

static void buddy_check(void);

const struct pmm_manager buddy_pmm_manager;
```

**功能说明：**

1. **在系统中的作用**

   Buddy System 按 2 的幂次管理空闲页。`MAX_ORDER = 20`，`free_area[k]` 管理所有大小为 `2^k` 页的空闲块。分配时向上取整到合适的 order，找不到时向更高 order 查找并逐层拆分；释放时与空闲的 buddy 反复合并。

2. **需要处理的主要场景**

   - 初始化 `MAX_ORDER` 个空闲链表；
   - 将初始物理内存拆成自然对齐的 2 的幂次块；
   - 请求页数不是 2 的幂时向上取整；
   - 从更高 order 的块中拆分；
   - 释放时与 buddy 合并；
   - 处理超出最大 order 或空闲内存不足的请求；
   - 维护每个 order 的空闲块数量和总空闲页数；
   - 检查空闲块的地址对齐、`property` 和链表一致性。

3. **与其他模块的交互关系**

   ```text
   page_init()
      ↓
   buddy_init_memmap()
      ↓
   free_area[0..MAX_ORDER-1]
      ↓
   buddy_alloc_pages() / buddy_free_pages()
      ↓
   SLUB 的 slab 页分配
   ```

4. **核心不变量**

   - order 为 `k` 的空闲块包含 `2^k` 页；
   - 空闲块起始物理页号必须按 `2^k` 对齐；
   - `buddy_of(page, k)` 的页号为：

     ```c
     ppn ^ (1 << k)
     ```

   - 每个空闲块头页满足 `PageProperty(page)`；
   - 每个 order 的 `nr_free` 等于该链表中的块数；
   - 所有 order 的 `nr_free` 总和等于 `buddy_nr_free_pages()`。

### 最终提示词

```text
[ROLE]
你是一名 uCore 内核开发者。请在 kern/mm/buddy_pmm.c 中实现 Buddy System 物理页分配器。

[CONTEXT]
已有 struct Page、page_link、property、flags、PageProperty()、nbase、npage、pages、
page2ppn() 等基础设施。MAX_ORDER 定义为 20，order k 管理 2^k 页的空闲块。
Buddy 分配器通过 struct pmm_manager 注册为 buddy_pmm_manager。

[SPECIFICATION]
请实现：

1. buddy_order(size_t n)
   - 返回能容纳 n 页的最小 order；
   - 如果 n 超过最大可管理块，返回 MAX_ORDER。

2. buddy_of(struct Page *page, unsigned int order)
   - 计算 page 的物理页号 ppn；
   - buddy_ppn = ppn ^ (1 << order)；
   - 如果 buddy_ppn 不在 [nbase, npage) 范围内，返回 NULL；
   - 否则返回 &pages[buddy_ppn - nbase]。

3. buddy_list_insert / buddy_add_free_block / buddy_remove_free_block
   - 每个 order 使用独立空闲链表；
   - 插入时保持物理地址升序；
   - 添加空闲块时设置 property=2^order、SetPageProperty、ref=0；
   - 删除空闲块时 ClearPageProperty、property=0，并更新该 order 的 nr_free；
   - 所有操作必须同步维护全局 nr_free。

4. buddy_insert_and_merge(struct Page *base, unsigned int order)
   - 将 base 作为 2^order 页的空闲块；
   - 当 order+1 < MAX_ORDER 时，检查其 buddy 是否为空闲块且大小相同；
   - 如果 buddy 可合并，从空闲链表删除 buddy；
   - 取两者中地址较小的作为新的 base；
   - order++，size 翻倍，继续尝试合并；
   - 最后将合并后的块加入对应 order 的空闲链表。

5. buddy_init()
   - 初始化所有 order 的空闲链表和 nr_free；
   - 全局 nr_free = 0。

6. buddy_init_memmap(struct Page *base, size_t n)
   - 断言 n > 0；
   - 对 [base, base+n) 中每一页断言 PageReserved；
   - 清除 flags/property，设置 ref=0；
   - 将区域拆成自然对齐的 2 的幂次块；
   - 每个块调用 buddy_insert_and_merge()。

7. buddy_alloc_pages(size_t n)
   - 断言 n > 0；
   - 计算请求 order；
   - 从该 order 向上寻找第一个非空链表；
   - 如果找不到，返回 NULL；
   - 从找到的块中取出一个块；
   - 逐层拆分，把每次拆分出的高半部分作为低一阶空闲块加入链表；
   - 清除最终分配块头页的 PageProperty、property、flags；
   - 返回分配块头页。

8. buddy_free_pages(struct Page *base, size_t n)
   - 断言 n > 0；
   - 计算 order 和实际释放页数 2^order；
   - 对释放范围内每一页断言 !PageReserved 且 !PageProperty；
   - 清除 flags/property，设置 ref=0；
   - 调用 buddy_insert_and_merge() 执行合并。

9. buddy_nr_free_pages()
   - 返回全局 nr_free。

10. buddy_check_lists()
   - 遍历所有 order 的链表；
   - 检查每个块 PageProperty、非 Reserved、ref=0；
   - 检查 property == 2^order；
   - 检查起始页号按 2^order 对齐；
   - 检查块范围位于 [nbase, npage)；
   - 检查每个 order 的块数等于 nr_free；
   - 检查所有块页数总和等于全局 nr_free。

[RELY]
可以使用：
- struct Page、page_link、property、flags、ref
- PageProperty()、SetPageProperty()、ClearPageProperty()
- PageReserved()
- page2ppn()、nbase、npage、pages
- list_init、list_empty、list_next、list_add_before、list_del
- le2page()
- assert()

[CONSTRAINTS]
- 不要修改页分配器框架和 pmm_manager 接口；
- 每个 order 的链表必须按物理地址升序；
- 拆分时必须把高半部分加入低一阶链表；
- 合并时必须只与“同 order 且空闲”的 buddy 合并；
- 必须处理超出 MAX_ORDER 和空闲内存不足的情况；
- 注意维护每个 order 的 nr_free 和全局 nr_free。

[TEST]
buddy_check() 会检查：
- 基本 1 页 alloc/free；
- alloc_pages(3) 必须实际占用 4 页，且返回页号按 4 页对齐；
- 清空所有 order-0 空闲块后，alloc_pages(1) 必须能触发大块拆分；
- 两次相互 buddy 的 1 页分配释放后必须合并，order-0 链表应重新为空；
- 申请超过当前空闲页数必须返回 NULL，且 nr_free 不变；
- 所有 order 的链表、property、对齐和总页数必须满足不变量。

[OUTPUT]
输出 buddy_pmm.c 的完整实现，并注册 buddy_pmm_manager。
```

### 实现迭代过程

本模块的实现经历了 **3 次迭代**，过程如下：

---

#### 第一次迭代

**遇到的问题：**

- 问题 1：初版提示词没有明确“order k 的块必须按 2^k 页对齐”，生成代码直接按请求页数切分，`alloc_pages(3)` 没有按 4 页对齐；
- 问题 2：分配时只从当前 order 取块，没有向更高 order 查找并拆分；
- 问题 3：释放时只把块插回链表，没有与 buddy 合并。

**问题解决策略：**

- 针对问题 1：在 `[SPECIFICATION]` 中明确 `order k`、`2^k` 页、起始页号按 `2^k` 对齐；
- 针对问题 2：补充“从请求 order 向上查找第一个非空链表，再逐层拆分”的流程；
- 针对问题 3：补充 `buddy_of()` 的 XOR 计算公式和 `buddy_insert_and_merge()` 的循环合并逻辑。

---

#### 第二次迭代

第二次生成代码可以进行基本分配，但仍有问题：

- 问题 1：拆分时把低半部分加入了空闲链表，应该保留低半部分作为当前分配块，把高半部分作为新的空闲块；
- 问题 2：合并时没有检查 buddy 的 `property` 是否等于当前块大小，可能把不同大小的块错误合并；
- 问题 3：`nr_free` 在拆分和合并过程中被重复加减。

**问题解决策略：**

- 在 `[SPECIFICATION]` 中明确“每次拆分出的高半部分加入低一阶空闲链表”；
- 在合并条件中加入 `buddy->property == size`；
- 在 `[CONSTRAINTS]` 中要求每个 order 的 `nr_free` 与链表块数同步维护。

---

#### 第三次迭代

第三次生成代码后：

- `buddy_order()` 正确向上取整；
- `buddy_of()` 正确使用 `ppn ^ (1 << order)`；
- `alloc_pages(3)` 占用 4 页且地址按 4 页对齐；
- 两次 order-0 分配释放后能够合并到更高 order；
- `buddy_check_lists()` 的链表、对齐、范围和总页数检查全部通过。

**最终结果：**

经过 **3 次迭代**，该模块最终：

- ✅ 编译通过；
- ✅ `[buddy_check] basic alloc/free: passed`
- ✅ `[buddy_check] order alignment (3 -> 4 pages): passed`
- ✅ `[buddy_check] block split: passed`
- ✅ `[buddy_check] buddy merge: passed`
- ✅ `[buddy_check] out-of-memory request: passed`
- ✅ `buddy_check() succeeded!`
- ✅ `check_alloc_page() succeeded!`

**关键改进点总结：**

1. 在 `[SPECIFICATION]` 中补充了 **order、2^k 页、物理页号对齐** 的精确定义；
2. 在 `[RELY]` 中列出 `page2ppn()`、`nbase`、`npage`，避免生成代码越界访问；
3. 在 `[TEST]` 中增加 OOM、拆分、合并、对齐和链表一致性检查。

---

## 模块四：SLUB 对象分配器

负责人：`2411189-李梓宸`

### 模块功能描述

**需要实现/修改的函数：**

```c
/* 对外接口 */
void slub_init(void);
void *slub_alloc(size_t size);
void slub_free(void *ptr);
void slub_check(void);

/* 内部辅助函数 */
static struct kmem_cache *slub_cache_for(size_t size);

static inline void *slub_page_kva(struct Page *page);

static inline bool slub_bit_test(const uint8_t *bitmap, size_t index);
static inline void slub_bit_set(uint8_t *bitmap, size_t index);
static inline void slub_bit_clear(uint8_t *bitmap, size_t index);

static struct slub_slab *slub_slab_of(struct kmem_cache *cache, void *obj);

static struct slub_slab *slub_grow(struct kmem_cache *cache);

static bool slub_locate(void *ptr,
                        struct kmem_cache **cache_out,
                        struct slub_slab **slab_out,
                        size_t *index_out);

static void *slub_alloc_large(size_t size);
static bool slub_free_large(void *ptr);

static void slub_purge_slab(struct kmem_cache *cache,
                            struct slub_slab *slab);

static void slub_evict_slab(struct kmem_cache *cache,
                            struct slub_slab *slab);

static bool slub_check_overlap(void *a, size_t a_size,
                               void *b, size_t b_size);
```

**功能说明：**

1. **在系统中的作用**

   SLUB 是建立在物理页分配器之上的第二层对象分配器：

   ```text
   第一层：Best-Fit / Buddy 管理物理页
   第二层：SLUB 管理小对象和任意大小对象
   ```

   小对象通过固定大小缓存分配，减少内部碎片和页分配次数；大对象直接通过页分配器分配连续页。

2. **需要处理的主要场景**

   - 8、16、32、64、128、256 字节小对象缓存；
   - 每个 slab 占一页，页首放元数据；
   - 对象按自身大小对齐；
   - 空闲对象使用侵入式单链表连接；
   - slab 用尽时调用 `alloc_pages(1)` 扩展；
   - 整个 slab 全空时归还页分配器；
   - 对象释放后优先复用；
   - `size == 0` 返回 `NULL`；
   - 大于 256 字节的对象走大对象路径；
   - 大对象使用连续多页，并在页首保存元数据；
   - 非法指针、重复释放需要拒绝或警告。

3. **与其他模块的交互关系**

   ```text
   pmm_init()
      ↓
   page_init()
      ↓
   Best-Fit / Buddy
      ↓
   alloc_pages() / free_pages()
      ↓
   SLUB slab / large object
      ↓
   slub_alloc() / slub_free()
   ```

   `kern_init()` 在 `pmm_init()` 之后调用 `slub_check()`：

   ```c
   pmm_init();
   slub_check();
   ```

### 最终提示词

```text
[ROLE]
你是一名 uCore 内核开发者。请实现基于页分配器的两层 SLUB 对象分配器。

[CONTEXT]
第一层已经提供 alloc_pages(n)/free_pages(page,n)，由 Best-Fit 或 Buddy 实现。
第二层需要提供 slub_alloc(size_t)/slub_free(void *)。项目运行在 RISC-V 上，
PGSIZE=4096，内核虚拟地址与物理地址之间使用 va_pa_offset 映射。
已有 list_entry_t、list_init、list_add、list_del、le2struct/to_struct 等链表设施。

[SPECIFICATION]
请实现以下内容：

一、固定大小缓存
1. 支持 8、16、32、64、128、256 字节 6 个缓存；
2. 请求大小向上取整到最小的足够大的缓存；
3. size == 0 返回 NULL；
4. 超过 256 字节的请求进入大对象路径，不能直接返回 NULL。

二、slab 结构
1. 每个 slab 占一个物理页；
2. 页首存放 struct slub_slab 元数据；
3. 对象区从 ROUNDUP(sizeof(struct slub_slab), object_size) 开始；
4. 每个对象按 object_size 对齐；
5. 每个 slab 维护 objects_per_slab、nr_free、state 位图；
6. state 位图中 1 表示已分配，0 表示空闲；
7. 所有 slab 通过 slab_link 挂在 kmem_cache.slab_list 上。

三、空闲对象链表
1. 使用侵入式单链表，空闲对象的第一个机器字保存下一个空闲对象地址；
2. 该方案必须兼容 8 字节对象；
3. cache 维护 free_list、nr_free、nr_total、nr_slabs。

四、slub_grow()
1. 调用 alloc_pages(1) 获取一个新页；
2. 初始化 slab 元数据；
3. 将页内所有对象串成空闲链表；
4. 把新 slab 加到 cache->slab_list；
5. 更新 cache->free_list、nr_free、nr_total、nr_slabs。

五、slub_alloc()
1. 懒初始化所有 cache；
2. 找到合适的 cache；
3. 如果 cache->free_list 为空，调用 slub_grow()；
4. 从 free_list 取出一个对象；
5. 通过 slab 范围和 object_size 计算对象下标；
6. 校验对象对齐、未分配、slab 有剩余空间；
7. 设置 state 位，减少 cache 和 slab 的 nr_free；
8. 返回对象地址。

六、slub_free()
1. ptr == NULL 直接返回；
2. 通过 slab 范围定位所属 cache、slab 和对象下标；
3. 如果指针不在任何 slab 中，尝试大对象链表；
4. 重复释放必须检测 state 位并警告；
5. 清除 state 位，把对象头插入 cache->free_list；
6. 增加 cache 和 slab 的 nr_free；
7. 如果 slab 全部空闲，调用 slub_evict_slab()：
   - 从 cache 空闲链表中删除该 slab 的所有对象；
   - 从 slab_list 删除；
   - 更新 cache 计数；
   - free_pages(slab->page, 1)。

七、大对象路径
1. 超过最大缓存的请求使用 alloc_pages(n) 分配连续页；
2. 在页首放 struct slub_large，记录 next、page、nr_pages、size、magic；
3. 返回 header 之后的 payload；
4. slub_free() 必须能在大对象链表中找到并 free_pages()；
5. 必须支持 300 字节和 PGSIZE 字节的多页对象；
6. 分配和释放前后 nr_free_pages() 必须恢复。

八、slub_check()
实现自测，覆盖：
- 不同大小对象分配且不重叠；
- 对象按缓存大小对齐；
- 一个 slab 不够时扩展第二个页；
- 批量释放后页回收；
- 释放对象优先复用；
- size == 0；
- 300 字节大对象；
- 4096 字节多页对象；
- 所有测试结束后页数恢复。

[RELY]
可以使用：
- alloc_pages(n)、free_pages(page,n)、nr_free_pages()
- page2pa()、va_pa_offset、struct Page
- ROUNDUP、PGSIZE、memset
- list_init、list_add、list_del、list_next、list_entry
- to_struct/le2slab
- assert、panic、warn、cprintf

[CONSTRAINTS]
- 不要绕过页分配器直接使用物理地址；
- 不要修改 pmm_manager 的 Best-Fit/Buddy 实现；
- 必须处理 size == 0、未知指针、重复释放；
- 小对象不能越界到相邻对象；
- slab 元数据不能与对象区重叠；
- 大对象不能与 slab 对象冲突；
- 释放时必须恢复页计数。

[TEST]
slub_check() 预期输出：
  [slub_check] size classes/non-overlap: passed
  [slub_check] slab growth: used 2 page(s): passed
  [slub_check] bulk free/page reclaim: passed
  [slub_check] free-list reuse: passed
  [slub_check] size == 0: handled (NULL)
  [slub_check] large-object fallback (300 bytes): passed
  [slub_check] multi-page object (4096 bytes): passed
  slub_check() succeeded!

[OUTPUT]
输出 slub.c 和 slub.h 的完整实现，并在 kern_init() 中 pmm_init() 之后调用 slub_check()。
```

### 实现迭代过程

本模块的实现经历了 **3 次迭代**，过程如下：

---

#### 第一次迭代

**遇到的问题：**

- 问题 1：初版提示词只要求“实现 SLUB”，生成代码把请求直接四舍五入到页，没有真正实现 slab 缓存；
- 问题 2：没有实现空闲对象链表，分配和释放只能整页进行；
- 问题 3：没有 slab 回收逻辑，批量释放后页数不能恢复。

**问题解决策略：**

- 在 `[SPECIFICATION]` 中明确“两层架构”和“slab 页 + 对象区”的结构；
- 补充 `slub_grow()`、`slub_alloc()`、`slub_free()` 的完整流程；
- 在 `[TEST]` 中加入“批量释放后页数恢复”和“对象复用”检查。

---

#### 第二次迭代

第二次生成代码能够完成小对象分配，但仍有问题：

- 问题 1：slab 元数据区与第一个对象重叠，8 字节对象被破坏；
- 问题 2：空闲对象链表使用完整 `list_entry_t`，8 字节对象放不下；
- 问题 3：slab 全部空闲后没有从 cache 中删除，导致页不能归还。

**问题解决策略：**

- 在 `[SPECIFICATION]` 中明确 `offset = ROUNDUP(sizeof(struct slub_slab), object_size)`；
- 要求空闲对象第一个机器字保存 next 指针，使用侵入式单链表；
- 补充 `slub_purge_slab()` 和 `slub_evict_slab()` 的回收流程。

---

#### 第三次迭代

第二次实现通过了 8/16/32/64/128/256 字节缓存测试，但发现：

- 问题 1：`slub_alloc(300)`、`slub_alloc(4096)` 等大于 256 字节的请求返回 `NULL`，不满足 Challenge 中“任意大小”的要求；
- 问题 2：没有针对大对象的测试，报告无法证明任意大小分配能力。

**问题解决策略：**

- 在 `[SPECIFICATION]` 中增加“大对象路径”；
- 使用 `alloc_pages(n)` 分配连续页；
- 在页首放 `struct slub_large`，记录 `next`、`page`、`nr_pages`、`size`、`magic`；
- `slub_free()` 先查 slab，再查大对象链表；
- 在 `[TEST]` 中增加 300 字节和 4096 字节对象测试，并检查 `nr_free_pages()` 恢复。

**最终结果：**

经过 **3 次迭代**，该模块最终：

- ✅ 编译通过；
- ✅ `slub_check()` 全部通过；
- ✅ Best-Fit 和 Buddy 两种页分配器下均通过；
- ✅ 8–256B 小对象走 slab；
- ✅ 300B、4096B 大对象走页级分配；
- ✅ 批量释放后页数恢复。

实测输出：

```text
  [slub_check] size classes/non-overlap: passed
  [slub_check] slab growth: used 2 page(s): passed
  [slub_check] bulk free/page reclaim: passed
  [slub_check] free-list reuse: passed
  [slub_check] size == 0: handled (NULL)
  [slub_check] large-object fallback (300 bytes): passed
  [slub_check] multi-page object (4096 bytes): passed
slub_check() succeeded!
```

**关键改进点总结：**

1. 在 `[SPECIFICATION]` 中补充了 **slab 元数据布局、对象偏移、位图状态、页回收**；
2. 在 `[RELY]` 中明确 `alloc_pages/free_pages`、`page2pa`、`va_pa_offset`、链表接口；
3. 在 `[TEST]` 中增加大对象和多页对象测试，弥补了初版只测小对象的不足；
4. 在 `[CONSTRAINTS]` 中明确禁止绕过页分配器直接使用物理地址。

---

### 练习：[练习标题]

**负责人：** [2411189-李梓宸]

**任务一：实现物理内存探测与页描述符初始化功能（借助 AI 完成）**

**1. 实现思路**
通过借助 AI 辅助（Prompt 详见 `prompt.md`），在 `kern/mm/pmm.c` 的 `page_init()` 函数中完成了物理内存的探测与初始化。核心步骤如下：

- 调用 `get_memory_base()` 和 `get_memory_size()` 从设备树获取物理内存的起始地址和大小。
- 计算总物理页数 `npage`，并设置虚拟地址与物理地址的偏移量 `va_pa_offset`。
- 计算出 Page 描述符数组（`pages`）所需的空间，并确保它以及内核镜像（由链接符号 `end` 标记）和 OpenSBI 不会与空闲内存区域重叠。
- 找到第一个可用的完整物理页，将其转换为对应的 Page 指针后，调用 `init_memmap()` 将剩余可用物理内存注册到 `pmm_manager` 中。

**2. 测试结果**
运行 `make qemu` 后，内核成功启动并打印出探测到的物理内存范围。截图显示 `[Test 1] Page Init: PASS`，证明初始化逻辑正确。
![Page Init 运行结果](images/练习功能模块一任务一.png)

#### 功能模块二：物理内存分配功能

**任务一：理解 First-Fit 连续物理内存分配算法**

**1. First-Fit 算法原理简述**
结合 `kern/mm/default_pmm.c` 中的源码，First-Fit 的核心机制如下：

- **初始化空闲页 (`default_init_memmap`)**：遍历传入的物理页，将页的 `ref` 和 `flags` 清零，设置 `property` 记录连续空闲页数，并按物理地址顺序将这些空闲块插入到全局双向链表 `free_list` 中。
- **分配连续物理页 (`default_alloc_pages`)**：从 `free_list` 的表头开始遍历，寻找第一个 `property >= n` 的空闲块。如果找到的块大于请求页数，则进行切割，将剩余部分重新设置为空闲块；将分配出的块从链表中删除，并更新 Page 状态。
- **回收与合并 (`default_free_pages`)**：释放页面时，清空页状态，更新 `property`，并将释放的块**按物理地址顺序**插入到 `free_list` 中。插入后，会检查该块与前、后物理地址相邻的块是否连续，如果连续则进行合并，更新合并后首页的 `property`，并从链表中删除多余的节点。

**2. 思考题：原有 First-Fit 算法是否有进一步的改进空间？**
有。First-Fit 算法在每次分配时都从链表头开始顺序查找，时间复杂度为 O(N)，效率较低。此外，它倾向于优先分配低地址的内存，容易导致低地址产生大量极小的外部碎片。改进方向包括：

- 引入 **Next-Fit** 算法，记录上一次查找结束的位置，下一次分配从该位置继续查找。
- 使用分离空闲链表（Segregated Free Lists）或平衡树（如红黑树）来管理空闲块，将查找时间复杂度降至 O(log N)。

**任务二：实现 Best-Fit 连续物理内存分配算法（借助 AI 完成）**

**1. 实现思路**
通过设计 Prompt（详见 `prompt.md`），让 AI 在 `kern/mm/best_fit_pmm.c` 中实现了 Best-Fit 算法，并在 `kern/mm/pmm.c` 的 `init_pmm_manager()` 中将管理器切换为 `best_fit_pmm_manager`。核心改动：

- **`alloc_pages`**：遍历 `free_list` 中的**所有**空闲块，记录满足 `property >= n` 且 `property` 最小的块。找到最匹配的块后执行切割和分配。
- **`free_pages`**：释放时，按物理地址顺序将新空闲块插入链表。然后通过检查 `page2pa` 计算地址连续性，如果与前后物理块连续，则进行 `merge` 合并。

**2. 测试结果**
运行测试后，成功打印 `memory management: best_fit_pmm_manager` 以及 `[Test 2] Best-Fit: PASS`。
![Best Fit 运行结果](images/练习功能模块二任务二.png)

**3. 思考题：你的 Best-Fit 算法是否有进一步的改进空间？**
有。Best-Fit 虽然能尽量保留大块连续内存，减少大块浪费，但其缺点在于每次分配都需要遍历整个空闲链表（O(N)），且容易产生大量极小的、无法再被利用的碎片（外部碎片）。改进方向：可以引入**分箱策略（Segregated Fit）**，按照内存块大小的 2 的幂次维护多个链表，这样查找时只需在相近大小的链表中寻找；或者采用**红黑树**来维护空闲块，实现 O(log N) 的查找与插入。

**任务三：Buddy System 实现与自测**

**负责人：** 2411584-马锐

**1. 实现与接入**

在完整工程 `code/lab2/kern/mm/buddy_pmm.c/.h` 中实现 Buddy，通过 `struct pmm_manager` 提供初始化、空闲区注册、分配、释放、空闲页统计和检查回调，并在 `pmm.c` 的 `init_pmm_manager()` 中选择 `buddy_pmm_manager`。公共 `Page` 结构、已有框架测试和评分脚本保持原样。

第 k 阶空闲链表管理 `2^k` 页的块。申请时将请求向上取整，例如申请 3 页实际取 4 页块；对应阶没有空闲块时，从更大的块逐级对半拆分。释放时根据物理页号与块页数异或计算伙伴，只有伙伴同阶、空闲且在合法范围内才合并，并继续尝试向上合并。初始空闲区按物理页号对齐拆分，因此起点不规整、总页数不是 2 的幂时也能保留可用页。

`Page.property` 在块首页记录实际块页数，只有空闲块首页设置 `PageProperty`。分配 3 页后仍可用原请求 `free_pages(base, 3)` 回收实际的 4 页。空闲页统计按实际块大小变化，失败请求返回 `NULL`。

**2. 提示词与自测设计**

本次分两步执行提示词：先实现并接入分配器，再单独扩展 `buddy_check()`，自测阶段不改分配算法。两份实际使用的提示词保存在 [prompt.md](prompt.md) 末尾的“Buddy System 实现与接入提示词”和“Buddy System 自测提示词”部分。

| 测试场景      | 检查内容与作用                                                  |
| --------- | -------------------------------------------------------- |
| 1 页分配与释放  | 必须成功分配，空闲页数减少 1，释放后恢复，检查基础回调和计数。                         |
| 3 页向上取整   | 实际减少 4 页，按原请求释放后恢复，检查分配大小与回收规则一致。                        |
| 分裂与递归合并   | 从 16 页块分配小块，回收伙伴后重新申请完整 16 页块，验证拆分和向上合并。                 |
| 相邻但非伙伴    | 4 页区域中释放第 1、2 页，虽然相邻但不能合成 2 页块；再释放真正的伙伴，才可重新得到大块。        |
| 分配失败      | 覆盖 0 页、超大请求、总页数不足、耗尽与碎片化，逐字段核对描述符、链表链接及空闲页计数，检查失败不会破坏状态。 |
| 多块并存与乱序释放 | 分配 1、8、4、2、1 页，逐对检查范围不重叠，乱序释放后恢复 16 页并重新分配整块。            |

确定性场景使用私有页描述符，暂存真实分配器的链表和全局描述符范围。所有测试分配释放完成后，恢复真实状态及原空闲页数，再输出成功；这样不会回收调用方正在使用的页面。上述测试分别从块大小、伙伴关系、状态保持和地址范围检查实现行为。

**3. 实际验证结果**

由助手在本机 WSL 执行 RISC-V 编译和独立验证：内核六类自测通过；宿主检查覆盖不规整初始区间、保留空洞、3000 次混合分配与释放，以及四类非法释放的独立负例；QEMU 的 32、64、96、128 MiB 四种配置均完成 Buddy 检查和框架分配检查。

```text
buddy_check: all 6 scenarios passed; allocator state restored.
buddy_check() succeeded!
check_alloc_page() succeeded!
```

原始输出见 [Buddy验证输出](Buddy验证输出.txt)，复现脚本为 `debug/check_buddy.py`。本次没有运行 `make grade`，上述结果不作为整组评分结果。

### Challenge：[Challenge标题]

**负责人：** [2411189-李梓宸]

### Challenge 1：硬件的可用物理内存范围的获取方法

**解答：**
如果操作系统无法提前获知硬件的可用物理内存范围，通常可通过以下几种机制让 OS 动态获取物理内存信息：

1. **BIOS 中断探测（e820）**：在 x86 架构的实模式下，操作系统在启动早期可以调用 BIOS 提供的 `INT 15H, EAX=E820H` 中断。BIOS 会返回一个物理地址映射表，明确告知 OS 哪些物理地址段是可用 RAM，哪些被硬件保留或占用。
2. **UEFI 内存映射表**：在现代 UEFI 引导模式下，UEFI 固件会在将控制权交接给 OS 时，通过 `GetMemoryMap()` 服务提供一份详细的内存描述符表，直接标出可用和保留的内存区域。
3. **直接内存探测（内存扫描/踩踏）**：在极简嵌入式环境中，OS 可以从一个安全的基准物理地址开始，按页递增尝试进行写入和读取操作。如果触发了总线错误或缺页异常，说明到达了物理内存的边界。
4. **ACPI 表解析**：从高级配置与电源接口（ACPI）提供的静态表中解析物理内存分布信息，获取地址空间的拓扑结构。

### Challenge 2：任意大小的内存单元 SLUB 分配算法

**1. 实现思路**
本模块通过从零设计，实现了两层架构的 SLUB 内存分配器：

- **第一层（页级）**：依赖于底层现有的物理内存管理器（如 Best-Fit）。当上层的对象缓存池为空时，向底层申请一页或多页连续的物理内存。
- **第二层（对象级）**：将申请到的一整页物理内存，根据用户请求的 `size` 进行对齐，切分成多个固定大小的对象（Object），并通过内部的空闲链表（Free-list）进行串联管理。本实现维护了多个固定大小的缓存池（如 8, 16, 32, 64, 128 字节等），提供了 `slub_alloc` 和 `slub_free` 接口。

**2. 提示词设计**
（完整 Prompt 内容详见 `prompt.md`）

- 第一份 Prompt（生成算法）：明确任务为“两层架构”，要求 AI 设计 `kmem_cache` 结构体维护空闲链表，并强调分配与释放时的安全性。
- 第二份 Prompt（生成测试）：要求 AI 编写 `slub_check()` 函数，强制要求覆盖不同大小对象的分配、跨页的多对象申请、释放后的内存复用、以及非法参数的边界防御。

**3. 测试覆盖与有效性说明**
通过运行内核测试，SLUB 模块的表现如下：

- **大小选择与地址对齐**：验证了不同尺寸的请求能被正确路由到对应的缓存池，且返回的指针符合对齐要求。
- **多页扩展（Slab Growth）**：通过批量申请大量小对象，验证了当单页容量耗尽时，底层能正确向系统申请新的页面。
- **内存复用与回收**：释放对象后再次申请同等大小对象，验证了系统会优先从空闲链表中返回刚被释放的地址，避免了底层物理页的频繁申请与泄露。
- **安全防御与边界**：测试了 `double-free`，系统成功拦截并报错，防止了链表损坏；对 `size == 0` 的请求安全返回了 `NULL`。

这些测试有效证明了：上层对象的切分与回收逻辑闭环、下层页的申请与归还联通正常，且具备一定的安全防御能力，完全符合 SLUB 的核心思想。

---

### 分支任务：GDB 调试页表查询过程

**负责人：** 2411584-马锐

首先在 `kern_init+22` 暂停，PC 为 `0xffffffffc02000ec`，选定的访存指令为 `sd ra,8(sp)`。此时 `sp = 0xffffffffc0205ff0`，因此目标虚拟地址为 `sp + 8 = 0xffffffffc0205ff8`，`satp = 0x8000000000080206`。图 1 记录该指令执行前的状态。

![页表调试图1：选定访存指令及有效虚拟地址](images/页表调试01-访存指令.png)

设置目标地址条件断点并单步执行该指令后，宿主 GDB 命中 `get_physical_address`。入口地址与图 1 的计算结果一致，`is_debug = false`，调用栈显示 `MMU_DATA_STORE` 和 `size = 8`，对应内核的真实 8 字节写访问。

![页表调试图2：QEMU 访存入口参数与调用栈](images/页表调试02-QEMU访存入口.png)

继续运行到页表查询循环，实测 `base = 0x80206000`、`levels = 3`。首次查询时 `i = 0`，根页表索引为 `idx = 511`，PTE 的物理地址为 `0x80206ff8`，正好等于 `base + 511 * 8`，见图 3。

![页表调试图3：根页表索引与PTE物理地址](images/页表调试03-索引与PTE地址.png)

在 `cpu_helper.c:923` 执行一次 `next`，完成 `address_space_ldq(...)` 读取后，实测 `pte = 0x200000cf`，见图 4。低 8 位 `0xcf` 中 `V/R/W/X/A/D` 均为 1。

![页表调试图4：读取PTE后的结果](images/页表调试04-PTE读取结果.png)

继续观察第 946 行的 `!(pte & (PTE_R | PTE_W | PTE_X))` 判断。本次 R/W/X 均为 1，条件为假，跳过查询下一级页表的分支，进入叶子项的合法性与权限检查，最终到达物理地址计算。实测 `ppn = 0x80000`、`ptshift = 18`，说明根级叶子采用 1 GiB 大页映射，实际只读取一级页表项。

手算物理地址：`ppn << 12 = 0x80000000`，虚拟地址低 30 位为 `0x205ff8`，两者相加得到 `0x80205ff8`。在地址赋值完成后的第 1038 行打印 `*physical`，QEMU 实际输出同为 `0x80205ff8`，见图 5，完成本次地址翻译核对。

![页表调试图5：最终物理地址与手算结果核对](images/页表调试05-物理地址核对.png)

调试过程中遇到客体 GDB 等待超时、选错线程栈帧导致局部参数不可见，以及控制台命令粘贴方式等问题。通过向助手提供实际输出，分别调整远程等待设置、切回正确栈帧及逐条输入命令后解决。具体情景与处理过程见下方调试记录。

详细过程见[页表调试记录](页表调试记录.md)。

#### TLB 查询、回填与命中（补充）

**负责人：** 2412483-李梦涛

上面的记录覆盖了“访存 → 页表遍历 → 物理地址核对”。本节在同一份 `code/lab2` 上补齐 TLB 层，说明 TLB 未命中如何进入页表查询、翻译成功后如何回填、后续访问如何命中，以及 QEMU 软件 TLB 与真实 CPU TLB 的差别。

1. **慢路径入口**：数据写从 `helper_stq_mmu → do_st8_mmu → mmu_lookup → mmu_lookup1` 进入，`mmu_lookup1` 先读 `CPUTLBEntry.addr_write` 并用 `tlb_hit()` 比较页地址；取指则在 `probe_access_internal` 里做同样的 `tlb_hit_page()` 判断。只有未命中才走 `tlb_fill_align → riscv_cpu_tlb_fill → get_physical_address`。
2. **一次完整回填**：对 `sd ra,8(sp)` 的地址 `0xffffffffc0205ff8`，实测先从空表项（`tlb_addr=0xffffffffffffffff`，MISS）进入 `tlb_fill`，页表查询得到 `base=0x80206000`、`pte=0x200000cf`、`pa=0x80205ff8`，随后 `tlb_set_page(vaddr_page=0xffffffffc0205000, pa_page=0x80205000, tlb_size=0x1000)`。扫描 `CPUTLB` 可看到回填结果：`f[20][5]: addr_write=0xffffffffc0205000, addend=0x7fffb2200000`（`20 = 22-1-1` 是 S 模式的 fast 下标，`5 = (vaddr>>12) & 0xff`）。
3. **命中的快路径不在 C 里**：第一次未命中之后，同一 4 KiB 页的栈访问不再触发 `mmu_lookup1` 断点，因为 TCG 在翻译访存指令时已生成 inline 的 TLB 比较（`tcg/i386/tcg-target.c.inc` 的 `tcg_out_qemu_ld/st`），命中时直接用 `vaddr + addend` 得到宿主地址。这解释了为什么本次 `[T]` 只出现一次，也说明 QEMU 的“TLB 命中”是宿主指令级检查，而“未命中”才落到 C 代码。
4. **与未开启分页的对比**：M 模式、`satp=0` 时 `get_physical_address` 在 `mode == PRV_M` 分支直接返回 `VA=PA`，但 QEMU 仍会执行 `tlb_fill`/`tlb_set_page`；真实 CPU 在 Bare 模式不查 TLB。
5. **与真实 TLB 的差异**：QEMU 的 TLB 是每个 MMU 索引一组的直接映射软件数组（`addr_read/write/code + addend`），无相联度、替换策略与 ASID；默认按 4 KiB 回填（即使 PTE 是 1 GiB 大页）；且与 TCG 翻译块缓存强耦合，`satp`/`sfence.vma` 变化会同时 flush TLB 与 TB。

完整调用路径、逐条日志、8 项差异对照与复现命令见[TLB 调试记录](TLB调试记录.md)，原始输出见 `logs/tlb_host_gdb.log`。

---

## 五、测试与验证

<!-- 根据该实验的具体情况，提供完整的测试运行截图，应包含：
- 编译及运行成功的输出（make qemu），可能有多个
- 测试通过的结果（make grade），只有一个
-->

**测试截图：**

![练习功能模块一任务一](./images/练习功能模块一任务一.png)
![练习功能模块二任务二](./images/练习功能模块二任务二.png)
![练习功能模块二任务三](./images/练习功能模块二任务三.png)
![练习功能模块二challenge](./images/练习功能模块二challenge.png)

---

## 六、实验总结与收获

### 对操作系统的理解

1. 列出你们认为本实验中重要的知识点，以及与对应的 OS 原理中的知识点，并简要说明二者的含义、关系和差异
2. 列出你们认为 OS 原理中很重要、但在本实验中没有对应上的知识点

### AI 协作开发的经验

[写出你们在与 AI 协作过程中实际收获的经验或者感悟]

---

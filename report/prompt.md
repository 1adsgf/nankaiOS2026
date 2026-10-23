【任务说明 / Prompt】
1. 任务：请在 `kern/mm/pmm.c` 文件中的 `page_init()` 函数内（寻找带有 lab2 和 YOUR CODE 标记的位置）实现物理内存探测与页描述符初始化功能。该函数不接收参数，也不返回结果。
2. 操作要求：你必须在项目中实际修改文件，而不仅仅是展示代码。严禁修改框架中已有的宏定义、数据结构、其他函数以及任何测试断言函数。只允许在指定位置编写实现代码。
3. 输出与验证要求：代码必须符合 C 语言语法且能成功编译。成功执行后，必须能通过 `cprintf` 打印出探测到的物理内存范围。此外，必须保证正确设置 `va_pa_offset`、`npage` 和 `pages`。

【依赖信息 / Rely】
实现该函数可依赖以下已知信息：
- 设备树获取函数：`get_memory_base()`、`get_memory_size()`
- 异常与输出函数：`panic()`、`cprintf()`
- 内存注册函数：`init_memmap()`
- 地址与页转换：`pa2page()`、链接符号 `end`（表示内核结束位置）、`struct Page`
- 框架提供的宏：物理内存上界宏、物理/虚拟地址转换宏、页面对齐宏、页面状态宏。
- 全局变量：`va_pa_offset`、`npage`、`pages`。

【接口清单 / Guarantee】
不需要对外提供新接口，只需完整实现 `static void page_init(void)` 内部逻辑。如果有必要，你可以定义局部的辅助变量。

【行为规格 / Specification】
请按以下逻辑和边界情况严格实现：
1. 前置条件与初始探测：
   - 调用相关函数获取物理内存的起始地址和大小。如果设备树信息无效（如大小为 0 或地址异常），必须调用 `panic()` 终止初始化。
   - 计算总的物理页数量 `npage`，并正确设置虚拟地址和物理地址的偏移量 `va_pa_offset`。
2. 排除保留内存（区分虚拟地址、物理地址、物理页号和描述符地址）：
   - 计算页描述符数组 `struct Page` 所需的空间。`pages` 数组必须放在内核镜像（从内核起始地址到链接符号 `end`）之后。
   - 必须确保 OpenSBI、内核镜像本身以及描述符数组占用的空间不会进入空闲区。
   - 计算时务必注意页对齐（Page Alignment）。
3. 注册空闲物理内存：
   - 找到排除保留内存后，第一个可用的完整物理页的起始物理地址。
   - 计算剩余可用页数。如果可用完整页面数量为 0，**不得**调用 `init_memmap()`。
   - 如果存在可用页，将其物理地址转换为对应的 Page 描述符指针（使用 `pa2page()`），并调用 `init_memmap()` 将该空闲区交给当前 `pmm_manager` 管理。
4. 输出：使用 `cprintf()` 打印出内存探测的范围（例如物理内存起止地址）。
【任务说明 / Prompt】
1. 任务：请在 `kern/mm/best_fit_pmm.c` 文件中标有 `lab2` 和 `YOUR CODE` 的位置，实现完整的 Best-Fit（最佳适应）连续物理内存分配算法。同时，请在 `kern/mm/pmm.c` 的 `init_pmm_manager()` 函数中，将当前物理内存管理器切换为 `best_fit_pmm_manager`。
2. 操作要求：必须在项目中实际修改文件，而不仅仅是展示代码！严禁修改 `best_fit_pmm.c` 中已有的 `basic_check()` 和 `best_fit_check()` 这两个测试函数！严禁修改 `pmm.h` 和 `memlayout.h` 中已有的数据结构、宏定义和现有接口。
3. 输出与验证要求：代码必须符合 C 语言语法，能够成功编译。实现必须满足 Best-Fit 的语义，并成功通过 `best_fit_check()` 的断言测试。请在指定的代码位置注释加上学号：`// YOUR STUDENT ID: [2411189]`。

【依赖信息 / Rely】
1. 请参考 `kern/mm/default_pmm.c` 中的 First-Fit 实现，了解 `struct pmm_manager` 的各个成员函数签名。
2. 你可以使用 `struct Page`（包含 `ref`, `flags`, `property` 等成员）、`list_entry`、`list_add`、`list_del` 等双向链表宏。
3. 你可以使用 `pmm.h` 中定义的页面状态宏（如 `SetPageProperty`、`ClearPageProperty` 等）。

【接口清单 / Guarantee】
1. 你必须在 `best_fit_pmm.c` 内部设计并实现必要的辅助函数（如 init, init_memmap, alloc_pages, free_pages）。
2. 你必须在文件末尾定义并导出 `const struct pmm_manager best_fit_pmm_manager` 结构体，将你实现的函数注册进去，名称必须是 `best_fit_pmm_manager`。

【行为规格 / Specification】
请严格按照以下算法语义实现核心函数：
1. `init_memmap`：遍历传入的物理页，初始化页的属性（清零 ref 和 flags），设置 property 为连续空闲页数，并按物理地址顺序将空闲页块插入全局空闲链表 `free_list` 中。
2. `alloc_pages` (Best-Fit 核心)：
   - 遍历全局空闲链表 `free_list`，寻找所有满足 `p->property >= n` 的空闲块。
   - **Best-Fit 语义**：在所有满足条件的块中，选择 `property` 值最小（即大小最接近 n）的那个块进行分配。
   - 如果找到的块大小大于 n，则需要进行切割：将前 n 页分配出去，剩余的部分作为一个新的空闲块（更新其 property 为 `p->property - n`，并更新其 Page 属性），按物理地址顺序重新插入空闲链表。
   - 如果成功分配，需要更新分配出去的首页的 ref、flags 和 property，并返回该起始页的指针。如果没有足够的连续页，返回 NULL。
3. `free_pages` (包含合并逻辑)：
   - 释放页面时，清零相关页的 ref 和 flags，设置首页的 property 为释放的页数 n。
   - 将释放的空闲块按物理地址顺序插入到 `free_list` 中（不要直接插在表头，要找到地址比它大的节点插在前面）。
   - **关键合并**：插入后，必须检查该块与前后相邻的物理页块是否连续。如果连续，必须将它们合并成一个大的空闲块，更新首页的 property，并从链表中删掉多余的管理节点。
   【任务说明 / Prompt】
1. 任务：请在 `kern/mm/` 目录下从零创建 `buddy_pmm.c` 和 `buddy_pmm.h`，实现 Buddy System（伙伴系统）物理内存分配模块，并在 `kern/mm/pmm.c` 中将其接入并切换为 `buddy_pmm_manager`。
2. 操作要求：必须在项目中实际创建文件并修改代码。严禁修改现有的通用数据结构和已有框架。
3. 输出与验证要求：代码能通过 `make` 编译，能成功启动 QEMU。

【依赖信息 / Rely】
1. 参考 `pmm.h` 中的 `struct pmm_manager` 和 `struct Page`。
2. 使用双向链表 `list.h` 中的宏，以及相关的 `SetPageProperty` 等宏。

【接口清单 / Guarantee】
1. 必须实现 `init`, `init_memmap`, `alloc_pages`, `free_pages` 等函数。
2. 必须定义并导出 `buddy_pmm_manager`。
3. 需要设计并维护一个 `free_area` 数组（例如 `list_entry_t free_area[MAX_ORDER]`），用于记录不同大小（2的幂次）的空闲块。

【行为规格 / Specification】
1. `init`：初始化 `free_area` 数组中的所有空闲链表。
2. `alloc_pages`：
   - 将请求的页数向上取整为 2 的幂次（设为 order）。
   - 在 `free_area[order]` 中查找，如果没有空闲块，则向上层（更大的 order）寻找，找到后将大块不断对半分裂，直到得到对应 order 的块，分裂出的另一半放入对应的 `free_area`。
   - 分配成功后，更新 Page 属性并返回。
3. `free_pages`：
   - 释放页面时，计算其 order 及对应的伙伴块（buddy）的地址。
   - 检查伙伴块是否空闲且大小相同。如果是，将它们从链表中移除，合并成更高 order 的块，并继续向上检查合并，直到无法合并为止。
   【任务说明 / Prompt】
1. 任务：请在 `kern/mm/buddy_pmm.c` 中设计并实现 `buddy_check()` 测试函数。
2. 操作要求：只修改 `buddy_pmm.c` 添加测试代码，不要修改其他源文件。

【行为规格 / Specification】
请编写测试逻辑以覆盖以下核心行为和边界情况：
1. 基础分配与释放：申请 1 页，确认成功；释放 1 页，确认内存恢复。
2. 阶数（Order）对齐测试：申请 3 页，检查系统是否实际返回了 4 页（因为 Buddy System 必须按 2 的幂次分配）。
3. 分裂测试：先申请一个大块，再申请一个小块，验证是否触发了大块的分裂逻辑。
4. 合并测试：连续申请两个物理地址相邻的块（互为伙伴），然后分别释放，验证是否成功触发了伙伴合并。
5. 极限测试：申请超过系统可用总内存的页数，确认返回 NULL。
【任务说明 / Prompt】
1. 任务：请在 `kern/mm/` 目录下从零创建 `slub.c` 和 `slub.h`，实现两层架构的 SLUB 内存对象分配模块。
2. 操作要求：必须在项目中实际创建文件，仅实现模块，暂不接入 `pmm_manager`（SLUB 用于更细粒度的 `kmalloc` 级别分配）。

【依赖信息 / Rely】
1. 底层的页分配直接调用现有的 `alloc_pages(n)` 和 `free_pages(base, n)` 接口。
2. 使用 `pmm.h` 和 `list.h` 中的数据结构。

【接口清单 / Guarantee】
1. 实现 `void *slub_alloc(size_t size);` 和 `void slub_free(void *ptr);`。
2. 实现一个简单的缓存管理结构体 `struct kmem_cache`，包含对象大小、页内偏移、空闲对象链表等。

【行为规格 / Specification】
1. 架构设计：维护几个固定大小（如 8, 16, 32, 64, 128 字节）的缓存池（kmem_cache）。
2. `slub_alloc(size)`：根据请求的 size 对齐到最合适的缓存池。如果该池为空，则向底层申请 1 页内存，将其划分为多个相同大小的对象，用空闲链表串联，并分配其中一个给用户。
3. `slub_free(ptr)`：根据 ptr 的地址推算其所属的缓存池，将其重新插入该池的空闲链表。
4. 安全性：分配和释放时需进行基础校验（防止重复释放）。
【任务说明 / Prompt】
1. 任务：请在 `kern/mm/slub.c` 中实现 `slub_check()` 测试函数。

【行为规格 / Specification】
1. 不同大小分配：分别调用 `slub_alloc(8)`、`slub_alloc(64)`、`slub_alloc(200)`，确保返回的指针合法且互不重叠。
2. 跨界与利用率：申请大量同尺寸对象（超过 1 页能容纳的数量），验证底层是否自动申请了第二页。
3. 释放与复用：释放一个对象，再次申请同尺寸对象，检查是否拿到了刚刚释放的地址（即内存复用）。
4. 边界：传递 `size = 0` 时的处理（返回 NULL 或分配最小块）。成功打印 "slub_check() succeeded!"。
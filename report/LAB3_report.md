# 操作系统实验报告

## 实验基本信息

| 项目       | 内容                                 |
| -------- | ---------------------------------- |
| **实验名称** | Lab 3: 中断处理机制                      |
| **小组成员** | 2411189-李梓宸、2411584-马锐、2412483-李梦涛 |
| **完成日期** | 2026-11-02                         |

### 小组分工

练习如何分工？

| 成员          | 负责的练习/模块 |
| ----------- | -------- |
| 2412483-李梦涛 |          |
| 2411189-李梓宸 |          |
| 2411584-马锐  |          |

实验报告如何分工？

---

## 一、实验目的

本实验的主要目的是：

1. 理解 RISC-V 中 Trap（中断与异常的统一总称）的硬件处理流程，以及 `stvec`、`sepc`、`scause`、`stval`、`sstatus`、`sie`、`sscratch` 等 CSR 的作用。
2. 掌握 Trap 上下文的保存与恢复方法，理解 trapframe 的内存布局与 `SAVE_ALL` / `RESTORE_ALL` 必须严格互逆的原因。
3. 打通“汇编 Trap 入口 → C 语言 `trap()` → 分类处理 → 恢复上下文 → `sret` 返回”的完整闭环。
4. 能够处理时钟中断、断点异常和非法指令异常，并正确更新 `sepc`，避免异常返回后重复执行同一条指令。
5. 学会用“明确接口与约束的 Spec + 提示词”让 AI 生成底层汇编和 C 代码，并通过真实 QEMU 测试验证生成结果。

---

## 二、实验环境

| 成员          | AI 编程工具 | 底层模型 | 备注  |
| ----------- | ------- | ---- | --- |
| 2412483-李梦涛 |         |      |     |
| 2411189-李梓宸 | 待填写     | 待填写  |     |
| 2411584-马锐  | 待填写     | 待填写  |     |

其他环境信息：

- 代码目录：`code/lab3`（由课程提供的 `lab3_new` 模板建立）。
- 编译工具链：`riscv64-unknown-elf-gcc`（`/opt/riscv/bin`）。
- 运行环境：WSL Ubuntu，`qemu-system-riscv64` 10.2.1，OpenSBI 1.8.1。
- 评分脚本：`tools/grade.sh`，三组用例合计 100 分。

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

Lab2 让内核具备了物理内存管理和启动页表，但内核仍然只能顺序执行，无法响应外部事件。Lab3 要解决的核心问题是：**当硬件事件（时钟中断）或指令执行错误（非法指令、断点）打断当前执行流时，内核如何保存现场、识别事件、完成处理，并无损地回到被打断的位置继续执行。**

整章围绕一条“Trap 生命周期”展开：

```text
硬件触发 Trap
  → PC 跳到 stvec 指向的 __alltraps
  → SAVE_ALL 在当前内核栈上构造 trapframe
  → __alltraps 按调用约定把 trapframe 指针传给 trap()
  → trap_dispatch 按 scause 最高位区分中断/异常
  → interrupt_handler / exception_handler 分类处理
  → 返回 __trapret
  → RESTORE_ALL 从 trapframe 恢复通用寄存器和 CSR
  → sret 回到原执行流
```

在这条链路中，`SAVE_ALL` 是框架已经提供的“现场记录”，学生需要补全与它严格对应的 `RESTORE_ALL`，以及把汇编与 C 连接起来的 `__alltraps`；随后再实现两个 C 语言处理函数，完成中断与异常的具体语义。时钟中断、`clock.c`、`intr.c`、`sync.h`、`atomic.h` 等时钟与原子操作支持由框架提供，用来验证整条 Trap 链路能够持续工作。

### 3.2 功能的逐步实现

1. **先实现 Trap 上下文恢复（`RESTORE_ALL`）**：因为 `SAVE_ALL` 已经定义了 trapframe 的布局，恢复必须严格按照同一布局进行；只有“存得进、取得出”，后续 C 处理函数才有意义。
2. **再实现 `__alltraps` 入口**：上下文保存完成后，需要按照 RISC-V 调用约定把 trapframe 指针放入 `a0` 并调用 `trap()`；这是汇编世界与 C 世界的接口。
3. **然后实现中断处理（`interrupt_handler`）**：时钟中断是本章唯一能主动、持续产生的 Trap，用来验证 `__alltraps`、`trap()`、`__trapret` 能否反复往返；同时处理软件中断和未知中断。
4. **最后实现异常处理（`exception_handler`）**：非法指令和断点异常需要输出信息并修改 `sepc`，否则 `sret` 后会再次执行同一条出错指令，形成异常死循环；其他异常用 `print_trapframe()` 兜底。
5. **补齐 Lab2 前置代码与启动兼容**：Lab3 模板要求把 Lab2 的 `page_init()`、Best-Fit 等代码填回，并保证开启分页后仍能启动，否则 Trap 实验无法运行。

---

## 四、实验内容与实现

### 练习：Trap 上下文恢复、Trap 入口与中断/异常处理

**负责人：** 2412483-李梦涛

**练习内容简述：**

本练习是 Lab3 的主体实验内容，按“Trap 上下文保存与恢复 → Trap 入口与返回 → 中断与异常处理”三条主线展开。需要完成的任务如下：

| 练习任务 | 具体要求 | 完成情况 |
|----------|----------|----------|
| 功能模块一：`RESTORE_ALL` | 与框架提供的 `SAVE_ALL` 严格互逆，从 trapframe 恢复 32 个通用寄存器和 `sstatus`/`sepc`/`stval`/`scause`，特别注意 `sp` 的槽位保存的是 Trap 前的原始 `sp` | 已完成，Test 1 PASS (35/35) |
| 功能模块一：`__alltraps` | 保存上下文后，按 RISC-V 调用约定把 trapframe 指针传给 `void trap(struct trapframe *tf)`，返回后进入 `__trapret` | 已完成，Test 1 PASS (35/35) |
| 功能模块二：`interrupt_handler` | 处理时钟中断：每次重新设置下一次时钟事件、累加 `ticks`、每 `TICK_NUM` 次输出、达到 `SHUTDOWN_AFTER` 次关机；处理软件中断和未知中断 | 已完成，Test 3 PASS (40/40) |
| 功能模块二：`exception_handler` | 处理非法指令和断点异常：输出规定信息并按指令长度更新 `tf->epc`；其他异常用 `print_trapframe(tf)` 兜底 | 已完成，Test 2 PASS (25/25) |
| Lab2 占位补全 | 按模板中的 `lab2` 注释补回 `page_init()`、Best-Fit 实现和启动页表兼容修改，保证 Lab3 工程能够启动 | 已完成，`check_alloc_page() succeeded!` |

本练习的详细实现、Spec 设计、最终提示词、迭代过程和问题处理，见下文各实现模块：

- [功能模块一：Trap 上下文恢复与 Trap 入口](#功能模块一trap-上下文恢复与-trap-入口)
- [功能模块二：中断与异常处理](#功能模块二中断与异常处理)
- [Lab2 代码衔接与启动兼容](#lab2-代码衔接与启动兼容)

---

### 功能模块一：Trap 上下文恢复与 Trap 入口

**负责人：** 2412483-李梦涛

#### 模块功能描述

**需要实现/修改的函数或汇编宏：**

```asm
RESTORE_ALL     # kern/trap/trapentry.S
__alltraps      # kern/trap/trapentry.S
```

`SAVE_ALL` 已经完成“把处理器状态保存到当前内核栈”的工作，保存后的栈顶就是一个 `struct trapframe`：

```c
struct trapframe {
    struct pushregs gpr;   // x0 ~ x31，共 32 个 uintptr_t
    uintptr_t status;      // sstatus
    uintptr_t epc;         // sepc
    uintptr_t badvaddr;    // stval（框架中字段名为 badvaddr）
    uintptr_t cause;       // scause
};
```

本模块需要完成两件事：

1. `RESTORE_ALL`：从 trapframe 中恢复 32 个通用寄存器和 4 个 CSR，使处理器回到 Trap 发生前的状态。
2. `__alltraps`：建立汇编入口与 C 函数 `void trap(struct trapframe *tf)` 之间的调用关系；`trap()` 返回后进入 `__trapret`，执行 `RESTORE_ALL` 和 `sret`。

`SAVE_ALL` 对 `sp` 的处理与普通寄存器不同：它先把原始 `sp` 写入 `sscratch`，分配 trapframe 空间后再用 `csrrw s0, sscratch, x0` 把原始 `sp` 取到 `s0`，并保存到 trapframe 的第 2 个槽位。因此 `RESTORE_ALL` 不能简单地把所有寄存器按顺序恢复，必须单独处理 `sp`。

#### Spec 的设计

写给 AI 的 Spec 重点约束了以下内容：

- 不得修改 `SAVE_ALL`、`struct trapframe` 和 `trap()` 的签名。
- `RESTORE_ALL` 必须与 `SAVE_ALL` 的偏移严格对应：GPR 占 `0 ~ 31 * REGBYTES`，`status/epc/badvaddr/cause` 分别位于 `32/33/34/35 * REGBYTES`。
- `sp` 的槽位（`2 * REGBYTES`）保存的是 Trap 发生前的原始 `sp`，必须最后恢复。
- `RESTORE_ALL` 需要使用 `s0 ~ s4` 作为临时寄存器读取 CSR，但这些寄存器随后也要从 trapframe 恢复，因此 CSR 写回必须先完成。
- `__alltraps` 中 `sp` 在调用 `trap()` 前后必须保持不变，`a0` 传入当前 trapframe 地址。
- 验收标准：`ebreak` 前后 26 个通用寄存器的值完全一致，且能正确返回到 `ebreak` 的下一条指令。

#### 最终提示词

```markdown
你正在实现 riscv64-ucore 的 Lab3 功能模块一，文件是 kern/trap/trapentry.S。
框架已经提供了 SAVE_ALL，它把 32 个通用寄存器和 sstatus/sepc/stval/scause
保存到当前内核栈上，形成一个 struct trapframe（36 个 uintptr_t）：
- 0~31 * REGBYTES：x0~x31，其中 x2(sp) 的槽位保存的是 Trap 前的原始 sp
- 32 * REGBYTES：sstatus
- 33 * REGBYTES：sepc
- 34 * REGBYTES：stval（框架字段名为 badvaddr）
- 35 * REGBYTES：scause

请完成：
1. RESTORE_ALL 宏：从 trapframe 恢复所有通用寄存器和 4 个 CSR。
2. __alltraps：SAVE_ALL 之后，按 RISC-V 调用约定把 trapframe 指针传给
   void trap(struct trapframe *tf)，trap 返回后进入 __trapret。

约束：
- 不得修改 SAVE_ALL、struct trapframe 和 trap() 的签名。
- 注意 SAVE_ALL 对 sp 的特殊处理：sp 的槽位保存的是原始 sp，必须最后恢复。
- 可以使用 s0~s4 作为临时寄存器读取 CSR，但它们也要在之后从 trapframe 恢复。
- 只能使用 riscv.h 中已有的 STORE/LOAD/REGBYTES 宏。
- 给出完整可编译的汇编，并解释每段代码与 SAVE_ALL 的对应关系。
```

#### 实现迭代过程

本模块经历了 2 次迭代。

##### 第一次迭代

**实现思路：**

- 先用 `s0` 取出 trapframe 中保存的原始 `sp`，再用 `s1 ~ s4` 取出 4 个 CSR；
- 写回 `sstatus`、`sepc`、`stval`、`scause`；
- 按 `x1、x3 ~ x31` 的顺序恢复通用寄存器；
- 最后恢复 `sp`。

**遇到的问题：**

- 问题 1：如果先恢复 `sp`，后续 `LOAD` 就会用新的 `sp` 作为基址，trapframe 的其余内容全部读错，程序在 `sret` 后跑飞。
- 问题 2：`sbadaddr` 这个 CSR 名称在当前 binutils 中无法识别，汇编报错
  `Error: unknown CSR 'sbadaddr'`。

**问题解决策略：**

- 针对问题 1：把 `sp` 的恢复放到最后一条，并且直接用 `LOAD x2, 2*REGBYTES(sp)`。RISC-V 的 load 会先用旧的 `sp` 计算地址，再把结果写入 `x2`，因此这一条既安全又不需要额外的临时寄存器。
- 针对问题 2：改用新版本工具链认可的标准 CSR 名称 `stval`（CSR 0x143），C 结构体字段名仍保持框架的 `badvaddr`，不影响布局。

##### 第二次迭代

**实现思路：**

- 补齐 `__alltraps`：`SAVE_ALL` 后执行 `mv a0, sp`，再 `jal trap`。

**遇到的问题：**

- 问题：不确定 `trap()` 返回后 `sp` 是否还能作为 trapframe 基址。检查 RISC-V 调用约定后确认 `sp` 是 callee-saved，C 函数返回时会恢复它，因此 `__trapret` 中的 `sp` 仍然等于 trapframe 基址，不需要额外调整。

**最终结果：**

- 编译通过；
- `[Test 1] Trap entry and breakpoint return: PASS (35/35)`。测试在 `ebreak` 前设置 26 个寄存器，返回后逐一比较，全部一致，说明 `RESTORE_ALL` 的布局和 `sp` 处理正确。

**关键改进点总结：**

1. `sp` 必须最后恢复，而且利用 load 指令“先算地址、后写目标”的语义可以直接写回 `x2`。
2. CSR 临时寄存器 `s0 ~ s4` 必须在恢复 GPR 之前使用完，否则会被 trapframe 中的旧值覆盖。
3. 汇编中的 CSR 名称要使用当前工具链支持的 `stval`，不能照抄旧教材的 `sbadaddr`。

---

### 功能模块二：中断与异常处理

**负责人：** 2412483-李梦涛

#### 模块功能描述

**需要实现的函数：**

```c
void interrupt_handler(struct trapframe *tf);   // kern/trap/trap.c
void exception_handler(struct trapframe *tf);   // kern/trap/trap.c
```

`trap_dispatch()` 根据 `scause` 的最高位区分两类 Trap：

- 最高位为 1：中断，调用 `interrupt_handler()`；
- 最高位为 0：异常，调用 `exception_handler()`。

`interrupt_handler()` 需要完成：

1. 时钟中断 `IRQ_S_TIMER`：调用 `clock_set_next_event()` 设置下一次时钟事件，累加 `ticks`；每 `TICK_NUM`（100）次输出一次 `100 ticks`；累计输出达到 `SHUTDOWN_AFTER` 后调用 `sbi_shutdown()`。
2. 软件中断 `IRQ_S_SOFT`：输出 `Supervisor software interrupt`。
3. 其他中断：输出 `Unknown interrupt: cause N`，并用 `print_trapframe(tf)` 打印完整现场。

`exception_handler()` 需要完成：

1. 非法指令 `CAUSE_ILLEGAL_INSTRUCTION`：输出 `Illegal instruction at 0x...`，并按指令长度更新 `tf->epc`。
2. 断点 `CAUSE_BREAKPOINT`：输出 `Breakpoint at 0x...`，并按指令长度更新 `tf->epc`。
3. 其他异常：调用 `print_trapframe(tf)` 兜底。

其中最关键的是 `epc` 的更新：`sepc` 保存的是触发异常的指令地址，如果直接 `sret`，处理器会再次执行同一条指令。对于 `ebreak` 和非法指令，必须跳过当前指令；RISC-V 同时支持 16 位压缩指令和 32 位指令，不能一律加 4。

#### Spec 的设计

写入 Spec 的关键约束：

- 不得修改两个处理函数的签名，也不得修改 `trap.c` 中规定以外的函数。
- `scause` 的最高位表示中断，取中断号时必须屏蔽最高位，不能直接用 `tf->cause == IRQ_S_TIMER` 判断。
- 时钟中断必须“先设置下一次事件，再计数”，否则系统只会收到一次时钟中断。
- `ticks` 每收到一次时钟中断都要加 1；`ticks % TICK_NUM == 0` 时输出统计；输出次数达到 `SHUTDOWN_AFTER` 后关机。
- 非法指令和断点的 `epc` 增量由指令编码决定：低两位为 `0b11` 是 32 位指令，增量为 4；否则是 16 位压缩指令，增量为 2。
- 未列出的中断/异常统一用 `print_trapframe(tf)` 兜底。
- 验收标准：三组评分用例分别检查寄存器恢复、非法指令返回、持续时钟中断。

#### 最终提示词

```markdown
你正在实现 riscv64-ucore 的 Lab3 功能模块二，文件是 kern/trap/trap.c。
框架已经提供 trap_dispatch()，它根据 scause 的最高位把 Trap 分发给：
  void interrupt_handler(struct trapframe *tf);
  void exception_handler(struct trapframe *tf);

请实现这两个函数，要求：

1. interrupt_handler：
   - scause 最高位是 1，中断号 = cause 去掉最高位。
   - IRQ_S_TIMER：调用 clock_set_next_event() 设置下一次时钟事件；
     ticks 自增；每 TICK_NUM(100) 次调用 print_ticks()；
     累计输出达到 SHUTDOWN_AFTER 次后调用 sbi_shutdown()。
   - IRQ_S_SOFT：输出 "Supervisor software interrupt"。
   - 其他：输出 "Unknown interrupt: cause %d"，再 print_trapframe(tf)。

2. exception_handler：
   - CAUSE_ILLEGAL_INSTRUCTION：输出 "Illegal instruction at 0x%016lx"，
     并按指令长度更新 tf->epc。
   - CAUSE_BREAKPOINT：输出 "Breakpoint at 0x%016lx"，并按指令长度更新 tf->epc。
   - 其他：print_trapframe(tf)。

约束：
- 不得修改函数签名，不得修改 trap.c 中规定以外的函数。
- 指令长度不能假定为 4：读取 tf->epc 处的 16 位指令，低两位为 0b11
  表示 32 位指令（+4），否则是 16 位压缩指令（+2）。
- 时钟中断必须每次重新设置下一次事件，否则只会触发一次。
- 输出格式要稳定，便于评分脚本用 grep 检查。
- 说明 SHUTDOWN_AFTER 的取值理由。
```

#### 实现迭代过程

本模块经历了 4 次迭代。

##### 第一次迭代

**实现思路：**

- 直接在 `interrupt_handler` 中用 `switch (tf->cause)` 判断 `IRQ_S_TIMER`，处理时钟中断。

**遇到的问题：**

- 问题：`tf->cause` 的最高位为 1，实际值是 `0x8000000000000005`，直接和 `IRQ_S_TIMER`（5）比较永远不相等，时钟中断被当成未知中断。

**问题解决策略：**

- 先计算 `uintptr_t cause = (uintptr_t)tf->cause & ~((uintptr_t)1 << 63);`，再用 `cause` 判断。这样既保留了 `scause` 的原始值，又能得到正确的中断号。

##### 第二次迭代

**实现思路：**

- 时钟中断中只累加 `ticks` 并打印，没有重新设置下一次时钟事件。

**遇到的问题：**

- 问题：系统只收到一次时钟中断，串口只打印一行 `100 ticks` 后就静止。

**问题解决策略：**

- 在时钟中断分支中先调用 `clock_set_next_event()`，再执行计数和打印。`clock_set_next_event()` 通过 `sbi_set_timer(get_time() + timebase)` 设置下一次事件，形成周期性的时钟中断。

##### 第三次迭代

**实现思路：**

- 非法指令和断点异常统一使用 `tf->epc += 4`。

**遇到的问题：**

- 问题：测试中的 `ebreak` 和 `.word 0x0000007b` 都是 4 字节，固定加 4 可以通过当前测试，但 RISC-V 允许 16 位压缩指令，固定加 4 会跳过错误的地址，属于“只对测试成立”的实现。

**问题解决策略：**

- 增加 `trap_inst_len()`：读取 `tf->epc` 处的 16 位指令，若 `(inst & 0x3) == 0x3` 则为 32 位指令，返回 4；否则返回 2。非法指令和断点都使用该函数更新 `epc`。这样既满足当前测试，也符合 RISC-V 指令编码规则。

##### 第四次迭代

**实现思路：**

- 未知中断和未知异常都输出完整 trapframe。

**遇到的问题：**

- 问题：评分脚本会直接构造 `tf.cause = 20` 调用 `exception_handler()`，期望看到 `print_trapframe()` 输出的 `cause 0x00000014`；如果只输出一行自定义信息，兜底路径就没有被验证。

**问题解决策略：**

- 严格按 Spec 实现：未知中断输出提示后再 `print_trapframe(tf)`；未知异常只调用 `print_trapframe(tf)`，不额外增加输出。

**最终结果：**

- 编译通过；
- `[Test 2] Illegal instruction return: PASS (25/25)`；
- `[Test 3] Sustained supervisor timer: PASS (40/40, ticks=200)`。

**关键改进点总结：**

1. 中断号必须屏蔽 `scause` 最高位。
2. 时钟中断必须每次重新 arm，不能只设置一次。
3. `epc` 增量必须按指令长度计算，不能固定为 4。
4. 未知 Trap 的兜底输出要保留 `print_trapframe()`，它是调试和评分的重要接口。

---

### Lab2 代码衔接与启动兼容

Lab3 模板中有多处 `lab2` 占位，需要把小组 Lab2 的成果填回，否则工程无法完成内存初始化。

| 文件                       | 处理内容                                                                                                         |
| ------------------------ | ------------------------------------------------------------------------------------------------------------ |
| `kern/mm/pmm.c`          | 补回 `page_init()`：从 DTB 读取物理内存范围，把页描述符数组放在内核镜像之后，保留 OpenSBI/内核/页数组所占页，剩余页交给 `init_memmap()`                   |
| `kern/mm/memlayout.h`    | 补回 `KERNEL_BEGIN_PADDR`、`KERNEL_BEGIN_VADDR`，供 `page_init()` 使用                                              |
| `kern/mm/best_fit_pmm.c` | 补回 Lab2 的 Best-Fit 实现（`best_fit_init`、`init_memmap`、`alloc_pages`、`free_pages`、`nr_free_pages` 及 manager 结构） |
| `kern/init/entry.S`      | 在启动页表中增加根页表项 2，把 VA `0x80000000` 开始的 1 GiB 恒等映射到 PA `0x80000000`                                             |

Lab3 的 `init_pmm_manager()` 仍然选择模板的 `default_pmm_manager`（First-Fit），因此运行日志中显示 `memory management: default_pmm_manager`；Best-Fit 代码按 Lab3 模板的 `lab2` 占位要求补全，便于后续切换和对比，但不影响本实验的 Trap 测试。

启动页表的恒等映射是 Lab2 已经验证过的兼容修改。QEMU 从物理地址 `0x80200000` 开始执行，`entry.S` 写入 `satp` 后，下一条指令仍然按物理 PC 取指；如果只映射高地址 `0xffffffffc0000000`，开启分页的瞬间就会取指异常。增加根页表项 2 后，从“开分页”到“跳转到高虚拟地址”之间的代码仍然可执行。

另外，本机 QEMU 10.2.1 + OpenSBI 1.8.1 下，`-bios default -device loader,file=...,addr=0x80200000` 的启动方式会让 OpenSBI 的 `Domain0 Next Address` 变成 0，内核根本不会被执行。Lab2 工程已经采用 `-kernel` 方式，因此 Lab3 的 `Makefile` 和 `tools/grade.sh` 也统一改为：

```sh
qemu-system-riscv64 -machine virt -nographic -bios default -kernel bin/ucore.img ...
```

这一修改只影响本机启动方式，不改变内核代码和评分逻辑。

---

## 五、测试与验证

### 5.1 编译与运行

在 `code/lab3` 下执行 `make`，生成 `bin/kernel` 和 `bin/ucore.img`；执行 `make qemu` 后，串口依次输出 DTB 信息、内核符号、物理内存初始化、`check_alloc_page() succeeded!`、`++ setup timer interrupts`，随后周期性输出 `100 ticks`。完整输出见 [LAB3_运行输出.txt](./LAB3_运行输出.txt)。

关键片段：

```text
memory management: default_pmm_manager
Physical memory: 0x0000000080000000 - 0x0000000087ffffff, size: 0x0000000008000000 bytes
npage: 0x88000, pages: 0xffffffffc0208000 (physical 0x0000000080208000)
Free physical memory: 0x0000000080348000 - 0x0000000087ffffff, 31928 pages
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0206000
satp physical address: 0x0000000080206000
++ setup timer interrupts
100 ticks
100 ticks
...
```

### 5.2 评分结果

执行 `make grade`，评分脚本会复制一份临时工程，注入 `kern/test` 测试代码，分别用三个宏编译并运行 QEMU。完整输出见 [LAB3_grade输出.txt](./LAB3_grade输出.txt)。

| 测试用例                                               | 检查内容                           | 结果           |
| -------------------------------------------------- | ------------------------------ | ------------ |
| trap entry, register restore, and breakpoint EPC   | `ebreak` 前后 26 个寄存器一致、断点返回     | PASS (35/35) |
| illegal instruction EPC advance and return         | 非法指令 `epc` 前进、软件中断/未知中断/未知异常兜底 | PASS (25/25) |
| re-armed supervisor timer and repeated trap return | 时钟中断持续触发、`ticks` 达到 200        | PASS (40/40) |

```text
Building trap entry, register restore, and breakpoint EPC...
  - trap entry, register restore, and breakpoint EPC: PASS (35/35)
Building illegal instruction EPC advance and return...
  - illegal instruction EPC advance and return: PASS (25/25)
Building re-armed supervisor timer and repeated trap return...
  - re-armed supervisor timer and repeated trap return: PASS (40/40)
Total Score: 100/100
```

---

## 六、实验总结与收获

### 对操作系统的理解

#### 1. 重要知识点及其与 OS 原理的对应关系

| 本实验中的知识点                                                        | 含义                                                       | 对应的 OS 原理知识点           | 关系与差异                                                      |
| --------------------------------------------------------------- | -------------------------------------------------------- | ---------------------- | ---------------------------------------------------------- |
| Trap（中断 + 异常）                                                   | RISC-V 把外部中断、指令异常、`ecall`/`ebreak` 统一称为 Trap             | 中断、异常、系统调用/陷入          | OS 原理通常把三者分开讲；RISC-V 用 `scause` 统一编码，处理入口也统一为 `stvec`      |
| 特权级 S/U/M                                                       | 内核运行在 S 模式，用户程序运行在 U 模式，OpenSBI 运行在 M 模式                 | 用户态/内核态                | RISC-V 有三档特权级，且中断委托（`mideleg`/`medeleg`）决定由哪一级处理；OS 原理多讲两态 |
| CSR（`sepc`/`scause`/`stval`/`sstatus`/`sie`/`stvec`/`sscratch`） | 硬件在 Trap 时自动记录 PC、原因、附加信息、中断使能等                          | 中断现场、中断向量表、程序状态字       | 硬件只自动保存少量 CSR，通用寄存器仍需软件用 `SAVE_ALL` 保存                     |
| trapframe 与上下文保存/恢复                                             | 把 32 个 GPR 和 4 个 CSR 按固定布局压栈/出栈                          | 上下文切换                  | 本实验是同一个执行流内的 Trap 上下文，不是进程间切换；没有调度器参与                      |
| `sret`                                                          | 从 S 模式返回，恢复 `sepc` 和 `sstatus.SPIE`                      | 中断返回指令（如 x86 的 `iret`） | RISC-V 用 `sret`，并且特权级恢复由 `sstatus.SPP` 决定                  |
| 时钟中断与 `sbi_set_timer`                                           | 通过 SBI 设置下一次时钟事件，QEMU 中 `timebase = 100000` 对应 10 ms     | 时钟中断、时间片               | 本实验只做计数、打印和关机，没有把时钟中断用于进程调度                                |
| 中断关闭与原子性                                                        | `local_intr_save/restore` 保存并关闭 `sstatus.SIE`，保护内存管理数据结构 | 临界区、互斥                 | 单核上关中断即可；多核还需要自旋锁、原子指令和内存屏障                                |
| 指令长度与 `epc` 更新                                                  | 16 位压缩指令与 32 位指令的 `epc` 增量不同                             | 指令译码、异常恢复              | 本实验用指令低两位判断长度，是异常返回正确性的关键细节                                |
| 汇编与 C 的调用约定                                                     | `a0` 传 trapframe 指针，`sp` 由被调用者保存                         | ABI、函数调用栈              | 汇编入口必须遵守 ABI，否则 C 处理函数无法正确读取参数                             |

#### 2. OS 原理中很重要、但本实验没有对应上的知识点

1. **进程/线程与调度**：本实验的时钟中断只计数和关机，没有进程控制块、就绪队列、时间片轮转和上下文切换。
2. **用户态程序与系统调用**：本实验一直在 S 模式运行，没有 U 模式用户程序，也没有完整的 `ecall` 系统调用路径和返回值处理。
3. **缺页异常与按需分页**：本实验的页表只建立启动映射，没有页错误处理、页面置换和按需分配。
4. **外设中断与设备驱动**：只处理了时钟中断和软件中断，没有外部设备中断、中断控制器和驱动模型。
5. **多核与并发同步**：只考虑了单核关中断，没有多核启动、核间中断、自旋锁、原子指令和内存一致性。
6. **进程间通信与信号**：没有管道、共享内存、信号等机制。
7. **文件系统与系统调用接口**：没有文件描述符、VFS、磁盘驱动等。
8. **完整的虚拟内存管理**：没有用户地址空间、`mmap`、写时复制等。

### AI 协作开发的经验

1. **Spec 比“让 AI 写一个中断处理”更重要**：把 trapframe 布局、偏移、调用约定、不能修改的接口和验收标准写清楚，生成结果才可控。尤其是 `sp` 的特殊处理和 `epc` 增量这类细节，如果不写进 Spec，AI 很容易给出“看起来对、跑起来错”的代码。
2. **先读框架，再写提示词**：`SAVE_ALL`、`trap_dispatch`、`print_trapframe`、`clock_set_next_event` 都是已有接口，提示词里直接引用这些接口和字段名，比让 AI 自由发挥更可靠。
3. **用真实测试闭环**：本实验的三组评分用例分别覆盖寄存器恢复、异常返回和持续时钟中断。每次修改后都在 WSL 中重新 `make grade`，比只看代码更有效。
4. **环境兼容问题要单独记录**：`sbadaddr` 与 `stval` 的差异、QEMU 10 的 `-device loader` 启动问题、开启分页瞬间的取指问题，都不是算法错误，但会直接导致“编译不过/跑不起来”。把这类问题写进报告，比只写最终代码更有价值。
5. **不要为了通过测试而写死常量**：例如 `epc += 4` 可以通过当前用例，但不符合 RISC-V 压缩指令规则；最终改为按指令编码判断长度，代码才具有通用性。

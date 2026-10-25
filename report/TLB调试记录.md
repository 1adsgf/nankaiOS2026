# TLB 查询、回填与命中调试记录

本记录补充 [页表调试记录](页表调试记录.md)：那份记录覆盖“访存 → 页表遍历 → 物理地址核对”，本记录继续跟踪 **TLB 层**，包括 TLB 未命中时如何进入页表查询、翻译成功后如何回填、后续访问如何命中，以及 QEMU 的软件 TLB 与真实 CPU TLB 的差异。

## 实验信息

- 姓名、学号：李梦涛，2412483
- 调试日期：2026-10-25
- 内核：仓库内 `code/lab2`，`riscv64-unknown-elf-gcc 16.1.0` 编译
- QEMU：`10.2.1`（源码编译的调试版，`--enable-debug`，未 `make install`）
- 宿主 GDB：Ubuntu GDB 17.1，调试 x86-64 的 QEMU 进程
- 交叉 GDB：`gdb-multiarch`（用于确认客体 PC/satp；本次 TLB 证据主要在宿主侧采集）

> 版本说明：页表调试记录使用 QEMU 7.0.0，本记录使用 QEMU 10.2.1。函数调用关系一致，但 10.2.1 中 TLB 查找被拆分到 `mmu_lookup1` / `probe_access_internal`，行号与 7.0.0 不同。下文行号均指 10.2.1。

## 一、TLB 查找在 QEMU 源码中的位置

一次数据写（`sd`）的慢路径：

```
helper_stq_mmu()                    accel/tcg/ldst_common.c.inc:107
 └─ do_st8_mmu()                    accel/tcg/cputlb.c:2793
     └─ mmu_lookup()                accel/tcg/cputlb.c:1730
         └─ mmu_lookup1()           accel/tcg/cputlb.c:1641
             │  tlb_read_idx(entry, MMU_DATA_STORE)     读取 CPUTLBEntry.addr_write
             │  if (!tlb_hit(tlb_addr, addr))            比较页地址，未命中才继续
             └─ tlb_fill_align()    accel/tcg/cputlb.c:1237
                 └─ ops->tlb_fill() accel/tcg/cputlb.c:1255
                     └─ riscv_cpu_tlb_fill()   target/riscv/cpu_helper.c:1761
                         └─ get_physical_address()  target/riscv/cpu_helper.c:1187
                             └─ tlb_set_page()  accel/tcg/cputlb.c:1200（回填）
```

取指路径同理，只是入口换成 `get_page_addr_code_hostp() → probe_access_internal()`（`cputlb.c:1359`），在 `cputlb.c:1373` 先做 `tlb_hit_page()` 判断，未命中才 `tlb_fill_align()`。

而**命中的快路径不在 C 代码里**：TCG 在翻译访存指令时就生成了 inline 的 TLB 检查代码（x86-64 后端见 `tcg/i386/tcg-target.c.inc` 的 `tcg_out_qemu_ld/st`，约 2183–2238 行）：

1. `(vaddr >> (TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS)) & mask` 得到表项下标；
2. 读 `CPUTLBEntry.addr_write`（或 `addr_read`），与按页对齐后的 vaddr 比较；
3. 相等则直接用 `vaddr + addend` 得到宿主内存地址，**不进入任何 C 函数**；不等才跳到慢路径调用 helper。

`CPUTLBEntry` 只有 4 个 `uintptr_t`（`include/exec/tlb-common.h:25`）：`addr_read`、`addr_write`、`addr_code`、`addend`。

## 二、实测：一次 TLB 未命中到回填

沿用页表调试记录选定的访存：`kern_init+22` 的 `sd ra,8(sp)`（`0xffffffffc02000ec`），`sp = 0xffffffffc0205ff0`，目标虚拟地址 `0xffffffffc0205ff8`，`satp = 0x8000000000080206`。

### 1. 命中检查：表项为空

在 `mmu_lookup1` 的 `tlb_hit` 判断处设条件断点（地址落在栈页）：

```
[T1] mmu_lookup1: addr=0xffffffffc0205ff8 access_type=1 tlb_addr=0xffffffffffffffff -> MISS
```

`access_type=1` 即 `MMU_DATA_STORE`；`tlb_addr=0xffffffffffffffff` 是全 1，等于 `tlb_entry_is_empty()` 判定的空表项，所以这次必然未命中。

> 只有 `[T1]` 一次。原因见第三节：之后同一页的访问由 TCG 生成的 fast path 直接命中，不再调用 `mmu_lookup1`。

### 2. 进入 QEMU 的“硬件 MMU”

```
===== [B] TLB miss -> riscv_cpu_tlb_fill(address=0xffffffffc0205ff8, size=8,
        access_type=1, mmu_idx=1, probe=0)
      guest pc=0xffffffffc02000d6 satp=0x8000000000080206
#0  riscv_cpu_tlb_fill (...) at ../target/riscv/cpu_helper.c:1767
#1  tlb_fill_align (...) at ../accel/tcg/cputlb.c:1255
#2  mmu_lookup1 (...) at ../accel/tcg/cputlb.c:1656
#3  mmu_lookup (...) at ../accel/tcg/cputlb.c:1748
#4  do_st8_mmu (...) at ../accel/tcg/cputlb.c:2793
#5  helper_stq_mmu (...) at ../accel/tcg/ldst_common.c.inc:107
#6  code_gen_buffer ()
#7  cpu_tb_exec (...) at ../accel/tcg/cpu-exec.c:439
#8  cpu_loop_exec_tb (...) at ../accel/tcg/cpu-exec.c:889
```

两个细节：

- `access_type=1`（`MMU_DATA_STORE`）、`size=8`，与 `sd` 指令一致；
- `env->pc = 0xffffffffc02000d6` 是当前翻译块（TB）的起点，不是 `0x...00ec` 那条指令本身；具体指令地址由 `ra`（helper 返回地址）反映，实际访问地址仍是 `0xffffffffc0205ff8`。

### 3. 页表查询（与页表调试记录一致）

```
[B1] 读 satp：base=0x80206000, vm=8, levels=3, ptidxbits=9, ptesize=8, va_bits=39
[B2] level i=0: idx=0x1ff, pte_addr=0x80206ff8
[B3] 读出 pte=0x200000cf, ppn=0x80000 (V=1,R=1,W=1,X=1)
[B4] 命中叶子 PTE: pte=0x200000cf, ppn=0x80000, ptshift=18 (本项覆盖 2^18 个 4KiB 页)
[B5] 合成物理地址: *physical=0x80205ff8, prot=7 (1=READ,2=WRITE,4=EXEC)
```

`satp.PPN = 0x80206` → 根表 `0x80206000`；根索引 `0x1ff` → PTE 地址 `0x80206ff8`；PTE `0x200000cf` 的 R/W/X 非 0，是 1 GiB 叶子，所以只查一级。`prot=7` 表示读写执行都允许。

### 4. 回填软件 TLB

```
[C] tlb_set_page(vaddr_page=0xffffffffc0205000, pa_page=0x80205000,
    prot=7, mmu_idx=1, tlb_size=0x1000)
[D] 扫描 fast TLB（NB_MMU_MODES=22）中 vpage=0xffffffffc0205000 的条目：
    f[20][5]: addr_write=0xffffffffc0205000, addend=0x7fffb2200000
```

两个下标都可以算出来：

- **fast 数组下标 20**：`mmuidx_to_fast_index(mmu_idx) = NB_MMU_MODES - 1 - mmu_idx`。RISC-V 的 `NB_MMU_MODES = 22`，`mmu_idx=1`（S 模式）→ `22-1-1 = 20`；
- **表项下标 5**：`(vaddr >> TARGET_PAGE_BITS) & (mask >> CPU_TLB_ENTRY_BITS)`，`mask=0x1fe0` → 256 项；`(0xffffffffc0205000 >> 12) & 0xff = 0x05`。

`addr_write` 记录虚拟页号，`addend` 记录该页在 QEMU 宿主进程中的地址偏移，命中时 `vaddr + addend` 就是宿主内存指针。注意 `tlb_size=0x1000`：PTE 虽然是 1 GiB 大页，QEMU 的 fast TLB 仍按 4 KiB 粒度回填。

### 5. 第二个实例：数据读取设备树

```
[E] dtb_init 的数据访存: vaddr_page=0xffffffffc7e00000,
    pa_page=0x87e00000, prot=7, tlb_size=0x1000
```

与 `dtb_init()` 中 `boot_dtb + PHYSICAL_MEMORY_OFFSET` 的计算、以及串口输出的 `DTB Address: 0x87e00000` 一致，说明 TLB 回填的物理页号正确。

## 三、命中路径：为什么 `[T]` 只出现一次

`[T1]` 之后，内核继续执行 `memset`、`print_kerninfo` 等函数，栈页 `0xffffffffc0205000` 会被反复读写，但 `[T]` 没有了。原因不是“没有访问”，而是：

- 第一次未命中后，`tlb_set_page()` 已把该页写入 fast TLB（`f[20][5]`）；
- 之后同页访问由 **TCG 生成的指令** 直接完成 `addr_write` 比较并命中，只走 `vaddr + addend`，根本不调用 `mmu_lookup1()`，所以 C 断点不会触发。

这正好对应源码：`mmu_lookup1()` 里的 `if (!tlb_hit(...))` 是慢路径入口，而快路径在 `tcg_out_qemu_ld/st` 生成的代码里。也就是说，QEMU 对被模拟 CPU 的“TLB 命中”是一次**宿主指令级的 inline 检查**，而“TLB 未命中”才落到 C 实现的页表查询。

可以进一步验证表项内容：`addr_write=0xffffffffc0205000` 与虚拟页号相同（低 12 位无标志），`addend=0x7fffb2200000`；若命中，`0xffffffffc0205ff8 + addend` 就是该字节在 QEMU 进程中的地址。

## 四、未开启分页时的对比

在 QEMU 复位后的第一次取指（M 模式，`satp=0`）处：

```
===== [A] get_physical_address: mode=3 addr=0x1000 mmu_idx=3 access_type=2
#0  get_physical_address (...) at ../target/riscv/cpu_helper.c:1226
#1  riscv_cpu_tlb_fill (...) at ../target/riscv/cpu_helper.c:1848
#2  tlb_fill_align (...) at ../accel/tcg/cputlb.c:1255
#3  probe_access_internal (...) at ../accel/tcg/cputlb.c:1375
#4  get_page_addr_code_hostp (...) at ../accel/tcg/cputlb.c:1545
#5  get_page_addr_code (...) at ../accel/tcg/internal-common.h:108
#6  tb_htable_lookup (...) at ../accel/tcg/cpu-exec.c:203
#7  tb_lookup (...) at ../accel/tcg/cpu-exec.c:248
#8  cpu_exec_loop (...) at ../accel/tcg/cpu-exec.c:964
[A] *physical=0x1000 prot=7  (M 模式 / bare：不做页表翻译，VA 直接等于 PA)
```

`mode=3` 即 `PRV_M`，`get_physical_address()` 在 `cpu_helper.c:1226` 的 `mode == PRV_M` 分支直接返回 `*physical = addr`，不做页表遍历。但与真实 CPU 不同的是：QEMU 仍然经过 `tlb_fill` 并在成功后调用 `tlb_set_page(0x1000 -> 0x1000)`，因为 TCG 的统一访存路径依赖这张软件 TLB；真实 CPU 在 Bare 模式根本不查 TLB。

## 五、QEMU 软件 TLB 与真实 CPU TLB 的差异

| 维度 | 真实 CPU TLB | QEMU（TCG softmmu） |
|------|--------------|---------------------|
| 结构 | 硬件 CAM/组相联，并行比较 VPN | 每个 MMU 索引一组直接映射数组 `CPUTLBEntry`，冲突即覆盖 |
| 表项内容 | VPN + PPN + 权限/ASID | `addr_read/write/code`（虚拟页号 + 标志）+ `addend`（到宿主地址的偏移） |
| 填充方式 | MMU 硬件查表后自动填充 | 未命中时由 C 代码 `tlb_fill` → `riscv_cpu_tlb_fill` → `tlb_set_page` 填充 |
| 命中路径 | 硬件比较，流水线内完成 | TCG 生成的宿主指令 inline 比较；未命中才调用 C helper |
| Bare 模式 | 不查 TLB | 仍走 `tlb_fill`/`tlb_set_page` |
| 粒度 | 支持多种页大小，按需合并 | 默认 4 KiB（`tlb_size` 由 PMP 等决定），大页仍按 4 KiB 回填 |
| 失效 | `sfence.vma` 由硬件执行 | 写 `satp`、`sfence.vma` 触发 `tlb_flush`，并失效相关翻译块（TB） |
| 与代码缓存关系 | 独立 | 强耦合：映射变化必须同时 flush TB，否则会继续执行旧宿主代码 |

结论：QEMU 的 TLB 只保证**功能等价**，不模拟真实 TLB 的相联度、替换策略和 ASID；它同时承担“地址翻译缓存 + 代码翻译缓存一致性”的职责。

## 六、为什么只查了一级页表

根 PTE `0x200000cf` 的 R/W/X 非 0，是 1 GiB 叶子（`ptshift=18`），所以 `i=0` 就跳 `goto leaf`。若 PTE 是目录项（R=W=X=0），`get_physical_address()` 会执行 `base = ppn << 12`、`ptshift -= 9` 继续下一级，依次对应 1 GiB / 2 MiB / 4 KiB。本次与页表调试记录的结论一致；lab4 建立 4 KiB 映射后即可看到三级循环。

## 七、遇到的问题与 AI 协作

| 问题 | 原因与处理 |
|------|-----------|
| 宿主 GDB 无法 attach QEMU | WSL `kernel.yama.ptrace_scope=1`，`sudo` 需要交互密码。改为 `gdb --args qemu ...` 让 QEMU 成为 GDB 的子进程，效果等价。 |
| `Cannot access memory at address 0xffffffffc0201230` | 断点下在 `riscv_cpu_tlb_fill` 的函数开括号（1764 行），此时 `env` 尚未赋值；GDB 把垃圾值当指针。改到 1767 行（`env = &cpu->env` 之后）即可。 |
| `next` 写在 breakpoint 的 `commands` 里会中断脚本 | 改为“先 `run` 停住，再用线性命令单步，最后 `continue`”。 |
| 扫描 fast TLB 找不到条目 | 起初只扫描 `f[0..3]`；查 `ptype CPUTLB` 后发现 RISC-V 的 `NB_MMU_MODES=22`，S 模式的 fast 下标是 `21-1=20`，改为扫描 22 组后找到 `f[20][5]`。 |
| 客体 GDB `continue` 报 `Cannot execute this command while the target is running` | 本次 TLB 证据改在宿主侧采集；客体状态已由页表调试记录（`set remotetimeout unlimited`）覆盖，两端结论一致。 |

## 八、复现方式与日志

```bash
cd code/lab2
make    # 生成 bin/ucore.img

gdb -q -batch -nx -x ../../report/logs/tlb_host.gdb --args \
    ~/qemu-src/qemu-10.2.1/build/qemu-system-riscv64 \
    -machine virt -display none -serial file:/tmp/tlb_serial.log \
    -bios default -kernel bin/ucore.img
```

- 调试脚本：[`logs/tlb_host.gdb`](logs/tlb_host.gdb)
- 宿主 GDB 完整输出：[`logs/tlb_host_gdb.log`](logs/tlb_host_gdb.log)
- 同一轮运行的串口输出：[`logs/tlb_serial.log`](logs/tlb_serial.log)（在 `dtb_init` 第一次访存处结束调试，只输出到 `DTB Init` 之前）

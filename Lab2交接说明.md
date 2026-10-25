# Lab2 交接说明

交接人：2411584 马锐。日期：2026-10-25。

这次补了能编译运行的完整工程、Buddy 实现和自测，以及页表查询调试记录。

## 从哪里看

- 实际编译目录是 `code/lab2`，从这里运行 `make` 和 `make qemu`。当前 `pmm.c` 选择的是 `buddy_pmm_manager`。
- 队友原来的四个文件仍保留在 `code/练习`，本次没有覆盖。内存初始化和 Best-Fit 已复用到完整工程中；SLUB 还只在原目录里，尚未接入这套运行工程。
- [实验报告](report/report.md)新增 Buddy 说明和页表调试部分；[提示词报告](report/prompt.md)末尾是本次实际使用的两份 Buddy 提示词。
- [页表调试记录](report/页表调试记录.md)包含五张实测截图、调用路径和地址计算。结果是 `0xffffffffc0205ff8 -> 0x80205ff8`，使用根级 1 GiB 叶子映射。
- [Buddy 验证输出](report/Buddy验证输出.txt)记录助手在本机 WSL 执行的独立验证。内核六类自测、宿主 3000 次混合操作、四类非法释放检查，以及 QEMU 的 32/64/96/128 MiB 配置均通过。这不是 `make grade` 的成绩。

## 怎么运行

在 WSL 中先加载自己机器的 RISC-V 和 QEMU 环境。本机使用：

```bash
source /home/mamou/.config/compiler-course/riscv-env.sh
```

在仓库根目录验证 Buddy：

```bash
make -C code/lab2 OBJDIR=obj/buddy-check BINDIR=bin/buddy-check
python3 debug/check_buddy.py
```

脚本中 QEMU 的路径，以及 VS Code 配置里的 GDB、QEMU 源码路径，都按本机环境填写，换电脑需要调整。运行截图中的源码行号对应 QEMU 7.0.0。

VS Code 使用 WSL 窗口打开仓库内的 `lab2调试.code-workspace`。先启动配置 1（宿主），再启动配置 2（RISC-V）。宿主断点暂停时，客体等待回复属于正常情况；客体已设置 `set remotetimeout unlimited`。查看局部变量前，要选中断点线程里正确的函数栈帧。详细操作见 [开始调试](debug/开始调试.txt)。

## 后面需要小组确认

1. 原仓库已有 Buddy 提示词和运行截图，但缺少源码。本次补的是一份可运行实现；先和原实现者核对采用哪一版，再统一集成，避免两边继续分别修改。
2. TLB 查询、回填、命中路径及其与真实 CPU 的区别，仍需负责该部分的同学补充。我的记录完成到页表查询与物理地址核对。
3. 将队友的 SLUB 接入完整工程并验证；复查 Best-Fit 初始化和释放函数的尾插分支，插入后缺少退出循环的地方可能重复处理新节点。本次未改该算法。
4. 正式报告中的第三位成员信息、正式分工、整体逻辑和总结等仍需组内补齐。原有部分代码注释还带 `YOUR CODE`，由对应负责人核对学号。
5. 本次没有运行 `make grade`，已有评分脚本和框架测试保持原样。最终整合后再按课程要求验证并提交。

工程还包含两处启动兼容调整：Makefile 使用 `-kernel` 配合本机 OpenSBI；启动页表增加低地址恒等映射，保证开启分页到跳往高地址之间的代码可执行。不要只拷贝 Buddy 文件就丢掉完整工程中的这些准备。

`bin`、`obj` 和调试日志不入库，可以重新生成。两个 `debug/buddy-…提示词.txt` 长版文件是早期参考稿，实际使用的版本以 `report/prompt.md` 为准。

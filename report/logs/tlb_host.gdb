set pagination off
set confirm off
set debuginfod enabled off
set logging file /tmp/tlb_hostgdb.log
set logging overwrite on
set logging enabled on

set $n = 0

# [A] 未开启分页（M 模式 / satp=0）的第一次访存
break target/riscv/cpu_helper.c:1226
run

printf "\n===== [A] get_physical_address: mode=%d addr=0x%lx mmu_idx=%d access_type=%d\n", mode, addr, mmu_idx, access_type
bt 9
next
next
next
printf "[A] *physical=0x%lx prot=%d  (M 模式 / bare：不做页表翻译，VA 直接等于 PA)\n", *physical, *ret_prot
delete 1

# [T] 数据访存的软件 TLB 命中检查（mmu_lookup1 内的 if (!tlb_hit(...))）
break accel/tcg/cputlb.c:1653 if addr >= 0xffffffffc0205000 && addr < 0xffffffffc0206000
commands
  silent
  set $n = $n + 1
  if $n <= 8
    printf "[T%d] mmu_lookup1: addr=0x%lx access_type=%d tlb_addr=0x%lx -> %s\n", $n, addr, access_type, tlb_addr, (((tlb_addr & 0xfffffffffffff040) == (addr & 0xfffffffffffff000)) ? "HIT" : "MISS")
  end
  continue
end

# [B] 第一次写栈页：TLB 未命中 -> riscv_cpu_tlb_fill
break target/riscv/cpu_helper.c:1767 if address == 0xffffffffc0205ff8
commands
  silent
  printf "\n===== [B] TLB miss -> riscv_cpu_tlb_fill(address=0x%lx, size=%d, access_type=%d, mmu_idx=%d, probe=%d)\n", address, size, access_type, mmu_idx, probe
  printf "      guest pc=0x%lx satp=0x%lx\n", env->pc, env->satp
  bt 9
  continue
end

# [B1] 读 satp，得到根页表物理地址与分页模式
break target/riscv/cpu_helper.c:1290 if addr == 0xffffffffc0205ff8
commands
  silent
  printf "[B1] 读 satp：base=0x%lx, vm=%d, levels=%d, ptidxbits=%d, ptesize=%d, va_bits=%d\n", base, vm, levels, ptidxbits, ptesize, va_bits
  continue
end

# [B2] 每一级页表：索引与页表项地址
break target/riscv/cpu_helper.c:1356 if addr == 0xffffffffc0205ff8
commands
  silent
  printf "[B2] level i=%d: idx=0x%lx, pte_addr=0x%lx\n", i, idx, (pte_addr ? pte_addr : base + idx * ptesize)
  continue
end

# [B3] 读出 PTE 并取出 PPN
break target/riscv/cpu_helper.c:1408 if addr == 0xffffffffc0205ff8
commands
  silent
  printf "[B3] 读出 pte=0x%lx, ppn=0x%lx (V=%d,R=%d,W=%d,X=%d)\n", pte, ppn, (int)(pte & 1), (int)((pte >> 1) & 1), (int)((pte >> 2) & 1), (int)((pte >> 3) & 1)
  continue
end

# [B4] 命中叶子 PTE
break target/riscv/cpu_helper.c:1432 if addr == 0xffffffffc0205ff8
commands
  silent
  printf "[B4] 命中叶子 PTE: pte=0x%lx, ppn=0x%lx, ptshift=%d (本项覆盖 2^%d 个 4KiB 页)\n", pte, ppn, ptshift, ptshift
  continue
end

# [B5] 合成物理地址
break target/riscv/cpu_helper.c:1609 if addr == 0xffffffffc0205ff8
commands
  silent
  printf "[B5] 合成物理地址: *physical=0x%lx, prot=%d (1=READ,2=WRITE,4=EXEC)\n", *physical, prot
  continue
end

# [C] 翻译成功，回填软件 TLB
break target/riscv/cpu_helper.c:1876 if address == 0xffffffffc0205ff8
commands
  silent
  printf "[C] tlb_set_page(vaddr_page=0x%lx, pa_page=0x%lx, prot=%d, mmu_idx=%d, tlb_size=0x%lx)\n", address & ~(tlb_size - 1), pa & ~(tlb_size - 1), prot, mmu_idx, tlb_size
  continue
end

# [D] tlb_set_page 返回后，扫描 4 组 fast TLB，找到刚写入的栈页条目
break target/riscv/cpu_helper.c:1877 if address == 0xffffffffc0205ff8
commands
  silent
  printf "[D] 扫描 fast TLB（NB_MMU_MODES=22）中 vpage=0xffffffffc0205000 的条目：\n"
  set $k = 0
  while $k < 22
    set $i = 0
    while $i < 256
      if (cs->neg.tlb.f[$k].table[$i].addr_write & 0xfffffffffffff040) == 0xffffffffc0205000
        printf "    f[%d][%d]: addr_write=0x%lx, addend=0x%lx (addend + vaddr = host 地址)\n", $k, $i, cs->neg.tlb.f[$k].table[$i].addr_write, cs->neg.tlb.f[$k].table[$i].addend
      end
      set $i = $i + 1
    end
    set $k = $k + 1
  end
  continue
end

# [E] 另一个访存实例：dtb_init 读取 DTB，然后结束
break target/riscv/cpu_helper.c:1876 if address == 0xffffffffc7e00000
commands
  silent
  printf "[E] dtb_init 的数据访存: vaddr_page=0x%lx, pa_page=0x%lx, prot=%d, tlb_size=0x%lx\n", address & ~(tlb_size - 1), pa & ~(tlb_size - 1), prot, tlb_size
  printf "[F] 结束调试\n"
  kill
  quit
end

continue

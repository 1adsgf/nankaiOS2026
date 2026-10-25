# Native x86-64 GDB commands for QEMU 7.0.0.
# This file prepares a breakpoint; it does not perform the experiment.
set pagination off
set print pretty on
handle SIGPIPE nostop noprint
directory /home/mamou/src/compiler-course-env/qemu-7.0.0
set $lab2_va = 0
break get_physical_address if $lab2_va != 0 && addr == $lab2_va && !is_debug && (env->satp >> 60) == 8

define lab2-arm
  set $lab2_va = (unsigned long long)$arg0
  printf "Watching guest virtual address: 0x%llx\n", $lab2_va
end
document lab2-arm
Set the guest virtual address whose Sv39 page walk should stop in QEMU.
Usage: lab2-arm 0xffffffffc0200000
Select a real load/store effective address from your own stopped guest.
The breakpoint excludes debugger-originated translations (is_debug).
end

define lab2-disarm
  set $lab2_va = 0
  printf "Page-walk address filter disabled.\n"
end

define lab2-context
  printf "Guest virtual address: 0x%llx\n", (unsigned long long)addr
  printf "Guest satp: 0x%llx\n", (unsigned long long)env->satp
  print access_type
  print first_stage
  print is_debug
  bt 5
end
document lab2-context
Run while stopped inside get_physical_address to inspect its input context.
Locals such as base/idx/pte are meaningful only after their assignment runs.
end

printf "Lab2 host debugger ready. Use lab2-arm ADDRESS after selecting a guest access.\n"

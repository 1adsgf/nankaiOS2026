#!/usr/bin/env bash
set -euo pipefail

debug_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
kernel_root="$debug_root/code/lab2"
source /home/mamou/.config/compiler-course/riscv-env.sh
cd "$debug_root"

case "${1:-check}" in
  build)
    test -f "$kernel_root/Makefile" || {
      printf '%s\n' 'Missing complete kernel source: code/lab2/Makefile' >&2
      exit 1
    }
    make -C "$kernel_root"
    ;;
  check)
    command -v make gdb riscv64-unknown-elf-gdb qemu-system-riscv64
    gdb --version | sed -n '1p'
    qemu-system-riscv64 --version | sed -n '1p'
    test -f "$kernel_root/Makefile"
    test -s "$kernel_root/bin/kernel"
    test -s "$kernel_root/bin/ucore.img"
    readelf -S /opt/qemu/bin/qemu-system-riscv64 | grep -E '\.debug_info|\.debug_line'
    printf '%s\n' 'Kernel and debugger files are ready.'
    ss -ltnp | grep ':1234' || true
    ;;
  uart)
    touch "$debug_root/debug/qemu-serial.log"
    tail -n 60 -f "$debug_root/debug/qemu-serial.log"
    ;;
  *)
    printf '%s\n' 'Usage: bash debug/environment.sh [build|check|uart]' >&2
    exit 2
    ;;
esac

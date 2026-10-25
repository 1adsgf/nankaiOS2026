#!/usr/bin/env python3
"""Validate the real Buddy module on the host and in independent QEMU runs."""
import os
import pathlib
import selectors
import subprocess
import tempfile
import time

root = pathlib.Path(__file__).resolve().parent.parent
kernel = root / 'code/lab2'
log = []


def run_host_checks():
    with tempfile.TemporaryDirectory(prefix='lab2-buddy-check-') as temporary:
        executable = str(pathlib.Path(temporary) / 'buddy-check')
        command = ['gcc', '-std=gnu99', '-O0', '-g', '-Wall', '-Werror',
                   '-fno-builtin', '-nostdinc']
        for directory in ['libs', 'kern/debug', 'kern/mm', 'kern/driver']:
            command += ['-I', str(kernel / directory)]
        command += [str(root / 'debug/buddy_host_check.c'),
                    str(kernel / 'kern/mm/buddy_pmm.c'), '-o', executable]
        subprocess.run(command, check=True, capture_output=True, text=True)
        result = subprocess.run([executable], capture_output=True, text=True, timeout=15)
        log.append(result.stdout + result.stderr)
        if result.returncode != 0:
            raise RuntimeError('Host behavior checks failed: ' + result.stdout + result.stderr)
        print(result.stdout.strip(), flush=True)
        for case in ['double-free', 'mismatched-size', 'interior-pointer', 'null-pointer']:
            result = subprocess.run([executable, case], capture_output=True, text=True, timeout=5)
            log.append(case + ':\n' + result.stdout + result.stderr)
            if result.returncode != 2 or 'assertion at ' not in result.stdout:
                raise RuntimeError('Invalid-free defense failed: ' + case)
        print('Invalid-free defenses: PASS (4 separate negative cases).', flush=True)


def run_qemu(memory):
    image = kernel / 'bin/buddy-check/ucore.img'
    if not image.is_file():
        raise RuntimeError('Build first: make OBJDIR=obj/buddy-check BINDIR=bin/buddy-check')
    process = subprocess.Popen([
        '/opt/qemu/bin/qemu-system-riscv64', '-machine', 'virt', '-m', memory,
        '-display', 'none', '-monitor', 'none', '-serial', 'stdio',
        '-bios', 'default', '-kernel', str(image),
    ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    captured = b''
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=0.2):
                chunk = os.read(key.fileobj.fileno(), 8192)
                if not chunk:
                    raise RuntimeError('QEMU exited before initialization finished')
                captured += chunk
            if b'assertion failed' in captured or b'kernel panic' in captured:
                raise RuntimeError('Kernel panic during Buddy check')
            final_line = captured.find(b'satp physical address:')
            if final_line >= 0 and b'\n' in captured[final_line:]:
                for marker in [b'memory management: buddy_pmm_manager',
                               b'buddy_check() succeeded!',
                               b'check_alloc_page() succeeded!']:
                    if marker not in captured:
                        raise RuntimeError('Missing Buddy startup result: ' + repr(marker))
                print('QEMU ' + memory + ': PASS (Buddy selected, callback checks completed).', flush=True)
                return
        raise TimeoutError('QEMU Buddy startup check timed out at ' + memory)
    finally:
        log.append('QEMU ' + memory + ':\n' + captured.decode('utf-8', errors='replace'))
        selector.close()
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


try:
    run_host_checks()
    for memory_size in ['32M', '64M', '96M', '128M']:
        run_qemu(memory_size)
finally:
    (root / 'debug/buddy-validation.log').write_text('\n\n'.join(log), encoding='utf-8')

# Analyzing the faulty Driver Kernel Oops

## Commands executed on the emulated environment
 - Waited the kernel to boot and logged in as root. 
```bash
Welcome to Buildroot
buildroot login: root
Password: 
```

 - Then created the device node for the faulty driver and wrote to it.
```bash
mknod /dev/faulty c 247 0
```

 - echoing "hello_world" to /dev/faulty:
```bash
echo "hello_world" > /dev/faulty
```

 - obtained the kernel oops:
```bash
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b37000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 116 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008e1bd20
x29: ffffffc008e1bd80 x28: ffffff8001b9cf80 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000c x22: 000000000000000c x21: ffffffc008e1bdc0
x20: 000000555ac75a20 x19: ffffff8001ba1800 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008e1bdc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

# objdump Output
 - The objdump output for the faulty driver is as follows:

```bash
coursera/assignment-5-diogomatos3$ aarch64-none-linux-gnu-objdump -S buildroot/output/build/ldd-9f855907e5d023a0f8bd2ee75f500a637e30b3ef/misc-modules/faulty.ko 

buildroot/output/build/ldd-9f855907e5d023a0f8bd2ee75f500a637e30b3ef/misc-modules/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:   d2800001        mov     x1, #0x0                        // #0
   4:   d2800000        mov     x0, #0x0                        // #0
   8:   d503233f        paciasp
   c:   d50323bf        autiasp
  10:   b900003f        str     wzr, [x1]
  14:   d65f03c0        ret
  18:   d503201f        nop
  1c:   d503201f        nop

```

# Analysis
Here is a detailed analysis, with the root cause explanation:

---

## **Root Cause Analysis of Kernel Oops in `faulty_write`**

### **Overview**
The kernel encountered a **NULL pointer dereference** when executing the `echo "hello_world" > /dev/faulty` command. This resulted in an **Oops** message and a system crash.

### **Key Observations**
1. **The Kernel Oops Message:**
   - The crash happened due to an **attempted memory access at address `0x0000000000000000`** (NULL).
   - The backtrace shows the fault occurred in the function **`faulty_write+0x10/0x20 [faulty]`**, which is part of the `faulty.ko` module.

2. **Disassembly of `faulty_write`:**
   ```assembly
   0000000000000000 <faulty_write>:
      0:   d2800001        mov     x1, #0x0                        // #0
      4:   d2800000        mov     x0, #0x0                        // #0
      8:   d503233f        paciasp
      c:   d50323bf        autiasp
     10:   b900003f        str     wzr, [x1]  <-- FAULT OCCURS HERE
     14:   d65f03c0        ret
   ```
   - The instruction at address `0x10` (`str wzr, [x1]`) **attempts to store a zero value (`wzr`) at the memory location pointed to by `x1`**.
   - However, `x1` is explicitly set to `0x0` at instruction `0: d2800001 mov x1, #0x0`.
   - This results in a **write to a NULL pointer**, causing a **level 1 translation fault** in the memory subsystem.

### **Why Did This Happen?**
- The driver is intentionally designed to **write to a NULL pointer**, which is **invalid in the kernel memory space**.
- This results in a **segmentation fault (SEGV)** at runtime.
- The `objdump` output confirms that the `faulty_write` function directly **manipulates registers to force a NULL write**, likely for demonstration purposes in a learning environment.

### **Conclusion**
The **root cause** of this issue is an **explicit attempt to write to address `0x0` in `faulty_write`**. This leads to a **NULL pointer dereference**, causing the kernel to crash. This behavior is expected from a module designed to demonstrate faulty driver behavior.
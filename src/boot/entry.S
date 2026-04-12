/*
 * MicroNT Multiboot Boot Stub
 *
 * Entry point for QEMU -kernel loading. Multiboot compliant.
 * Sets up the machine state that KiSystemStartup expects:
 *   - 32-bit protected mode (multiboot already does this)
 *   - NT-compatible GDT with correct selectors
 *   - IDT (256 entries, initially empty)
 *   - TSS
 *   - PCR at FS: segment
 *   - Page tables: identity map low 16MB + kernel mapping at 0x80000000
 *   - Calls loader_main() in C to build LOADER_PARAMETER_BLOCK
 *   - Jumps to KiSystemStartup
 */

/* Multiboot header constants */
#define MULTIBOOT_MAGIC     0x1BADB002
#define MULTIBOOT_FLAGS     0x00000003  /* page-align modules, provide meminfo */
#define MULTIBOOT_CHECKSUM  -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

/* NT GDT selector values (from ks386.inc) */
#define KGDT_NULL       0x00
#define KGDT_R0_CODE    0x08
#define KGDT_R0_DATA    0x10
#define KGDT_R3_CODE    0x18
#define KGDT_R3_DATA    0x20
#define KGDT_TSS        0x28
#define KGDT_R0_PCR     0x30
#define KGDT_R3_TEB     0x38
/* 0x40 = reserved (LDT would go here) */
#define KGDT_LDT        0x48
#define KGDT_DF_TSS     0x50
#define KGDT_NMI_TSS    0x58

.section .multiboot, "a"
.align 4
multiboot_header:
    .long MULTIBOOT_MAGIC
    .long MULTIBOOT_FLAGS
    .long MULTIBOOT_CHECKSUM

.section .text
.code32
.globl _start
_start:
    /* Multiboot gives us:
     *   EAX = 0x2BADB002 (multiboot magic)
     *   EBX = pointer to multiboot_info structure
     * We're in 32-bit protected mode, paging disabled, A20 enabled
     */

    /* Write 'M' to VGA to show we got control */
    movw $0x4F4D, 0xB8000

    /* Write to COM1 to show we got control */
    movw $0x3F8, %dx
    movb $'!', %al
    outb %al, %dx

    /* Save multiboot info pointer */
    movl %ebx, multiboot_info_ptr

    /* Set up our own stack */
    movl $boot_stack_top, %esp

    /* Install the NT-compatible GDT */
    movb $'G', %al
    outb %al, %dx
    lgdt gdt_ptr

    /* Reload segment registers with NT selectors */
    movb $'J', %al
    outb %al, %dx
    ljmp $KGDT_R0_CODE, $1f
1:
    movb $'D', %al
    movw $0x3F8, %dx
    outb %al, %dx

    movw $KGDT_R0_DATA, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* FS = PCR (GDT entry not populated yet, but selector is valid — will be
     * set up properly in loader_main before kernel needs it) */
    /* For now, use KGDT_R0_DATA for FS to avoid fault on empty descriptor */
    movw $KGDT_R0_DATA, %ax
    movw %ax, %fs
    movw $0, %ax
    movw %ax, %gs

    /* Load IDT */
    movb $'I', %al
    movw $0x3F8, %dx
    outb %al, %dx
    lidt idt_ptr

    /* Don't load TSS yet — the GDT entry isn't set up.
     * loader_main will set up the TSS GDT entry and load it. */

    movb $'\n', %al
    outb %al, %dx

    /* Call C loader to set up page tables, load PE images, build loader block */
    pushl multiboot_info_ptr
    call loader_main
    /* loader_main returns the address of KiSystemStartup */
    /* It also returns LOADER_PARAMETER_BLOCK pointer in loader_block global */

    /* Push LoaderBlock as argument to KiSystemStartup */
    pushl loader_block
    call *%eax

    /* Should never return */
    cli
halt:
    hlt
    jmp halt


/*
 * GDT - NT-compatible Global Descriptor Table
 *
 * The NT kernel expects specific selectors at specific offsets.
 * Each entry is 8 bytes. The GDT must have entries at:
 *   0x00: Null
 *   0x08: KGDT_R0_CODE  - Ring 0 code (flat 4GB)
 *   0x10: KGDT_R0_DATA  - Ring 0 data (flat 4GB)
 *   0x18: KGDT_R3_CODE  - Ring 3 code (flat 4GB)
 *   0x20: KGDT_R3_DATA  - Ring 3 data (flat 4GB)
 *   0x28: KGDT_TSS      - Task State Segment
 *   0x30: KGDT_R0_PCR   - PCR data segment (base = PCR address)
 *   0x38: KGDT_R3_TEB   - TEB segment (ring 3)
 *   0x40: reserved
 *   0x48: KGDT_LDT      - LDT descriptor
 *   0x50: KGDT_DF_TSS   - Double fault TSS
 *   0x58: KGDT_NMI_TSS  - NMI TSS
 */
.section .data
.align 16
.globl boot_gdt
boot_gdt:
    /* 0x00: Null descriptor */
    .quad 0x0000000000000000

    /* 0x08: KGDT_R0_CODE - Ring 0 code, flat 4GB, 32-bit, DPL=0 */
    .quad 0x00CF9A000000FFFF

    /* 0x10: KGDT_R0_DATA - Ring 0 data, flat 4GB, 32-bit, DPL=0 */
    .quad 0x00CF92000000FFFF

    /* 0x18: KGDT_R3_CODE - Ring 3 code, flat 4GB, 32-bit, DPL=3 */
    .quad 0x00CFFA000000FFFF

    /* 0x20: KGDT_R3_DATA - Ring 3 data, flat 4GB, 32-bit, DPL=3 */
    .quad 0x00CFF2000000FFFF

    /* 0x28: KGDT_TSS - filled in at runtime by set_gdt_entry() */
    .quad 0x0000000000000000

    /* 0x30: KGDT_R0_PCR - filled in at runtime (base = PCR address) */
    .quad 0x0000000000000000

    /* 0x38: KGDT_R3_TEB - filled in at runtime */
    .quad 0x0000000000000000

    /* 0x40: reserved */
    .quad 0x0000000000000000

    /* 0x48: KGDT_LDT - placeholder */
    .quad 0x0000000000000000

    /* 0x50: KGDT_DF_TSS - filled in by kernel */
    .quad 0x0000000000000000

    /* 0x58: KGDT_NMI_TSS - filled in by kernel */
    .quad 0x0000000000000000

    /* Extra entries for V86 mode etc - pad to 0x100 */
    .fill 20, 8, 0
boot_gdt_end:

gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1
    .long boot_gdt

/*
 * IDT - 256 entries, initially zeroed.
 * The kernel (KiSwapIDT) will fill these in.
 */
.align 16
.globl boot_idt
boot_idt:
    .fill 256, 8, 0
boot_idt_end:

idt_ptr:
    .word boot_idt_end - boot_idt - 1
    .long boot_idt

/*
 * TSS - minimal Task State Segment
 */
.align 16
.globl boot_tss
boot_tss:
    .fill 26, 4, 0      /* 104 bytes = standard TSS size */
    .word 0              /* T flag */
    .word 104            /* I/O map base = size of TSS (no I/O bitmap) */
boot_tss_end:

/* Multiboot info pointer */
.globl multiboot_info_ptr
multiboot_info_ptr:
    .long 0

/* Loader block pointer (set by loader_main) */
.globl loader_block
loader_block:
    .long 0

/*
 * Boot stack - 16KB
 */
.section .bss
.align 16
boot_stack:
    .space 16384
.globl boot_stack_top
boot_stack_top:

/*
 * 64→32 mode-drop transition stub.
 *
 * Called from main.c in long mode (post-ExitBootServices). Loads our
 * 32-bit GDT, drops to compatibility mode via far-return, then steps
 * through the formal long-mode-exit sequence:
 *
 *   1. Reload DS/ES/SS/FS/GS with 32-bit descriptors
 *   2. CR0.PG = 0        -- disable paging (now unpaged; identity-mapped)
 *   3. EFER.LME = 0      -- exit long mode
 *   4. CR4.PAE = 0       -- disable PAE (NT 3.5 is non-PAE)
 *   5. CR3 = 32-bit PD   -- our NT page directory
 *   6. CR0.PG = 1        -- re-enable paging, now 32-bit non-PAE
 *   7. LIDT 32-bit IDT   -- (GDT already loaded pre-drop; persists)
 *   8. FS = KGDT_R0_PCR  -- per-CPU PCR pointer
 *   9. LTR KGDT_TSS      -- install 32-bit task state segment
 *  10. Switch to KSEG0 stack
 *  11. clts/fninit/fnclex -- FPU fresh state
 *  12. Push loader_block, call kernel entry (never returns)
 *
 * Addressing discipline:
 *   - In 32-bit compat mode x86 has no RIP-relative; absolute symbol
 *     references become R_X86_64_32 relocations which gnu-efi's PIE
 *     linker refuses to emit.
 *   - Solution: pack all transition state into one trans_block struct,
 *     compute its runtime address via RIP-relative LEA in long mode
 *     into %rbx, then access every field as %ebx+offset in 32-bit
 *     code. %ebx survives the mode switch unchanged.
 *
 * Why every instruction stream in the stub is identity-mapped across
 * the mode transition:
 *   - UEFI x64 identity-maps system RAM in its PML4, so everything
 *     runs at virt == phys before the CR3 swap.
 *   - Between CR0.PG=0 and CR0.PG=1, the CPU fetches from phys directly.
 *   - Our 32-bit PD (via mmu_register_image of the loader image + stack)
 *     also identity-maps those phys ranges, so after the CR3 swap we're
 *     still at the same linear address.
 *
 * Signature (from main.c):
 *   void handoff(unsigned long entry_kseg0,
 *                unsigned long loader_block_kseg0,
 *                unsigned long stack_top_kseg0,
 *                unsigned long pd_phys,
 *                unsigned long gdt_phys,
 *                unsigned long idt_phys);
 * SysV x64: rdi, rsi, rdx, rcx, r8, r9.
 *
 * GDT/IDT bases are passed as phys. The long-mode LGDT uses phys because
 * UEFI's PML4 only identity-maps phys. AFTER the CR3 swap to our NT PD
 * (which maps both identity and KSEG0), we re-LGDT/LIDT with the KSEG0
 * alias of those same phys pages, so the descriptor tables stay reachable
 * when the kernel later switches CR3 to a process PD where only the KSEG0
 * PDEs [512..515] + non-paged range are copied (PROCSUP.C:297).
 */

/* trans_block field offsets. Both long-mode prologue and 32-bit post-
 * drop code use these — every access is %rbx/%ebx + OFFSET, never a
 * symbol reference that would need an absolute 32-bit relocation. */
#define TR_ENTRY   0     /* .quad kernel entry KSEG0 VA                */
#define TR_LPB     8     /* .quad loader_block KSEG0 VA                */
#define TR_STACK   16    /* .quad idle-stack top KSEG0 VA              */
#define TR_PD      24    /* .long NT PD phys (fits in 32 bits)         */
#define TR_GDTR    32    /* .word limit; .quad base=phys (10-byte, long LGDT) */
#define TR_IDTR    42    /* .word limit; .long base=KSEG0 (6-byte, 32-bit LIDT)*/
#define TR_GDTR32  48    /* .word limit; .long base=KSEG0 (6-byte, 32-bit LGDT re-load) */

    .section .text
    .globl handoff
    .type  handoff, @function
    .code64
handoff:
    cli

    /* Resolve trans_block's runtime address once (RIP-relative). From
     * here on it's referenced exclusively as (%rbx) / (%ebx). */
    leaq    trans_block(%rip), %rbx

    /* Stash the six args. The 64-bit stores for entry/lpb/stack keep
     * the upper halves zero (they're KSEG0 VAs that fit in 32 bits). */
    movq    %rdi, TR_ENTRY(%rbx)
    movq    %rsi, TR_LPB(%rbx)
    movq    %rdx, TR_STACK(%rbx)
    movl    %ecx, TR_PD(%rbx)

    /* Fill the GDTR (used by LGDT below — 10-byte form, phys base). */
    movw    $(32*8 - 1), TR_GDTR(%rbx)
    movq    %r8, TR_GDTR+2(%rbx)

    /* Fill the 32-bit GDTR (used by LGDT re-load post-CR3-swap — 6-byte
     * form). Base = KSEG0|gdt_phys so it stays reachable when the kernel
     * later switches CR3 to a process PD where identity map is gone. */
    movw    $(32*8 - 1), TR_GDTR32(%rbx)
    movl    %r8d, %eax
    orl     $0x80000000, %eax
    movl    %eax, TR_GDTR32+2(%rbx)

    /* Fill the 32-bit IDTR (used by LIDT post-drop — 6-byte form,
     * KSEG0|idt_phys for the same survival reason). */
    movw    $(256*8 - 1), TR_IDTR(%rbx)
    movl    %r9d, %eax
    orl     $0x80000000, %eax
    movl    %eax, TR_IDTR+2(%rbx)

    /* LGDT in long mode — 10-byte operand. GDTR survives the mode
     * transition unchanged; 32-bit code uses the same descriptors. */
    lgdt    TR_GDTR(%rbx)

    /* Far-return to 32-bit compat CS (KGDT_R0_CODE = 0x08). %rbx
     * survives the jump; we keep using it in the 32-bit code below. */
    pushq   $0x08
    leaq    compat_entry(%rip), %rax
    pushq   %rax
    lretq

    /*------------------------------------------------------------------
     * From here on: 32-bit compat mode (D=1 CS). Paging still active
     * via UEFI's PML4 until we flip it below. All data references go
     * through %ebx which holds trans_block's linear address.
     *------------------------------------------------------------------*/
    .code32
compat_entry:
    /* Reload data segments with our 32-bit DS (KGDT_R0_DATA = 0x10). */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    xorw    %ax, %ax
    movw    %ax, %fs
    movw    %ax, %gs

    /* CR0.PG = 0 — disable paging. Identity mapping means virt == phys,
     * so the next instruction fetch continues at the same linear addr. */
    movl    %cr0, %eax
    andl    $0x7FFFFFFF, %eax
    movl    %eax, %cr0

    /* Clear EFER.LME (MSR 0xC0000080, bit 8) — exit long mode. */
    movl    $0xC0000080, %ecx
    rdmsr
    andl    $0xFFFFFEFF, %eax
    wrmsr

    /* Normalise CR4 to the OVMF32 post-boot state (0x640 =
     * OSFXSR | OSXMMEXCPT | MCE). OVMF64 leaves extra bits set —
     * PAE (required for long mode), PGE (global-pages cache),
     * PSE (4 MB page support), potentially SMEP/SMAP on newer
     * firmware. NT 3.5's MM walk expects classic non-PAE
     * semantics with no global TLB entries leaking across our
     * CR3 swap; keeping a minimal, predictable CR4 matches the
     * world 32-bit NT was tested against. */
    movl    $0x640, %eax
    movl    %eax, %cr4

    /* Load our 32-bit CR3 = NT PD phys. */
    movl    TR_PD(%ebx), %eax
    movl    %eax, %cr3

    /* CR0.PG = 1 — re-enable paging, now 32-bit non-PAE with our PD. */
    movl    %cr0, %eax
    orl     $0x80000000, %eax
    movl    %eax, %cr0

    /* Re-LGDT with the KSEG0 alias of our GDT. Until this point GDTR
     * held the phys base (needed for the long-mode LGDT, since UEFI's
     * PML4 only identity-maps phys). Post-CR3-swap our NT PD maps both
     * identity and KSEG0 for the GDT's phys page, so either base works
     * here — but we commit to KSEG0 now so GDTR survives once the
     * kernel starts switching CR3 to process PDs (which drop the
     * identity map; PROCSUP.C:297 copies only PDE[512..515] + non-paged
     * range). Segment descriptors themselves (TSS base, PCR base) are
     * already expressed as KSEG0 VAs in the GDT content, so this
     * aligns GDTR with the rest of the NT-facing view. */
    lgdt    TR_GDTR32(%ebx)

    /* Load 32-bit IDT (6-byte operand via %ebx-relative, KSEG0 base). */
    lidt    TR_IDTR(%ebx)

    /* FS = PCR (KGDT_R0_PCR = 0x30). */
    movw    $0x30, %ax
    movw    %ax, %fs

    /* LTR with TSS (KGDT_TSS = 0x28). */
    movw    $0x28, %ax
    ltr     %ax

    /* Switch to KSEG0 idle stack. From here on we must not read
     * anything off the old UEFI stack — main.c's frame is gone. */
    movl    TR_STACK(%ebx), %esp

    /* FPU: clear TS/EM, reinitialize, clear pending exceptions. */
    clts
    fninit
    fnclex

    /* Diagnostic: 'K' to COM1. Matches the old 32-bit handoff marker. */
    movw    $0x3F8, %dx
    movb    $'K', %al
    outb    %al, %dx

    /* Call KiSystemStartup (stdcall, loader_block on stack). Never
     * returns — halt deterministically if it somehow does. */
    pushl   TR_LPB(%ebx)
    movl    TR_ENTRY(%ebx), %eax
    calll   *%eax

    cli
1:  hlt
    jmp     1b
    .size   handoff, .-handoff


/*------------------------------------------------------------------
 * Transition state — populated by handoff's long-mode prologue.
 * Lives in .data so it's part of the loader image and therefore
 * covered by our identity map across the mode switch.
 *------------------------------------------------------------------*/
    .section .data
    .align  8
    .globl  trans_block
trans_block:
    .quad   0          /* TR_ENTRY  = 0  */
    .quad   0          /* TR_LPB    = 8  */
    .quad   0          /* TR_STACK  = 16 */
    .long   0          /* TR_PD     = 24 */
    .long   0          /* pad to align GDTR on 8-byte offset? not required */
    .word   0          /* TR_GDTR  limit = 32 */
    .quad   0          /* TR_GDTR  base  = 34 (total 10 bytes, ends at 42) */
    .word   0          /* TR_IDTR  limit = 42 */
    .long   0          /* TR_IDTR  base  = 44 (ends at 48) */
    .word   0          /* TR_GDTR32 limit = 48 */
    .long   0          /* TR_GDTR32 base  = 50 (ends at 54) */

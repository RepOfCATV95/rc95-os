/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Restore the saved amd64 processor context and resume the kernel.
 */

#include <asm.inc>
#include <ksamd64.inc>

#define HIBER_SPECIAL_CR0          HEX(00)
#define HIBER_SPECIAL_CR2          HEX(08)
#define HIBER_SPECIAL_CR3          HEX(10)
#define HIBER_SPECIAL_CR4          HEX(18)
#define HIBER_SPECIAL_DR0          HEX(20)
#define HIBER_SPECIAL_DR1          HEX(28)
#define HIBER_SPECIAL_DR2          HEX(30)
#define HIBER_SPECIAL_DR3          HEX(38)
#define HIBER_SPECIAL_DR6          HEX(40)
#define HIBER_SPECIAL_DR7          HEX(48)
#define HIBER_SPECIAL_GDTR         HEX(50)
#define HIBER_SPECIAL_IDTR         HEX(60)
#define HIBER_SPECIAL_TR           HEX(70)
#define HIBER_SPECIAL_LDTR         HEX(72)
#define HIBER_SPECIAL_CR8          HEX(A0)
#define HIBER_SPECIAL_GSBASE       HEX(A8)
#define HIBER_SPECIAL_KERNELGSBASE HEX(B0)
#define HIBER_SPECIAL_STAR         HEX(B8)
#define HIBER_SPECIAL_LSTAR        HEX(C0)
#define HIBER_SPECIAL_CSTAR        HEX(C8)
#define HIBER_SPECIAL_SFMASK       HEX(D0)
#define HIBER_CONTEXT_CS           HEX(118)
#define HIBER_CONTEXT_DS           HEX(11A)
#define HIBER_CONTEXT_ES           HEX(11C)
#define HIBER_CONTEXT_FS           HEX(11E)
#define HIBER_CONTEXT_GS           HEX(120)
#define HIBER_CONTEXT_SS           HEX(122)
#define HIBER_CONTEXT_EFLAGS       HEX(124)
#define HIBER_CONTEXT_RAX          HEX(158)
#define HIBER_CONTEXT_RCX          HEX(160)
#define HIBER_CONTEXT_RDX          HEX(168)
#define HIBER_CONTEXT_RBX          HEX(170)
#define HIBER_CONTEXT_RSP          HEX(178)
#define HIBER_CONTEXT_RBP          HEX(180)
#define HIBER_CONTEXT_RSI          HEX(188)
#define HIBER_CONTEXT_RDI          HEX(190)
#define HIBER_CONTEXT_R8           HEX(198)
#define HIBER_CONTEXT_R9           HEX(1A0)
#define HIBER_CONTEXT_R10          HEX(1A8)
#define HIBER_CONTEXT_R11          HEX(1B0)
#define HIBER_CONTEXT_R12          HEX(1B8)
#define HIBER_CONTEXT_R13          HEX(1C0)
#define HIBER_CONTEXT_R14          HEX(1C8)
#define HIBER_CONTEXT_R15          HEX(1D0)
#define HIBER_CONTEXT_RIP          HEX(1D8)
#define HIBER_FX_STATE_OFFSET      HEX(5B0)
#define HIBER_TRAMPOLINE_HAS_FX    HEX(02)
#define HIBER_CR4_PGE              HEX(80)
#define CR0_NPX_MASK               HEX(0E)

.code64

PUBLIC WinLdrResumeProcessorState
WinLdrResumeProcessorState:
    mov rsi, rcx
    mov r14d, edx
    cli

    /* Switch to the saved page tables and invalidate global TLB entries. */
    mov rax, cr4
    and rax, NOT HIBER_CR4_PGE
    mov cr4, rax
    mov rax, [rsi + HIBER_SPECIAL_CR4]
    mov cr4, rax
    mov rax, [rsi + HIBER_SPECIAL_CR3]
    mov cr3, rax

    /* Load the restored descriptor tables and task state. */
#ifdef _USE_ML
    lgdt fword ptr [rsi + HIBER_SPECIAL_GDTR + 6]
    lidt fword ptr [rsi + HIBER_SPECIAL_IDTR + 6]
#else
    lgdt [rsi + HIBER_SPECIAL_GDTR + 6]
    lidt [rsi + HIBER_SPECIAL_IDTR + 6]
#endif

    mov rax, [rsi + HIBER_SPECIAL_GDTR + 8]
    movzx ecx, word ptr [rsi + HIBER_SPECIAL_TR]
    and ecx, HEX(FFF8)
    add rax, rcx
    and byte ptr [rax + 5], HEX(FD)

    mov ax, word ptr [rsi + HIBER_SPECIAL_LDTR]
    lldt ax
    mov ax, word ptr [rsi + HIBER_SPECIAL_TR]
    ltr ax

    /* Restore control, debug, and syscall registers. */
    mov rax, [rsi + HIBER_SPECIAL_CR0]
    and rax, NOT CR0_NPX_MASK
    mov cr0, rax
    mov rax, [rsi + HIBER_SPECIAL_CR2]
    mov cr2, rax
    mov rax, [rsi + HIBER_SPECIAL_CR8]
    mov cr8, rax

    mov rax, [rsi + HIBER_SPECIAL_DR0]
    mov dr0, rax
    mov rax, [rsi + HIBER_SPECIAL_DR1]
    mov dr1, rax
    mov rax, [rsi + HIBER_SPECIAL_DR2]
    mov dr2, rax
    mov rax, [rsi + HIBER_SPECIAL_DR3]
    mov dr3, rax
    mov rax, [rsi + HIBER_SPECIAL_DR6]
    mov dr6, rax

    mov ecx, HEX(C0000101)
    mov rax, [rsi + HIBER_SPECIAL_GSBASE]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    mov ecx, HEX(C0000102)
    mov rax, [rsi + HIBER_SPECIAL_KERNELGSBASE]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    mov ecx, HEX(C0000081)
    mov rax, [rsi + HIBER_SPECIAL_STAR]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    mov ecx, HEX(C0000082)
    mov rax, [rsi + HIBER_SPECIAL_LSTAR]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    mov ecx, HEX(C0000083)
    mov rax, [rsi + HIBER_SPECIAL_CSTAR]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    mov ecx, HEX(C0000084)
    mov rax, [rsi + HIBER_SPECIAL_SFMASK]
    mov rdx, rax
    shr rdx, 32
    wrmsr

    /* Restore the saved floating-point state while CR0.TS remains clear. */
    test r14d, HIBER_TRAMPOLINE_HAS_FX
    jz HiberRestoreSegments
    fxrstor64 [rsi + HIBER_FX_STATE_OFFSET]

HiberRestoreSegments:
    mov rax, [rsi + HIBER_SPECIAL_CR0]
    mov cr0, rax

    mov ax, word ptr [rsi + HIBER_CONTEXT_DS]
    mov ds, ax
    mov ax, word ptr [rsi + HIBER_CONTEXT_ES]
    mov es, ax
    mov ax, word ptr [rsi + HIBER_CONTEXT_FS]
    mov fs, ax
    mov ax, word ptr [rsi + HIBER_CONTEXT_GS]
    mov gs, ax
    mov ax, word ptr [rsi + HIBER_CONTEXT_SS]
    mov ss, ax

    /* Build an IRETQ frame at the saved stack pointer. */
    mov rax, [rsi + HIBER_CONTEXT_RSP]
    sub rax, 24
    mov rcx, [rsi + HIBER_CONTEXT_RIP]
    mov [rax], rcx
    movzx ecx, word ptr [rsi + HIBER_CONTEXT_CS]
    mov [rax + 8], rcx
    mov ecx, [rsi + HIBER_CONTEXT_EFLAGS]
    mov [rax + 16], rcx
    mov rsp, rax

    mov rax, [rsi + HIBER_SPECIAL_DR7]
    mov dr7, rax
    mov rbx, [rsi + HIBER_CONTEXT_RBX]
    mov rbp, [rsi + HIBER_CONTEXT_RBP]
    mov rdi, [rsi + HIBER_CONTEXT_RDI]
    mov r8, [rsi + HIBER_CONTEXT_R8]
    mov r9, [rsi + HIBER_CONTEXT_R9]
    mov r10, [rsi + HIBER_CONTEXT_R10]
    mov r11, [rsi + HIBER_CONTEXT_R11]
    mov r12, [rsi + HIBER_CONTEXT_R12]
    mov r13, [rsi + HIBER_CONTEXT_R13]
    mov r14, [rsi + HIBER_CONTEXT_R14]
    mov r15, [rsi + HIBER_CONTEXT_R15]
    mov rdx, [rsi + HIBER_CONTEXT_RDX]
    mov rcx, [rsi + HIBER_CONTEXT_RCX]
    mov rax, [rsi + HIBER_CONTEXT_RAX]
    mov rsi, [rsi + HIBER_CONTEXT_RSI]
    iretq
END

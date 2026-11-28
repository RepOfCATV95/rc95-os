/*
 * PROJECT:     RC95LDR
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Restore the saved i386 processor context and resume the kernel.
 */

#include <asm.inc>
#include <ks386.inc>

#define HIBER_STATE_SPECIAL       HEX(2CC)
#define HIBER_SPECIAL_CR0         HEX(00)
#define HIBER_SPECIAL_CR2         HEX(04)
#define HIBER_SPECIAL_CR3         HEX(08)
#define HIBER_SPECIAL_CR4         HEX(0C)
#define HIBER_SPECIAL_DR0         HEX(10)
#define HIBER_SPECIAL_DR1         HEX(14)
#define HIBER_SPECIAL_DR2         HEX(18)
#define HIBER_SPECIAL_DR3         HEX(1C)
#define HIBER_SPECIAL_DR6         HEX(20)
#define HIBER_SPECIAL_DR7         HEX(24)
#define HIBER_SPECIAL_GDTR        HEX(28)
#define HIBER_SPECIAL_IDTR        HEX(30)
#define HIBER_SPECIAL_TR          HEX(38)
#define HIBER_SPECIAL_LDTR        HEX(3A)
#define HIBER_CONTEXT_FLOAT_SAVE  HEX(1C)
#define HIBER_FX_STATE_OFFSET     HEX(320)
#define CR4_PGE                   HEX(80)
#define CR0_NPX_MASK              HEX(0E)
#define HIBER_TRAMPOLINE_HAS_FPU  HEX(01)
#define HIBER_TRAMPOLINE_HAS_FX   HEX(02)

.code32
#ifdef _USE_ML
.686P
.XMM
#endif

PUBLIC _Rc95LdrI386ResumeTrampoline@8
_Rc95LdrI386ResumeTrampoline@8:
    mov esi, [esp + 4]
    mov edx, [esp + 8]
    cli

    /* Drop global TLB entries before switching to the saved page tables. */
    mov eax, cr4
    and eax, NOT CR4_PGE
    mov cr4, eax

    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_CR4]
    mov cr4, eax
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_CR3]
    mov cr3, eax
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_CR0]
    mov cr0, eax

    /* The saved descriptor tables are mapped by the restored CR3. */
#ifdef _USE_ML
    lgdt fword ptr [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_GDTR + 2]
    lidt fword ptr [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_IDTR + 2]
#else
    lgdt [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_GDTR + 2]
    lidt [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_IDTR + 2]
#endif

    /* Clear the busy bit before reloading the saved task register. */
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_GDTR + 4]
    movzx ecx, word ptr [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_TR]
    and ecx, HEX(FFF8)
    add eax, ecx
    and byte ptr [eax + 5], HEX(FD)

    mov ax, word ptr [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_LDTR]
    lldt ax
    mov ax, word ptr [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_TR]
    ltr ax

    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_CR2]
    mov cr2, eax
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_DR0]
    mov dr0, eax
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_DR1]
    mov dr1, eax
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_DR2]
    mov dr2, eax
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_DR3]
    mov dr3, eax
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_DR6]
    mov dr6, eax

    mov eax, cr0
    and eax, NOT CR0_NPX_MASK
    mov cr0, eax

    test edx, HIBER_TRAMPOLINE_HAS_FX
    jnz HiberRestoreFx
    test edx, HIBER_TRAMPOLINE_HAS_FPU
    jz HiberRestoreSegments

HiberRestoreX87:
    frstor [esi + HIBER_CONTEXT_FLOAT_SAVE]
    jmp HiberRestoreSegments

HiberRestoreFx:
    fxrstor [esi + HIBER_FX_STATE_OFFSET]

HiberRestoreSegments:
    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_CR0]
    mov cr0, eax

    mov ax, word ptr [esi + CsSegDs]
    mov ds, ax
    mov ax, word ptr [esi + CsSegEs]
    mov es, ax
    mov ax, word ptr [esi + CsSegFs]
    mov fs, ax
    mov ax, word ptr [esi + CsSegGs]
    mov gs, ax
    mov ax, word ptr [esi + CsSegSs]
    mov ss, ax

    /* Build an IRET frame at the saved stack pointer. */
    mov eax, [esi + CsEsp]
    sub eax, 12
    mov ecx, [esi + CsEip]
    mov [eax], ecx
    movzx ecx, word ptr [esi + CsSegCs]
    mov [eax + 4], ecx
    mov ecx, [esi + CsEflags]
    mov [eax + 8], ecx
    mov esp, eax

    mov eax, [esi + HIBER_STATE_SPECIAL + HIBER_SPECIAL_DR7]
    mov dr7, eax
    mov edi, [esi + CsEdi]
    mov ebx, [esi + CsEbx]
    mov ebp, [esi + CsEbp]
    mov edx, [esi + CsEdx]
    mov ecx, [esi + CsEcx]
    mov eax, [esi + CsEax]
    mov esi, [esi + CsEsi]
    iretd
END

/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     i386 wake stub for ACPI sleep states (S2/S3)
 *
 * After S2/S3 the firmware jumps to the waking vector in real mode, with the
 * CPU reset and paging off. The 16-bit part below is copied to the low stub
 * page (below 1MB), whose address is stored in the FACS. It enters protected
 * mode with paging on, using temporary page tables that map the stub
 * identically and the rest like the kernel does, and jumps to
 * HalpWakeEntry32 in the kernel. That restores the registers saved by
 * HalpWakeSaveContext and returns from it a second time.
 *
 * The 16-bit code is written as bytes so that it does not depend on the
 * assembler's support for 16-bit code. The data it uses follows it. The
 * layout of the state (HALP_WAKE_STATE) is checked in lowstub.c.
 */

#include <asm.inc>

/* Offsets in HALP_WAKE_STATE */
#define WS_RESUMED  HEX(00)
#define WS_CR0      HEX(04)
#define WS_CR3      HEX(08)
#define WS_CR4      HEX(0C)
#define WS_DR0      HEX(10)
#define WS_DR1      HEX(14)
#define WS_DR2      HEX(18)
#define WS_DR3      HEX(1C)
#define WS_DR6      HEX(20)
#define WS_DR7      HEX(24)
#define WS_TR       HEX(28)
#define WS_LDTR     HEX(2A)
#define WS_GDTBASE  HEX(2C)
#define WS_SEGDS    HEX(30)
#define WS_SEGES    HEX(34)
#define WS_SEGFS    HEX(38)
#define WS_SEGGS    HEX(3C)
#define WS_SEGSS    HEX(40)
#define WS_EBX      HEX(44)
#define WS_EBP      HEX(48)
#define WS_EDI      HEX(4C)
#define WS_ESI      HEX(50)
#define WS_ESP      HEX(54)
#define WS_EIP      HEX(58)
#define WS_EFLAGS   HEX(5C)
#define WS_FX       HEX(70)

#define ZERO_OFFSET(f) (f - _HalpWakeEntry16)

PUBLIC _HalpWakeEntry16
PUBLIC _HalpWakeData
PUBLIC _HalpWakeEntry16End
PUBLIC _HalpWakeEntry32
PUBLIC _HalpWakeSaveContext

EXTERN _HalpWakeSaveControlState:PROC
EXTERN _HalpWakeState:DWORD

.code

/*
 * BOOLEAN HalpWakeSaveContext(VOID)
 *
 * Like setjmp: returns FALSE after saving the processor state, and returns
 * TRUE to the same caller when the machine woke up and the stub restored the
 * state. The registers and the stack pointer that are saved are the ones of
 * the caller, so this works as long as the caller's frame is alive.
 */
_HalpWakeSaveContext:
    push ebx
    push esi
    push edi
    push ebp
    call _HalpWakeSaveControlState      /* control registers, FPU: C code */
    pop ebp
    pop edi
    pop esi
    pop ebx

    mov eax, offset _HalpWakeState
    mov [eax + WS_EBX], ebx
    mov [eax + WS_ESI], esi
    mov [eax + WS_EDI], edi
    mov [eax + WS_EBP], ebp
    lea ecx, [esp + 4]                  /* esp after the return */
    mov [eax + WS_ESP], ecx
    mov ecx, [esp]                      /* the return address */
    mov [eax + WS_EIP], ecx
    pushfd
    pop ecx
    and ecx, HEX(0FFFFFDFF)             /* interrupts stay off after the wake up */
    mov [eax + WS_EFLAGS], ecx
    mov ecx, ds
    mov [eax + WS_SEGDS], ecx
    mov ecx, es
    mov [eax + WS_SEGES], ecx
    mov ecx, fs
    mov [eax + WS_SEGFS], ecx
    mov ecx, gs
    mov [eax + WS_SEGGS], ecx
    mov ecx, ss
    mov [eax + WS_SEGSS], ecx
    mov dword ptr [eax + WS_RESUMED], 0
    xor eax, eax
    ret

/* 16-bit part: runs with CS = stub page / 16, IP = 0 *************************/

_HalpWakeEntry16:
    .byte HEX(FA)                                   /* cli */
    .byte HEX(FC)                                   /* cld */
    .byte HEX(8C), HEX(C8)                          /* mov ax, cs */
    .byte HEX(8E), HEX(D8)                          /* mov ds, ax */

    .byte HEX(66), HEX(0F), HEX(01), HEX(16)        /* lgdt fword ptr [Gdtr] */
    .short ZERO_OFFSET(WakeGdtr)
    .byte HEX(66), HEX(0F), HEX(01), HEX(1E)        /* lidt fword ptr [Idtr] */
    .short ZERO_OFFSET(WakeIdtr)

    /*
     * The CPU was reset, so CR4 is 0: the page tables of the kernel can have
     * 4MB pages (PSE). The saved CR4 is restored in the 32-bit part.
     */
    .byte HEX(0F), HEX(20), HEX(E0)                 /* mov eax, cr4 */
    .byte HEX(66), HEX(0D)                          /* or eax, CR4_PSE */
    .long HEX(10)
    .byte HEX(0F), HEX(22), HEX(E0)                 /* mov cr4, eax */

    .byte HEX(66), HEX(A1)                          /* mov eax, [PageTableRoot] */
    .short ZERO_OFFSET(WakePageTableRoot)
    .byte HEX(0F), HEX(22), HEX(D8)                 /* mov cr3, eax */

    .byte HEX(0F), HEX(20), HEX(C0)                 /* mov eax, cr0 */
    .byte HEX(66), HEX(0D)                          /* or eax, CR0_PG | CR0_PE */
    .long HEX(80000001)
    .byte HEX(0F), HEX(22), HEX(C0)                 /* mov cr0, eax */

    .byte HEX(66), HEX(8B), HEX(36)                 /* mov esi, [StatePtr] */
    .short ZERO_OFFSET(WakeStatePtr)

    /* Jump to the kernel, 32-bit offset, with the kernel's code selector */
    .byte HEX(66), HEX(FF), HEX(2E)                 /* jmp fword ptr [Jump32] */
    .short ZERO_OFFSET(WakeJump32)

.align 4

/* Data, filled in by HalpWakeBuildStub (HALP_WAKE_STUB_DATA) */
_HalpWakeData:
WakeJump32:
    .long 0                 /* Jump32Offset */
    .short 8                /* Jump32Selector (KGDT_R0_CODE) */
    .short 0
WakePageTableRoot:
    .long 0
WakeStatePtr:
    .long 0
WakeGdtrPad:
    .short 0
WakeGdtr:
    .short 0                /* Limit */
    .long 0                 /* Base */
WakeIdtrPad:
    .short 0
WakeIdtr:
    .short 0                /* Limit */
    .long 0                 /* Base */
_HalpWakeEntry16End:

/* 32-bit part: runs in the kernel, with the temporary page tables ***********/

.code32
_HalpWakeEntry32:
    /* esi = HALP_WAKE_STATE. Ring 0 data selector (KGDT_R0_DATA) */
    mov ax, HEX(10)
    mov ds, ax
    mov es, ax
    mov ss, ax

    /* The paging of the machine that went to sleep */
    mov eax, [esi + WS_CR3]
    mov cr3, eax
    mov eax, [esi + WS_CR4]
    mov cr4, eax

    /* FPU/SSE state; CR4.OSFXSR is set again and CR0.TS is clear */
    clts
    .byte HEX(0F), HEX(AE), HEX(8E)                 /* fxrstor [esi + WS_FX] */
    .long WS_FX

    mov eax, [esi + WS_CR0]
    mov cr0, eax

    /* Debug registers */
    mov eax, [esi + WS_DR0]
    mov dr0, eax
    mov eax, [esi + WS_DR1]
    mov dr1, eax
    mov eax, [esi + WS_DR2]
    mov dr2, eax
    mov eax, [esi + WS_DR3]
    mov dr3, eax
    mov eax, [esi + WS_DR6]
    mov dr6, eax
    mov eax, [esi + WS_DR7]
    mov dr7, eax

    /* The TSS is still marked busy in the GDT: clear that before ltr */
    mov edx, [esi + WS_GDTBASE]
    movzx ecx, word ptr [esi + WS_TR]
    and ecx, HEX(0FFF8)
    and byte ptr [edx + ecx + 5], HEX(0FD)
    ltr word ptr [esi + WS_TR]
    lldt word ptr [esi + WS_LDTR]

    /* Segments */
    mov eax, [esi + WS_SEGDS]
    mov ds, eax
    mov eax, [esi + WS_SEGES]
    mov es, eax
    mov eax, [esi + WS_SEGFS]
    mov fs, eax
    mov eax, [esi + WS_SEGGS]
    mov gs, eax
    mov eax, [esi + WS_SEGSS]
    mov ss, eax

    /* HalpWakeSaveContext returns TRUE */
    mov dword ptr [esi + WS_RESUMED], 1

    /* Return to the saved EIP on the saved stack, with the saved flags */
    mov esp, [esi + WS_ESP]
    push dword ptr [esi + WS_EIP]
    push dword ptr [esi + WS_EFLAGS]
    mov ebx, [esi + WS_EBX]
    mov ebp, [esi + WS_EBP]
    mov edi, [esi + WS_EDI]
    mov esi, [esi + WS_ESI]
    mov eax, 1
    popfd
    ret

END

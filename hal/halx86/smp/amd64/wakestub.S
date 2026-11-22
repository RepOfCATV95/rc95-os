/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     amd64 wake stub for ACPI sleep states (S2/S3)
 *
 * After S2/S3 the firmware jumps to the waking vector in real mode, with the
 * CPU reset and paging off. The 16-bit part below is copied to the low stub
 * page (below 1MB), whose address is stored in the FACS. It switches to long
 * mode with temporary page tables that map the stub identically and the rest
 * like the kernel does, using a small GDT of its own, and the 64-bit
 * trampoline jumps to HalpWakeEntry64 in the kernel. That restores the
 * registers saved by HalpWakeSaveContext and returns from it a second time.
 *
 * The 16-bit code and the trampoline are written as bytes: an assembler for
 * 64-bit code can't be asked for 16-bit code. The data they use follows. The
 * layout of the state (HALP_WAKE_STATE) is checked in lowstub.c.
 */

#include <asm.inc>

/* Offsets in HALP_WAKE_STATE */
#define WS_RESUMED  HEX(00)
#define WS_CR0      HEX(08)
#define WS_CR3      HEX(10)
#define WS_CR4      HEX(18)
#define WS_CR8      HEX(20)
#define WS_EFER     HEX(28)
#define WS_GDTRLIM  HEX(36)
#define WS_GDTRBASE HEX(38)
#define WS_IDTRLIM  HEX(46)
#define WS_TR       HEX(50)
#define WS_LDTR     HEX(52)
#define WS_SEGCS    HEX(54)
#define WS_SEGDS    HEX(56)
#define WS_SEGES    HEX(58)
#define WS_SEGFS    HEX(5A)
#define WS_SEGGS    HEX(5C)
#define WS_SEGSS    HEX(5E)
#define WS_DR0      HEX(60)
#define WS_DR1      HEX(68)
#define WS_DR2      HEX(70)
#define WS_DR3      HEX(78)
#define WS_DR6      HEX(80)
#define WS_DR7      HEX(88)
#define WS_GSBASE   HEX(90)
#define WS_GSSWAP   HEX(98)
#define WS_STAR     HEX(0A0)
#define WS_LSTAR    HEX(0A8)
#define WS_CSTAR    HEX(0B0)
#define WS_SFMASK   HEX(0B8)
#define WS_PAT      HEX(0C0)
#define WS_RBX      HEX(0C8)
#define WS_RBP      HEX(0D0)
#define WS_RDI      HEX(0D8)
#define WS_RSI      HEX(0E0)
#define WS_R12      HEX(0E8)
#define WS_R13      HEX(0F0)
#define WS_R14      HEX(0F8)
#define WS_R15      HEX(100)
#define WS_RSP      HEX(108)
#define WS_RIP      HEX(110)
#define WS_RFLAGS   HEX(118)
#define WS_FX       HEX(130)

#define ZERO_OFFSET(f) (f - HalpWakeEntry16)

PUBLIC HalpWakeEntry16
PUBLIC HalpWakeTramp64
PUBLIC HalpWakeData
PUBLIC HalpWakeEntry16End
PUBLIC HalpWakeEntry64
PUBLIC HalpWakeSaveContext

EXTERN HalpWakeSaveControlState:PROC
EXTERN HalpWakeState:BYTE

.code

/*
 * BOOLEAN HalpWakeSaveContext(VOID)
 *
 * Like setjmp: returns FALSE after saving the processor state, and returns
 * TRUE to the same caller when the machine woke up and the stub restored the
 * state. The registers and the stack pointer that are saved are the ones of
 * the caller, so this works as long as the caller's frame is alive.
 */
HalpWakeSaveContext:
    sub rsp, HEX(28)                    /* shadow space, alignment */
    call HalpWakeSaveControlState       /* control registers, MSRs, FPU: C code */
    add rsp, HEX(28)

    lea rax, [rip + HalpWakeState]
    mov [rax + WS_RBX], rbx
    mov [rax + WS_RBP], rbp
    mov [rax + WS_RDI], rdi
    mov [rax + WS_RSI], rsi
    mov [rax + WS_R12], r12
    mov [rax + WS_R13], r13
    mov [rax + WS_R14], r14
    mov [rax + WS_R15], r15
    lea rcx, [rsp + 8]                  /* rsp after the return */
    mov [rax + WS_RSP], rcx
    mov rcx, [rsp]                      /* the return address */
    mov [rax + WS_RIP], rcx
    pushfq
    pop rcx
    and ecx, HEX(0FFFFFDFF)             /* interrupts stay off after the wake up */
    mov [rax + WS_RFLAGS], rcx
    mov cx, cs
    mov [rax + WS_SEGCS], cx
    mov cx, ds
    mov [rax + WS_SEGDS], cx
    mov cx, es
    mov [rax + WS_SEGES], cx
    mov cx, fs
    mov [rax + WS_SEGFS], cx
    mov cx, gs
    mov [rax + WS_SEGGS], cx
    mov cx, ss
    mov [rax + WS_SEGSS], cx
    mov dword ptr [rax + WS_RESUMED], 0
    xor eax, eax
    ret

/* 16-bit part: runs with CS = stub page / 16, IP = 0 *************************/

HalpWakeEntry16:
    .byte HEX(FA)                                   /* cli */
    .byte HEX(FC)                                   /* cld */
    .byte HEX(8C), HEX(C8)                          /* mov ax, cs */
    .byte HEX(8E), HEX(D8)                          /* mov ds, ax */

    .byte HEX(66), HEX(0F), HEX(01), HEX(16)        /* lgdt fword ptr [Gdtr] */
    .short ZERO_OFFSET(WakeGdtr)

    .byte HEX(0F), HEX(20), HEX(E0)                 /* mov eax, cr4 */
    .byte HEX(66), HEX(0D)                          /* or eax, CR4_PAE */
    .long HEX(20)
    .byte HEX(0F), HEX(22), HEX(E0)                 /* mov cr4, eax */

    .byte HEX(66), HEX(A1)                          /* mov eax, [PageTableRoot] */
    .short ZERO_OFFSET(WakePageTableRoot)
    .byte HEX(0F), HEX(22), HEX(D8)                 /* mov cr3, eax */

    .byte HEX(66), HEX(B9)                          /* mov ecx, MSR_EFER */
    .long HEX(0C0000080)
    .byte HEX(0F), HEX(32)                          /* rdmsr */
    .byte HEX(66), HEX(0D)                          /* or eax, EFER_LME | EFER_NXE */
    .long HEX(900)
    .byte HEX(0F), HEX(30)                          /* wrmsr */

    .byte HEX(0F), HEX(20), HEX(C0)                 /* mov eax, cr0 */
    .byte HEX(66), HEX(0D)                          /* or eax, CR0_PG | CR0_PE */
    .long HEX(80000001)
    .byte HEX(0F), HEX(22), HEX(C0)                 /* mov cr0, eax */

    /* Jump to the 64-bit trampoline, with the code selector of the GDT below */
    .byte HEX(66), HEX(FF), HEX(2E)                 /* jmp fword ptr [Jump64] */
    .short ZERO_OFFSET(WakeJump64)

.align 16

/* 64-bit trampoline, runs in the stub page with the temporary page tables */
HalpWakeTramp64:
    .byte HEX(4C), HEX(8B), HEX(05)                 /* mov r8, [rip + StatePtr] */
    .long (WakeStatePtr - (HalpWakeTramp64 + 7))
    .byte HEX(48), HEX(8B), HEX(05)                 /* mov rax, [rip + Entry64Ptr] */
    .long (WakeEntryPtr - (HalpWakeTramp64 + 14))
    .byte HEX(FF), HEX(E0)                          /* jmp rax */

.align 8

/* Data, filled in by HalpWakeBuildStub (HALP_WAKE_STUB_DATA) */
HalpWakeData:
WakeJump64:
    .long 0                 /* Jump64Offset: the trampoline */
    .short 8                /* Jump64Selector: 64-bit code of WakeGdt */
    .short 0
WakePageTableRoot:
    .long 0
WakeGdtrPad:
    .short 0
WakeGdtr:
    .short 0                /* Limit */
    .long 0                 /* Base: WakeGdt */
    .long 0
WakeStatePtr:
    .quad 0
WakeEntryPtr:
    .quad 0
WakeGdt:
    .quad 0                 /* null */
    .quad HEX(00209A0000000000)     /* 64-bit code */
    .quad HEX(0000920000000000)     /* data */
HalpWakeEntry16End:

/* 64-bit part: runs in the kernel, with the temporary page tables ***********/

HalpWakeEntry64:
    /* r8 = HALP_WAKE_STATE. The stack, the registers are garbage until now */
    mov rsp, [r8 + WS_RSP]

    /* GDT and IDT of the kernel */
    .byte HEX(41), HEX(0F), HEX(01), HEX(90)        /* lgdt [r8 + WS_GDTRLIM] */
    .long WS_GDTRLIM
    .byte HEX(41), HEX(0F), HEX(01), HEX(98)        /* lidt [r8 + WS_IDTRLIM] */
    .long WS_IDTRLIM

    /* EFER first: the page tables of the kernel use the NX bit */
    mov eax, [r8 + WS_EFER]
    mov edx, [r8 + WS_EFER + 4]
    mov ecx, HEX(C0000080)
    wrmsr

    mov rax, [r8 + WS_CR4]
    mov cr4, rax
    mov rax, [r8 + WS_CR3]
    mov cr3, rax

    /* FPU/SSE state; CR4.OSFXSR is set again and CR0.TS is clear */
    clts
    .byte HEX(49), HEX(0F), HEX(AE), HEX(88)        /* fxrstor64 [r8 + WS_FX] */
    .long WS_FX

    mov rax, [r8 + WS_CR0]
    mov cr0, rax
    mov rax, [r8 + WS_CR8]
    mov cr8, rax

    /* Debug registers */
    mov rax, [r8 + WS_DR0]
    mov dr0, rax
    mov rax, [r8 + WS_DR1]
    mov dr1, rax
    mov rax, [r8 + WS_DR2]
    mov dr2, rax
    mov rax, [r8 + WS_DR3]
    mov dr3, rax
    mov rax, [r8 + WS_DR6]
    mov dr6, rax
    mov rax, [r8 + WS_DR7]
    mov dr7, rax

    /* The TSS is still marked busy in the GDT: clear that before ltr */
    mov rax, [r8 + WS_GDTRBASE]
    movzx ecx, word ptr [r8 + WS_TR]
    and ecx, HEX(0FFF8)
    and byte ptr [rax + rcx + 5], HEX(0FD)
    ltr word ptr [r8 + WS_TR]
    lldt word ptr [r8 + WS_LDTR]

    /* Load the code segment of the kernel: far return to the next instruction */
    .byte HEX(E8), 0, 0, 0, 0                       /* call $ + 5 */
    .byte HEX(58)                                   /* pop rax */
    .byte HEX(48), HEX(83), HEX(0C0), HEX(0E)       /* add rax, 14 */
    .byte HEX(41), HEX(0F), HEX(0B7), HEX(50), HEX(54) /* movzx edx, word ptr [r8 + WS_SEGCS] */
    .byte HEX(52)                                   /* push rdx */
    .byte HEX(50)                                   /* push rax */
    .byte HEX(48), HEX(0CB)                         /* retfq */

    /* Segments. GS before its base is restored. */
    mov ax, word ptr [r8 + WS_SEGDS]
    mov ds, ax
    mov ax, word ptr [r8 + WS_SEGES]
    mov es, ax
    mov ax, word ptr [r8 + WS_SEGFS]
    mov fs, ax
    mov ax, word ptr [r8 + WS_SEGGS]
    mov gs, ax
    mov ax, word ptr [r8 + WS_SEGSS]
    mov ss, ax

    /* MSRs: GS base (the PCR), the swapped GS base, syscall, PAT */
    mov ecx, HEX(0C0000101)
    mov eax, [r8 + WS_GSBASE]
    mov edx, [r8 + WS_GSBASE + 4]
    wrmsr
    mov ecx, HEX(0C0000102)
    mov eax, [r8 + WS_GSSWAP]
    mov edx, [r8 + WS_GSSWAP + 4]
    wrmsr
    mov ecx, HEX(0C0000081)
    mov eax, [r8 + WS_STAR]
    mov edx, [r8 + WS_STAR + 4]
    wrmsr
    mov ecx, HEX(0C0000082)
    mov eax, [r8 + WS_LSTAR]
    mov edx, [r8 + WS_LSTAR + 4]
    wrmsr
    mov ecx, HEX(0C0000083)
    mov eax, [r8 + WS_CSTAR]
    mov edx, [r8 + WS_CSTAR + 4]
    wrmsr
    mov ecx, HEX(0C0000084)
    mov eax, [r8 + WS_SFMASK]
    mov edx, [r8 + WS_SFMASK + 4]
    wrmsr
    mov ecx, HEX(277)
    mov eax, [r8 + WS_PAT]
    mov edx, [r8 + WS_PAT + 4]
    wrmsr

    /* HalpWakeSaveContext returns TRUE */
    mov dword ptr [r8 + WS_RESUMED], 1

    /* Return to the saved RIP on the saved stack, with the saved flags */
    mov rsp, [r8 + WS_RSP]
    push qword ptr [r8 + WS_RIP]
    push qword ptr [r8 + WS_RFLAGS]
    mov rbx, [r8 + WS_RBX]
    mov rbp, [r8 + WS_RBP]
    mov rdi, [r8 + WS_RDI]
    mov rsi, [r8 + WS_RSI]
    mov r12, [r8 + WS_R12]
    mov r13, [r8 + WS_R13]
    mov r14, [r8 + WS_R14]
    mov r15, [r8 + WS_R15]
    mov eax, 1
    popfq
    ret

END

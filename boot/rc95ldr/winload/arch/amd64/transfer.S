/*
 * Native AMD64 cold-boot transfer.
 *
 * RCX = validated RC95LDR_AMD64_REGISTER_FRAME
 * RDX = validated physical CR3 root
 * R8D = nonzero when EFER.NXE must be enabled before loading CR3
 *
 * Preconditions are enforced by Rc95LdrAmd64TransferKernel and the platform:
 * long mode is active; the target GDT/selectors are installed; the frame,
 * target stack, kernel and loader block are mapped in the new address space.
 * Interrupts are disabled before changing CR3. This stub deliberately does
 * not interpret or construct any Windows loader-block ABI.
 */
#include <asm.inc>

#define AMD64_FRAME_RAX       00h
#define AMD64_FRAME_RBX       08h
#define AMD64_FRAME_RCX       10h
#define AMD64_FRAME_RDX       18h
#define AMD64_FRAME_RSI       20h
#define AMD64_FRAME_RDI       28h
#define AMD64_FRAME_RBP       30h
#define AMD64_FRAME_RSP       38h
#define AMD64_FRAME_R8        40h
#define AMD64_FRAME_R9        48h
#define AMD64_FRAME_R10       50h
#define AMD64_FRAME_R11       58h
#define AMD64_FRAME_R12       60h
#define AMD64_FRAME_R13       68h
#define AMD64_FRAME_R14       70h
#define AMD64_FRAME_R15       78h
#define AMD64_FRAME_RIP       80h

.code64

PUBLIC Rc95LdrAmd64TransferValidated
Rc95LdrAmd64TransferValidated PROC
    cli
    cld
    mov r9, rcx
    mov r10, rdx
    test r8d, r8d
    jz Amd64NxReady
    mov ecx, 0C0000080h       ; IA32_EFER
    rdmsr
    or eax, 0800h             ; EFER.NXE
    wrmsr
Amd64NxReady:
    mov rax, r9
    mov cr3, r10

    /* Stage the return target on the destination stack. RET consumes it,
       leaving RSP exactly equal to the kernel's supplied initial stack. */
    mov r11, qword ptr [rax + AMD64_FRAME_RIP]
    mov r10, qword ptr [rax + AMD64_FRAME_RSP]
    mov rsp, r10
    push r11

    mov rbx, qword ptr [rax + AMD64_FRAME_RBX]
    mov rbp, qword ptr [rax + AMD64_FRAME_RBP]
    mov rsi, qword ptr [rax + AMD64_FRAME_RSI]
    mov rdi, qword ptr [rax + AMD64_FRAME_RDI]
    mov r8,  qword ptr [rax + AMD64_FRAME_R8]
    mov r9,  qword ptr [rax + AMD64_FRAME_R9]
    mov r10, qword ptr [rax + AMD64_FRAME_R10]
    mov r11, qword ptr [rax + AMD64_FRAME_R11]
    mov r12, qword ptr [rax + AMD64_FRAME_R12]
    mov r13, qword ptr [rax + AMD64_FRAME_R13]
    mov r14, qword ptr [rax + AMD64_FRAME_R14]
    mov r15, qword ptr [rax + AMD64_FRAME_R15]
    mov rdx, qword ptr [rax + AMD64_FRAME_RDX]
    mov rcx, qword ptr [rax + AMD64_FRAME_RCX]
    mov rax, qword ptr [rax + AMD64_FRAME_RAX]
    ret
Rc95LdrAmd64TransferValidated ENDP

PUBLIC Rc95LdrAmd64HaltForever
Rc95LdrAmd64HaltForever PROC
    cli
Amd64HaltLoop:
    hlt
    jmp Amd64HaltLoop
Rc95LdrAmd64HaltForever ENDP
END

#include <asm.inc>

/*
 * RC95LDR private BIOS thunk.
 *
 * The position-independent real-mode image is copied to physical 0x5000.
 * It owns its GDT and switches directly between flat 32-bit protected mode
 * and real mode. The complete transition and IVT dispatch are self-contained.
 *
 * Contract: i386 flat protected mode, paging disabled, identity-mapped low
 * memory, A20 enabled, and physical 0x5000..0x7fff reserved by startup.
 * The register packet is at 0x6000; the real-mode stack is at 0000:7f00.
 */

.code32

PUBLIC _Rc95LdrI386GatewayCheckMode
_Rc95LdrI386GatewayCheckMode:
    mov eax, cr0
    test eax, 1
    jz GatewayModeUnsupported
    test eax, HEX(080000000)
    jnz GatewayModeUnsupported
    pushfd
    pop eax
    test eax, HEX(020000)
    jnz GatewayModeUnsupported
    mov ax, cs
    cmp ax, HEX(008)
    jne GatewayModeUnsupported
    mov ax, ds
    cmp ax, HEX(010)
    jne GatewayModeUnsupported
    mov ax, ss
    cmp ax, HEX(010)
    jne GatewayModeUnsupported
    mov eax, 1
    ret
GatewayModeUnsupported:
    xor eax, eax
    ret

PUBLIC _Rc95LdrI386InvokeGateway
_Rc95LdrI386InvokeGateway:
    pushfd
    push ds
    push es
    push fs
    push gs
    pushad
    mov dword ptr ds:[HEX(0602C)], esp
    sgdt lXdtPrefix ds:[HEX(06048)]
    sidt lXdtPrefix ds:[HEX(06050)]
    cli
    lgdt lXdtPrefix ds:[HEX(05000) + (_Rc95LdrI386GatewayGdtr - _Rc95LdrI386GatewayImageStart)]
    .byte HEX(0EA)
    .long HEX(05000) + (_Rc95LdrI386GatewayPm16 - _Rc95LdrI386GatewayImageStart)
    .word HEX(018)

_TEXT ENDS

/*
 * Everything between ImageStart and ImageEnd is copied verbatim to 0x5000.
 * The image uses physical/segment-relative addresses only.
 */
.code16
PUBLIC _Rc95LdrI386GatewayImageStart
_Rc95LdrI386GatewayImageStart:

_Rc95LdrI386GatewayPm16:
    mov ax, HEX(020)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    /* Mask NMI only across the mode transition. */
    in al, HEX(070)
    mov byte ptr ds:[HEX(06044)], al
    or al, HEX(080)
    out HEX(070), al
    mov eax, cr0
    and al, HEX(0FE)
    mov cr0, eax
    .byte HEX(0EA)
    .word (_Rc95LdrI386GatewayRealMode - _Rc95LdrI386GatewayImageStart)
    .word HEX(0500)

_Rc95LdrI386GatewayRealMode:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor esp, esp
    mov sp, HEX(07F00)
    lidt lXdtPrefix cs:[(_Rc95LdrI386GatewayRealModeIdtr - _Rc95LdrI386GatewayImageStart)]
    mov al, byte ptr cs:[HEX(01044)]
    out HEX(070), al

    /* Resolve the requested vector before loading the caller's registers. */
    mov al, byte ptr ds:[HEX(06028)]
    xor ax, ax
    shl ax, 2
    mov si, ax
    mov ax, word ptr ds:[si]
    mov word ptr cs:[HEX(01040)], ax
    mov ax, word ptr ds:[si + 2]
    mov word ptr cs:[HEX(01042)], ax

    /* Load caller-supplied segment registers. */
    mov ax, word ptr ds:[HEX(06020)]
    mov ds, ax
    mov ax, word ptr cs:[HEX(01022)]
    mov es, ax
    mov ax, word ptr cs:[HEX(01024)]
    mov fs, ax
    mov ax, word ptr cs:[HEX(01026)]
    mov gs, ax

    /* Restore caller flags and general-purpose input registers. */
    push dword ptr cs:[HEX(0101C)]
    popfd
    mov ebx, dword ptr cs:[HEX(01004)]
    mov ecx, dword ptr cs:[HEX(01008)]
    mov edx, dword ptr cs:[HEX(0100C)]
    mov esi, dword ptr cs:[HEX(01010)]
    mov edi, dword ptr cs:[HEX(01014)]
    mov ebp, dword ptr cs:[HEX(01018)]
    /* Push the synthetic INT frame, then transfer through the IVT entry. */
    pushf
    push cs
    .word HEX(068)
    .word (_Rc95LdrI386GatewayInterruptReturn - _Rc95LdrI386GatewayImageStart)
    /* INT semantics: mask IF and TF in the live handler flags, preserving
       the caller's original flags in the synthetic IRET frame above. */
    cli
    pushf
    pop ax
    and ax, HEX(0FEFF)
    push ax
    popf
    mov eax, dword ptr cs:[HEX(01000)]

    /* Transfer to the IVT handler using a far return. */
    push word ptr cs:[HEX(01042)]  /* IVT segment */
    push word ptr cs:[HEX(01040)]  /* IVT offset */
    retf

_Rc95LdrI386GatewayInterruptReturn:
    /* Save all returned registers and flags before switching modes. */
    mov dword ptr cs:[HEX(01000)], eax
    mov dword ptr cs:[HEX(01004)], ebx
    mov dword ptr cs:[HEX(01008)], ecx
    mov dword ptr cs:[HEX(0100C)], edx
    mov dword ptr cs:[HEX(01010)], esi
    mov dword ptr cs:[HEX(01014)], edi
    mov dword ptr cs:[HEX(01018)], ebp
    pushfd
    pop eax
    mov dword ptr cs:[HEX(0101C)], eax
    mov ax, ds
    mov word ptr cs:[HEX(01020)], ax
    mov ax, es
    mov word ptr cs:[HEX(01022)], ax
    mov ax, fs
    mov word ptr cs:[HEX(01024)], ax
    mov ax, gs
    mov word ptr cs:[HEX(01026)], ax

    /* Preserve the BIOS's CMOS index and mask NMI during the return switch. */
    in al, HEX(070)
    mov byte ptr cs:[HEX(01044)], al
    or al, HEX(080)
    out HEX(070), al
    cli
    xor ax, ax
    mov ds, ax
    mov eax, cr0
    or al, 1
    mov cr0, eax
    db HEX(066), HEX(0EA)
    dd OFFSET _Rc95LdrI386GatewayPmReturn
    dw HEX(008)

.text ENDS

.code32
_Rc95LdrI386GatewayPmReturn:
    mov ax, HEX(010)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    lgdt lXdtPrefix ds:[HEX(06048)]
    lidt lXdtPrefix ds:[HEX(06050)]
    mov al, byte ptr ds:[HEX(06044)]
    out HEX(070), al
    mov esp, dword ptr ds:[HEX(0602C)]
    popad
    pop gs
    pop fs
    pop es
    pop ds
    popfd
    ret

_TEXT ENDS

/* Private GDT: flat 32-bit code/data and 16-bit code/data, all base zero. */
.code16
.align 8
_Rc95LdrI386GatewayGdt:
    .quad 0
    .word HEX(0FFFF), HEX(0000), HEX(09A00), HEX(0CF)
    .word HEX(0FFFF), HEX(0000), HEX(09200), HEX(0CF)
    .word HEX(0FFFF), HEX(0000), HEX(09A00), HEX(0000)
    .word HEX(0FFFF), HEX(0000), HEX(09200), HEX(0000)
_Rc95LdrI386GatewayGdtr:
    .word HEX(027)
    .long HEX(05000) + (_Rc95LdrI386GatewayGdt - _Rc95LdrI386GatewayImageStart)
_Rc95LdrI386GatewayRealModeIdtr:
    .word HEX(03FF)
    .long HEX(00000000)

PUBLIC _Rc95LdrI386GatewayImageEnd
_Rc95LdrI386GatewayImageEnd:

.endcode16
END

/* RC95LDR i386 protected/real-mode BIOS gateway, GNU as implementation.
 *
 * This is a relocatable GAS translation of gateway.S. The copied image
 * between ImageStart and ImageEnd is position-independent at physical 5000h.
 * Intel syntax is intentionally used, but all directives and symbol rules
 * are GNU as (not MASM and not asm.inc).
 */
.intel_syntax noprefix
#ifdef _WIN32
#define CNAME(x) _##x
#else
#define CNAME(x) x
#endif

.section .text
.code32
.global CNAME(Rc95LdrI386GatewayCheckMode)
CNAME(Rc95LdrI386GatewayCheckMode):
    mov eax, cr0
    test eax, 1
    jz .Lmode_unsupported
    test eax, 0x08000000
    jnz .Lmode_unsupported
    pushfd
    pop eax
    test eax, 0x00020000
    jnz .Lmode_unsupported
    mov ax, cs
    cmp ax, 0x0008
    jne .Lmode_unsupported
    mov ax, ds
    cmp ax, 0x0010
    jne .Lmode_unsupported
    mov ax, ss
    cmp ax, 0x0010
    jne .Lmode_unsupported
    mov eax, 1
    ret
.Lmode_unsupported:
    xor eax, eax
    ret

.global CNAME(Rc95LdrI386InvokeGateway)
CNAME(Rc95LdrI386InvokeGateway):
    pushfd
    push ds
    push es
    push fs
    push gs
    pushad
    mov DWORD PTR [0x602c], esp
    sgdt FWORD PTR [0x6048]
    sidt FWORD PTR [0x6050]
    cli
    lgdt FWORD PTR [0x5000 + (GatewayGdtr - CNAME(Rc95LdrI386GatewayImageStart))]
    .byte 0xea
    .long 0x5000 + (GatewayPm16 - CNAME(Rc95LdrI386GatewayImageStart))
    .word 0x0018

/* The copied image begins here; all internal offsets are image-relative. */
.code16
.section .text.rc95ldr_gateway,"ax",@progbits
.global CNAME(Rc95LdrI386GatewayImageStart)
CNAME(Rc95LdrI386GatewayImageStart):
GatewayPm16:
    mov ax, 0x0020
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    in al, 0x70
    mov BYTE PTR ds:[0x6044], al
    or al, 0x80
    out 0x70, al
    mov eax, cr0
    and al, 0xfe
    mov cr0, eax
    .byte 0xea
    .word (GatewayRealMode - CNAME(Rc95LdrI386GatewayImageStart))
    .word 0x0500

GatewayRealMode:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor esp, esp
    mov sp, 0x7f00
    lidt FWORD PTR cs:[GatewayRealModeIdtr - CNAME(Rc95LdrI386GatewayImageStart)]
    mov al, BYTE PTR cs:[0x1044]
    out 0x70, al

    /* Resolve the requested IVT vector (packet byte 28h). */
    movzx eax, BYTE PTR ds:[0x6028]
    shl eax, 2
    mov si, ax
    mov ax, WORD PTR ds:[si]
    mov WORD PTR cs:[0x1040], ax
    mov ax, WORD PTR ds:[si + 2]
    mov WORD PTR cs:[0x1042], ax

    /* Load caller-supplied segment registers from the packet. */
    mov ax, WORD PTR ds:[0x6020]
    mov ds, ax
    mov ax, WORD PTR cs:[0x1022]
    mov es, ax
    mov ax, WORD PTR cs:[0x1024]
    mov fs, ax
    mov ax, WORD PTR cs:[0x1026]
    mov gs, ax

    /* Restore the caller's flags and general-purpose input registers. */
    push DWORD PTR cs:[0x101c]
    popfd
    mov ebx, DWORD PTR cs:[0x1004]
    mov ecx, DWORD PTR cs:[0x1008]
    mov edx, DWORD PTR cs:[0x100c]
    mov esi, DWORD PTR cs:[0x1010]
    mov edi, DWORD PTR cs:[0x1014]
    mov ebp, DWORD PTR cs:[0x1018]

    /* INT-compatible frame: handler IRET returns to our continuation. */
    pushf
    push cs
    .byte 0x68
    .word (GatewayInterruptReturn - CNAME(Rc95LdrI386GatewayImageStart))
    cli
    pushf
    pop ax
    and ax, 0xfeff
    push ax
    popf
    mov eax, DWORD PTR cs:[0x1000]
    push WORD PTR cs:[0x1042]
    push WORD PTR cs:[0x1040]
    retf

GatewayInterruptReturn:
    mov DWORD PTR cs:[0x1000], eax
    mov DWORD PTR cs:[0x1004], ebx
    mov DWORD PTR cs:[0x1008], ecx
    mov DWORD PTR cs:[0x100c], edx
    mov DWORD PTR cs:[0x1010], esi
    mov DWORD PTR cs:[0x1014], edi
    mov DWORD PTR cs:[0x1018], ebp
    pushfd
    pop eax
    mov DWORD PTR cs:[0x101c], eax
    mov ax, ds
    mov WORD PTR cs:[0x1020], ax
    mov ax, es
    mov WORD PTR cs:[0x1022], ax
    mov ax, fs
    mov WORD PTR cs:[0x1024], ax
    mov ax, gs
    mov WORD PTR cs:[0x1026], ax

    in al, 0x70
    mov BYTE PTR cs:[0x1044], al
    or al, 0x80
    out 0x70, al
    cli
    xor ax, ax
    mov ds, ax
    mov eax, cr0
    or al, 1
    mov cr0, eax
    .byte 0x66, 0xea
    .long 0x5000 + (GatewayPmReturn - CNAME(Rc95LdrI386GatewayImageStart))
    .word 0x0008

.code32
GatewayPmReturn:
    mov ax, 0x0010
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    lgdt FWORD PTR [0x6048]
    lidt FWORD PTR [0x6050]
    mov al, BYTE PTR ds:[0x6044]
    out 0x70, al
    mov esp, DWORD PTR ds:[0x602c]
    popad
    pop gs
    pop fs
    pop es
    pop ds
    popfd
    ret

/* Private GDT: flat 32-bit and 16-bit code/data descriptors. */
.code16
.balign 8
GatewayGdt:
    .quad 0
    .word 0xffff, 0x0000, 0x9a00, 0x00cf
    .word 0xffff, 0x0000, 0x9200, 0x00cf
    .word 0xffff, 0x0000, 0x9a00, 0x0000
    .word 0xffff, 0x0000, 0x9200, 0x0000
GatewayGdtr:
    .word 0x0027
    .long 0x5000 + (GatewayGdt - CNAME(Rc95LdrI386GatewayImageStart))
GatewayRealModeIdtr:
    .word 0x03ff
    .long 0x00000000

.global CNAME(Rc95LdrI386GatewayImageEnd)
CNAME(Rc95LdrI386GatewayImageEnd):
.section .note.GNU-stack,"",@progbits

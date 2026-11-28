/* RC95LDR i386 real-mode to protected-mode stage, GNU as implementation. */
.intel_syntax noprefix
#ifdef _WIN32
#define CNAME(x) _##x
#else
#define CNAME(x) x
#endif
.extern CNAME(Rc95LdrBiosStageMain)
.global CNAME(Rc95LdrBiosStageRealModeEntry)

.section .text.rc95ldr_stage,"ax",@progbits
.code16
.equ RC95LDR_STAGE_SIGNATURE, 0x3942
.equ RC95LDR_STAGE_VERSION, 1

CNAME(Rc95LdrBiosStageRealModeEntry):
    cli
    cld
    mov BYTE PTR cs:[StageBootDrive], dl

    /* Initialize VGA text mode and hide the cursor. */
    push ax
    push bx
    push ds
    push si
    mov ax, 0x0003
    int 0x10
    mov ax, 0x1112
    xor bx, bx
    int 0x10
    mov ax, 0x0a01
    mov cx, 0x2000
    int 0x10
    pop si
    pop ds
    pop bx
    pop ax

    /* Convert DS:SI to a physical pointer and validate the handoff. */
    movzx eax, si
    mov cx, ds
    movzx ecx, cx
    shl ecx, 4
    add eax, ecx
    cmp eax, 0x000ffff0
    jae .Lstage_bad_protocol
    mov DWORD PTR cs:[StageHandoffPhysical], eax

    cmp WORD PTR ds:[si], RC95LDR_STAGE_SIGNATURE
    jne .Lstage_bad_protocol
    cmp WORD PTR ds:[si + 2], RC95LDR_STAGE_VERSION
    jne .Lstage_bad_protocol
    cmp DWORD PTR ds:[si + 4], 0
    je .Lstage_bad_protocol
    cmp DWORD PTR ds:[si + 8], 0
    je .Lstage_bad_protocol
    cmp DWORD PTR ds:[si + 12], 0x00010000
    jb .Lstage_bad_protocol

    /* Request BIOS A20 enable, then assert the chipset fast gate. */
    push ds
    push si
    mov ax, 0x2401
    int 0x15
    pop si
    pop ds
    in al, 0x92
    or al, 0x02
    and al, 0xfe
    out 0x92, al

    /* Non-destructive alias test at 0000:0500 and FFFF:0510. */
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov ax, 0xffff
    mov es, ax
    mov al, BYTE PTR ds:[0x0500]
    mov ah, BYTE PTR es:[0x0510]
    push ax
    mov BYTE PTR ds:[0x0500], 0x00
    mov BYTE PTR es:[0x0510], 0xff
    cmp BYTE PTR ds:[0x0500], 0x00
    pop ax
    mov BYTE PTR ds:[0x0500], al
    mov BYTE PTR es:[0x0510], ah
    pop es
    pop ds
    jne .Lstage_a20_failure

    lgdt FWORD PTR cs:[StageGdtr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    .byte 0x66, 0xea
    .long StageProtectedEntry
    .word 0x0008

.code32
StageProtectedEntry:
    mov ax, 0x0010
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esi, DWORD PTR ds:[StageHandoffPhysical]
    mov esp, DWORD PTR [esi + 12]
    and esp, 0xfffffff0
    mov ebx, DWORD PTR [esi + 8]
    mov eax, DWORD PTR [esi + 4]
    push eax
    push ebx
    movzx eax, BYTE PTR ds:[StageBootDrive]
    push eax
    call CNAME(Rc95LdrBiosStageMain)
    add esp, 12

.Lstage_returned:
    cli
    hlt
    jmp .Lstage_returned

.code16
.Lstage_bad_protocol:
    mov al, 'P'
    jmp .Lstage_fatal
.Lstage_a20_failure:
    mov al, 'A'
.Lstage_fatal:
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    cli
    hlt
    jmp .Lstage_fatal

.balign 8
StageGdt:
    .quad 0
    /* Flat 32-bit code and data. */
    .word 0xffff, 0x0000, 0x9a00, 0x00cf
    .word 0xffff, 0x0000, 0x9200, 0x00cf
StageGdtEnd:
StageGdtr:
    .word (StageGdtEnd - StageGdt) - 1
    .long StageGdt

.balign 4
StageHandoffPhysical:
    .long 0
StageBootDrive:
    .byte 0
.section .note.GNU-stack,"",@progbits

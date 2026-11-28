/* RC95LDR BIOS Stage 0 EDD loader, GNU as implementation. */
.intel_syntax noprefix
#ifdef _WIN32
#define CNAME(x) _##x
#else
#define CNAME(x) x
#endif
.section .text,"ax",@progbits
.code16
.global CNAME(Rc95LdrBiosStage0)
CNAME(Rc95LdrBiosStage0):
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov BYTE PTR [BootDrive], dl

    /* Require EDD extensions; this image has no CHS fallback. */
    mov bx, 0x55aa
    mov ah, 0x41
    mov dl, BYTE PTR [BootDrive]
    int 0x13
    jc .Lboot_failure
    cmp bx, 0xaa55
    jne .Lboot_failure
    test cl, 1
    jz .Lboot_failure

    /* Read B95S header at LBA 1 to 0000:0600. */
    mov WORD PTR [DiskPacket], 0x0010
    mov WORD PTR [DiskPacket + 2], 1
    mov WORD PTR [DiskPacket + 4], 0x0600
    mov WORD PTR [DiskPacket + 6], 0
    mov DWORD PTR [DiskPacket + 8], 1
    mov DWORD PTR [DiskPacket + 12], 0
    mov si, OFFSET FLAT:DiskPacket
    mov dl, BYTE PTR [BootDrive]
    mov ah, 0x42
    int 0x13
    jc .Lboot_failure

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, 0x0600
    mov eax, DWORD PTR [si]
    cmp eax, 0x53353942
    jne .Lboot_failure
    mov ax, WORD PTR [si + 4]
    cmp ax, 1
    jne .Lboot_failure
    mov ax, WORD PTR [si + 6]
    cmp ax, 32
    jne .Lboot_failure
    mov eax, DWORD PTR [si + 28]
    test eax, eax
    jnz .Lboot_failure
    mov eax, DWORD PTR [si + 24]
    test eax, eax
    jnz .Lboot_failure
    mov eax, DWORD PTR [si + 8]
    cmp eax, 0x8000
    jb .Lboot_failure
    cmp eax, 0xffff
    ja .Lboot_failure
    mov DWORD PTR [LoadAddress], eax
    mov ecx, DWORD PTR [si + 12]
    test ecx, ecx
    jz .Lboot_failure
    cmp ecx, 0xffff
    ja .Lboot_failure
    mov DWORD PTR [ImageLength], ecx
    add eax, ecx
    jc .Lboot_failure
    cmp eax, 0x10000
    ja .Lboot_failure
    mov eax, DWORD PTR [si + 16]
    cmp eax, ecx
    jae .Lboot_failure
    add eax, DWORD PTR [LoadAddress]
    cmp eax, 0xffff
    ja .Lboot_failure
    mov WORD PTR [EntryFarPointer], ax

    /* Transfer Stage 1 using one EDD DAP request, at most 64 sectors. */
    mov eax, DWORD PTR [ImageLength]
    add eax, 511
    shr eax, 9
    test eax, eax
    jz .Lboot_failure
    cmp eax, 64
    ja .Lboot_failure
    mov WORD PTR [DiskPacket + 2], ax
    mov eax, DWORD PTR [LoadAddress]
    mov dx, ax
    and dx, 0x000f
    mov WORD PTR [DiskPacket + 4], dx
    shr eax, 4
    mov WORD PTR [DiskPacket + 6], ax
    mov DWORD PTR [DiskPacket + 8], 2
    mov DWORD PTR [DiskPacket + 12], 0
    mov si, OFFSET FLAT:DiskPacket
    mov dl, BYTE PTR [BootDrive]
    mov ah, 0x42
    int 0x13
    jc .Lboot_failure

    /* CRC-32/IEEE over ImageLength bytes, excluding sector padding. */
    mov eax, 0xffffffff
    mov si, WORD PTR [LoadAddress]
    mov ecx, DWORD PTR [ImageLength]
.Lcrc_byte:
    movzx edx, BYTE PTR [si]
    xor eax, edx
    mov bl, 8
.Lcrc_bit:
    shr eax, 1
    jnc .Lcrc_no_xor
    xor eax, 0xedb88320
.Lcrc_no_xor:
    dec bl
    jnz .Lcrc_bit
    inc si
    dec ecx
    jnz .Lcrc_byte
    not eax
    mov si, 0x0614
    cmp eax, DWORD PTR [si]
    jne .Lboot_failure

    mov dl, BYTE PTR [BootDrive]
    xor ax, ax
    mov ds, ax
    mov es, ax
    /* JMP FAR m16:16. */
    .byte 0xff, 0x2e
    .word EntryFarPointer

.Lboot_failure:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, OFFSET FLAT:FailureText
.Lfailure_print:
    lodsb
    test al, al
    jz .Lfailure_halt
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp .Lfailure_print
.Lfailure_halt:
    cli
    hlt
    jmp .Lfailure_halt

.balign 4
BootDrive:
    .byte 0
LoadAddress:
    .long 0
ImageLength:
    .long 0
EntryFarPointer:
    .word 0, 0
DiskPacket:
    .byte 16, 0
    .word 1
    .word 0x0600, 0
    .long 1, 0
FailureText:
    .asciz "RC95LDR: B95S/EDD error"

.org 510
    .byte 0x55, 0xaa
.section .note.GNU-stack,"",@progbits

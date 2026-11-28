/* RC95LDR El Torito no-emulation Stage 0, GNU as implementation. */
.intel_syntax noprefix
#ifdef _WIN32
#define CNAME(x) _##x
#else
#define CNAME(x) x
#endif
.section .text,"ax",@progbits
.code16
.global CNAME(Rc95LdrBiosCdStage0)
CNAME(Rc95LdrBiosCdStage0):
    cli
    /* Normalize CS=07c0, IP=0000 to CS=0000, IP=7c06. */
    .byte 0xea
    .word 0x7c06
    .word 0x0000
.Lcd_normalized:
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov BYTE PTR [BootDrive], dl

    /* Validate the B95S header loaded at physical 07e00h. */
    cmp DWORD PTR [0x7e00], 0x53353942
    jne .Lcd_failure
    cmp WORD PTR [0x7e04], 1
    jne .Lcd_failure
    cmp WORD PTR [0x7e06], 32
    jne .Lcd_failure
    cmp DWORD PTR [0x7e08], 0x8000
    jne .Lcd_failure
    cmp DWORD PTR [0x7e18], 0
    jne .Lcd_failure
    cmp DWORD PTR [0x7e1c], 0
    jne .Lcd_failure
    mov ecx, DWORD PTR [0x7e0c]
    test ecx, ecx
    jz .Lcd_failure
    cmp ecx, 0x8000
    ja .Lcd_failure
    mov eax, DWORD PTR [0x7e10]
    cmp eax, ecx
    jae .Lcd_failure
    add eax, 0x8000
    cmp eax, 0xffff
    ja .Lcd_failure
    mov WORD PTR [EntryFarPointer], ax

    /* Stage 1 is already resident at physical 8000h. Verify exact length. */
    mov esi, 0x8000
    mov ecx, DWORD PTR [0x7e0c]
    mov eax, 0xffffffff
.Lcd_crc_byte:
    movzx edx, BYTE PTR [esi]
    xor eax, edx
    mov bl, 8
.Lcd_crc_bit:
    shr eax, 1
    jnc .Lcd_crc_no_xor
    xor eax, 0xedb88320
.Lcd_crc_no_xor:
    dec bl
    jnz .Lcd_crc_bit
    inc esi
    dec ecx
    jnz .Lcd_crc_byte
    not eax
    cmp eax, DWORD PTR [0x7e14]
    jne .Lcd_failure

    mov dl, BYTE PTR [BootDrive]
    xor ax, ax
    mov ds, ax
    mov es, ax
    jmp FWORD PTR [EntryFarPointer]

.Lcd_failure:
    xor ax, ax
    mov ds, ax
    mov si, OFFSET FLAT:FailureText
.Lcd_print:
    lodsb
    test al, al
    jz .Lcd_halt
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp .Lcd_print
.Lcd_halt:
    cli
    hlt
    jmp .Lcd_halt

.balign 4
BootDrive:
    .byte 0
EntryFarPointer:
    .word 0, 0
FailureText:
    .asciz "RC95LDR: CD bootstrap error"

.org 510
    .byte 0x55, 0xaa
.section .note.GNU-stack,"",@progbits

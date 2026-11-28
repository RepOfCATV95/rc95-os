#include <asm.inc>

/*
 * RC95LDR BIOS Stage 0 (512-byte boot sector).
 *
 * Input: DL = BIOS boot drive. B95S is read from LBA 1; Stage 1 is
 * loaded from LBA 2 using INT HEX(13) extensions (AH=HEX(42)).
 *
 * B95S layout: signature, version, header size, load address, exact
 * image length, entry offset, CRC-32/IEEE, flags, reserved.
 *
 * This sector deliberately does not parse B95I or call the protected-mode
 * manifest loader. It only loads and verifies the low-memory Stage-1 image.
 */
.code16
PUBLIC _Rc95LdrBiosStage0
_Rc95LdrBiosStage0:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, HEX(07C00)
    sti
    mov byte ptr [BootDrive], dl

    /* Require EDD extensions; there is no CHS fallback for this format. */
    mov bx, HEX(055AA)
    mov ah, HEX(041)
    mov dl, byte ptr [BootDrive]
    int HEX(013)
    jc BootFailure
    cmp bx, HEX(0AA55)
    jne BootFailure
    test cl, 1
    jz BootFailure

    /* Read the single B95S header sector from LBA 1 to 0000:0600. */
    mov ax, 0010h
    mov word ptr [DiskPacket], ax

    mov ax, 0001h
    mov word ptr [DiskPacket + 2], ax

    mov ax, 0600h
    mov word ptr [DiskPacket + 4], ax

    xor ax, ax
    mov word ptr [DiskPacket + 6], ax

    mov dword ptr [DiskPacket + 8], 1
    mov dword ptr [DiskPacket + 12], 0

    mov si, OFFSET DiskPacket
    mov dl, byte ptr [BootDrive]
    mov ah, 42h
    int 13h

    jc BootFailure

    xor ax, ax
    mov ds, ax
    mov es, ax

    /* Validate fixed header fields before using any address or length. */
    mov si, HEX(0600)
    mov eax, dword ptr [si]
    cmp eax, HEX(05353942)
    jne BootFailure
    mov ax, word ptr [si + 4]
    cmp ax, 1
    jne BootFailure
    mov ax, word ptr [si + 6]
    cmp ax, 32
    jne BootFailure
    mov eax, dword ptr [si + 28]
    test eax, eax
    jnz BootFailure
    mov eax, dword ptr [si + 24]
    test eax, eax
    jnz BootFailure
    mov eax, dword ptr [si + 8]
    cmp eax, HEX(08000)
    jb BootFailure
    cmp eax, HEX(0FFFF)
    ja BootFailure
    mov dword ptr [LoadAddress], eax
    mov ecx, dword ptr [si + 12]
    test ecx, ecx
    jz BootFailure
    cmp ecx, HEX(0FFFF)
    ja BootFailure
    mov dword ptr [ImageLength], ecx
    add eax, ecx
    jc BootFailure
    cmp eax, HEX(10000)
    ja BootFailure
    mov eax, dword ptr [si + 16]
    cmp eax, ecx
    jae BootFailure
    add eax, dword ptr [LoadAddress]
    cmp eax, HEX(0FFFF)
    ja BootFailure
    mov word ptr [EntryAddress], ax
    mov word ptr [EntryFarPointer], ax

    /* EDD DAP supports the complete Stage-1 extent in one transfer.
       The packager caps Stage 1 at the 32-KiB CS=0 window. */
    mov eax, dword ptr [ImageLength]
    add eax, 511
    shr eax, 9
    test eax, eax
    jz BootFailure
    cmp eax, 64
    ja BootFailure
    mov word ptr [DiskPacket + 2], ax
    mov eax, dword ptr [LoadAddress]
    mov dx, ax
    and dx, HEX(000F)
    mov word ptr [DiskPacket + 4], dx
    shr eax, 4
    mov word ptr [DiskPacket + 6], ax
    mov eax, 2
    mov dword ptr [DiskPacket + 8], eax
    xor eax, eax
    mov dword ptr [DiskPacket + 12], eax
    mov si, offset DiskPacket
    mov dl, byte ptr [BootDrive]
    mov ah, HEX(042)
    int HEX(013)
    jc BootFailure
    xor ax, ax
    mov ds, ax
    mov es, ax

    /* CRC-32/IEEE over ImageLength bytes (exclude sector padding). */
    mov eax, HEX(0FFFFFFFF)
    mov si, word ptr [LoadAddress]
    mov ecx, dword ptr [ImageLength]
CrcByte:
    movzx edx, byte ptr [si]
    xor eax, edx
    mov bl, 8
CrcBit:
    shr eax, 1
    jnc CrcNoXor
    xor eax, HEX(0EDB88320)
CrcNoXor:
    dec bl
    jnz CrcBit
    inc si
    dec ecx
    jnz CrcByte
    not eax
    mov si, HEX(0614)
    mov edx, dword ptr [si]
    cmp eax, edx
    jne BootFailure

    mov dl, byte ptr [BootDrive]
    xor ax, ax
    mov ds, ax
    mov es, ax
    /* FF /5 with a 16-bit absolute memory operand: JMP FAR m16:16. */
    db HEX(0FF), HEX(02E)
    dw offset EntryFarPointer

BootFailure:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, offset FailureText
FailurePrint:
    lodsb
    test al, al
    jz FailureHalt
    mov ah, HEX(0E)
    xor bx, bx
    int HEX(010)
    jmp FailurePrint
FailureHalt:
    cli
    hlt
    jmp FailureHalt

.align 4
BootDrive:
    .byte 0
LoadAddress:
    .long 0
ImageLength:
    .long 0
EntryAddress:
    .word 0
EntryFarPointer:
    .word 0, 0
DiskPacket:
    .byte 16, 0
    .word 1
    .word HEX(0600), 0
    .long 1, 0
FailureText:
    .asciz "RC95LDR: B95S/EDD error"

.org 510
    .byte HEX(055), HEX(0AA)
.endcode16
END

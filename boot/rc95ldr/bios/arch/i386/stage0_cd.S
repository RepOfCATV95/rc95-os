#include <asm.inc>

/*
 * RC95LDR El Torito no-emulation boot image Stage 0.
 *
 * The BIOS loads the complete boot image at 07C0:0000. The image layout is:
 *   physical HEX(07C00)  512-byte CD boot sector
 *   physical HEX(07E00)  B95S Stage-1 header (512-byte padded slot)
 *   physical HEX(08000)  Stage-1 image, already resident from the El Torito load
 *
 * mkrc95biosimage.py emits this image and the ISO builder must set the El
 * Torito load segment to HEX(07C0) and load-sector count to ceil(image/512).
 * No disk-relative LBA assumptions or FreeLoader services are used here.
 */
.code16
PUBLIC _Rc95LdrBiosCdStage0
_Rc95LdrBiosCdStage0:
    cli
    /* Normalize El Torito's default CS=07C0, IP=0000 to CS=0000, IP=7C06. */
    .byte HEX(0EA)
    .word HEX(07C06)
    .word HEX(0000)

CdStage0Normalized:
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, HEX(07C00)
    sti
    mov byte ptr [BootDrive], dl

    /* Validate the B95S header which was loaded with the boot image. */
    cmp dword ptr [HEX(07E00)], HEX(05353942)
    jne CdBootFailure
    cmp word ptr [HEX(07E04)], 1
    jne CdBootFailure
    cmp word ptr [HEX(07E06)], 32
    jne CdBootFailure
    cmp dword ptr [HEX(07E08)], HEX(08000)
    jne CdBootFailure
    cmp dword ptr [HEX(07E18)], 0
    jne CdBootFailure
    cmp dword ptr [HEX(07E1C)], 0
    jne CdBootFailure

    mov ecx, dword ptr [HEX(07E0C)]
    test ecx, ecx
    jz CdBootFailure
    cmp ecx, HEX(08000)
    ja CdBootFailure

    mov eax, dword ptr [HEX(07E10)]
    cmp eax, ecx
    jae CdBootFailure
    add eax, HEX(08000)
    cmp eax, HEX(0FFFF)
    ja CdBootFailure
    mov word ptr [EntryFarPointer], ax

    /* Stage 1 occupies physical HEX(08000) and is already loaded by El Torito. */
    mov esi, HEX(08000)
    mov ecx, dword ptr [HEX(07E0C)]
    mov eax, -1
CdCrcByte:
    movzx edx, byte ptr [esi]
    xor eax, edx
    mov bl, 8
CdCrcBit:
    shr eax, 1
    jnc CdCrcNoXor
    xor eax, HEX(0EDB88320)
CdCrcNoXor:
    dec bl
    jnz CdCrcBit
    inc esi
    dec ecx
    jnz CdCrcByte
    not eax
    cmp eax, dword ptr [HEX(07E14)]
    jne CdBootFailure

    mov dl, byte ptr [BootDrive]
    xor ax, ax
    mov ds, ax
    mov es, ax
    jmp fword ptr [EntryFarPointer]

CdBootFailure:
    xor ax, ax
    mov ds, ax
    mov si, offset FailureText
CdPrint:
    lodsb
    test al, al
    jz CdHalt
    mov ah, HEX(0E)
    xor bx, bx
    int HEX(010)
    jmp CdPrint
CdHalt:
    cli
    hlt
    jmp CdHalt

.align 4
BootDrive:
    .byte 0
EntryFarPointer:
    .word 0, 0
FailureText:
    .asciz "RC95LDR: CD bootstrap error"

.org 510
    .byte HEX(055), HEX(0AA)
.endcode16
END

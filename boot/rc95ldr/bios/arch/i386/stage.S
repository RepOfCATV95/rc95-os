#include <asm.inc>

EXTERN _Rc95LdrBiosStageMain:PROC

/*
 * RC95LDR BIOS real-mode stage transition.
 *
 * Entry contract (16-bit real mode):
 *   CS = 0 and IP = the stage's linked physical entry address below 1 MiB.
 *   The stage image is loaded at its link-time physical address.
 *   DS:SI -> RC95LDR_BIOS_STAGE_HANDOFF (physical pointer below 1 MiB)
 *   DL    = BIOS boot drive
 *
 * Handoff layout (packed):
 *   +0  WORD signature 0x3942
 *   +2  WORD version 1
 *   +4  DWORD RC95LDR_BIOS_STARTUP linear address
 *   +8  DWORD RC95LDR_BIOS_PLATFORM_PROVIDER linear address
 *   +12 DWORD private protected-mode stack top (linear)
 *
 * The stage descriptor and RC95LDR image must already have been read into
 * identity-mapped RAM by the preceding BIOS disk stage. This routine enables
 * A20, installs a private flat GDT, enters 32-bit protected mode, and calls
 * the supplied entry using RC95LDR's EAX/EBX register protocol.
 *
 * This is deliberately a transition stage, not a fabricated disk loader:
 * the on-disk extent/manifest is owned by the image packager and is not yet
 * standardized in this repository.
 */

#define RC95LDR_STAGE_SIGNATURE HEX(03942)
#define RC95LDR_STAGE_VERSION   1

.code16
PUBLIC _Rc95LdrBiosStageRealModeEntry
_Rc95LdrBiosStageRealModeEntry:
    cli
    cld
    mov [cs:Rc95LdrStageBootDrive], dl

    /* Take ownership of the VGA text console immediately: 80x25 mode 3,
       then BIOS 8x8 font (80x50). No FreeLoader video code is called. */
    push ax
    push bx
    push ds
    push si
    mov ax, HEX(0003)
    int HEX(010)
    mov ax, HEX(1112)
    xor bx, bx
    int HEX(010)
    xor ax, ax
    mov ax, 00A01h
    mov cx, 02000h              /* Hide cursor until RC95LDR draws its UI. */
    int 0010h
    pop si
    pop ds
    pop bx
    pop ax

    /* DS:SI is a real-mode pointer. Preserve the handoff physical address. */
    movzx eax, si
    mov cx, ds
    movzx ecx, cx
    shl ecx, 4
    add eax, ecx
    cmp eax, HEX(0FFFF0)       /* 16-byte handoff must remain below 1 MiB. */
    jae Rc95LdrStageBadProtocol
    mov [cs:Rc95LdrStageHandoffPhysical], eax

    /* Validate the compact handoff before using its addresses. */
    cmp word ptr ds:[si], RC95LDR_STAGE_SIGNATURE
    jne Rc95LdrStageBadProtocol
    cmp word ptr ds:[si + 2], RC95LDR_STAGE_VERSION
    jne Rc95LdrStageBadProtocol
    cmp dword ptr ds:[si + 4], 0
    je Rc95LdrStageBadProtocol
    cmp dword ptr ds:[si + 8], 0
    je Rc95LdrStageBadProtocol
    cmp dword ptr ds:[si + 12], HEX(010000)
    jb Rc95LdrStageBadProtocol

    /* Ask BIOS first, then use the chipset fast gate as a fallback. */
    push ds
    push si
    mov ax, HEX(02401)
    int HEX(015)
    pop si
    pop ds

    /* Fast A20 gate. BIOS/board firmware must leave port 0x92 implemented. */
    in al, HEX(092)
    or al, HEX(002)
    and al, HEX(0FE)
    out HEX(092), al

    /* Verify A20 with non-destructive alias test at 0000:0500/FFFF:0510. */
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov ax, HEX(0FFFF)
    mov es, ax
    mov al, byte ptr ds:[HEX(0500)]
    mov ah, byte ptr es:[HEX(0510)]
    push ax
    mov byte ptr ds:[HEX(0500)], HEX(00)
    mov byte ptr es:[HEX(0510)], HEX(0FF)
    cmp byte ptr ds:[HEX(0500)], HEX(00)
    pop ax
    mov byte ptr ds:[HEX(0500)], al
    mov byte ptr es:[HEX(0510)], ah
    pop es
    pop ds
    jne Rc95LdrStageA20Failure

    /* Load the private flat GDT and enter protected mode. */
    lgdt lXdtPrefix cs:[Rc95LdrStageGdtr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    .byte HEX(066), HEX(0EA)
#ifdef _USE_ML
    .long OFFSET Rc95LdrStageProtectedEntry
#else
    .long Rc95LdrStageProtectedEntry
#endif
    .word HEX(008)
.endcode16

.code32
Rc95LdrStageProtectedEntry:
    mov ax, HEX(010)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esi, dword ptr ds:[Rc95LdrStageHandoffPhysical]
    mov esp, dword ptr [esi + 12]
    and esp, HEX(0FFFFFFF0)
    mov ebx, dword ptr [esi + 8]
    mov eax, dword ptr [esi + 4]
    push eax                  /* provider (third cdecl argument) */
    push ebx                  /* startup descriptor (second argument) */
    movzx eax, byte ptr ds:[Rc95LdrStageBootDrive]
    push eax                  /* boot drive (first argument) */
    call _Rc95LdrBiosStageMain
    add esp, 12

Rc95LdrStageReturned:
    cli
    hlt
    jmp Rc95LdrStageReturned

#ifdef _USE_ML
_TEXT ENDS
#endif
.code16
Rc95LdrStageBadProtocol:
    mov al, 'P'
    jmp Rc95LdrStageFatal
Rc95LdrStageA20Failure:
    mov al, 'A'
Rc95LdrStageFatal:
    mov ah, HEX(0E)
    xor bx, bx
    int HEX(010)
    cli
    hlt
    jmp Rc95LdrStageFatal

ALIGN 8
Rc95LdrStageGdt LABEL BYTE
    DQ 0
    DW 0FFFFh
    DW 00000h
    DB 09Ah
    DB 0CFh
    DB 00h

    DW 0FFFFh
    DW 00000h
    DB 092h
    DB 0CFh
    DB 00h

Rc95LdrStageGdtEnd LABEL BYTE

Rc95LdrStageGdtr:
    DW (Rc95LdrStageGdtEnd - Rc95LdrStageGdt) - 1
    DD OFFSET Rc95LdrStageGdt

/* Keep mutable real-mode stage state in the same CS-relative image. */
ALIGN 4
Rc95LdrStageHandoffPhysical DWORD 0
Rc95LdrStageBootDrive BYTE 0

.endcode16
END

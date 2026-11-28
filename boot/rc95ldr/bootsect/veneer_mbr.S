/*
 * PROJECT:    RC95-OS RC95LDR BIOS boot sector
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    VENEER MBR: load VENEER.EXE, run it, then start the active VBR
 * COPYRIGHT:  Copyright 1997-2028 RC95-OS contributors
 *
 * An MBR cannot parse a filesystem, so VENEER.EXE is stored as a raw run of
 * sectors in the post-MBR gap. The installer patches the 8-byte block at 0x1B0:
 *     +0 dword  VeneerLba      first sector of the image (default 1)
 *     +4 word   VeneerSectors  image length in sectors (1..1024, 0 = absent)
 *     +6 word   reserved
 *
 * VENEER.EXE image contract (flat, 16-bit real mode, defined here because the
 * on-disk VENEER.EXE format is not yet specified elsewhere in the tree):
 *     +0 dword  signature 'VNR1' (0x31524E56)
 *     +4 word   entry offset within the image (< image size)
 *   Loaded at 1000:0000. Entered by FAR CALL with DL = boot drive, DS = ES = 0.
 *   It must preserve 0000:0600..0000:07FF (the relocated MBR) and the partition
 *   table, and return with RETF, CF clear on success. CF set = fatal.
 *
 * After it returns, this MBR finds the active partition and starts its VBR with
 * DL = boot drive, DS:SI -> partition entry, as regular_mbr.S does.
 * Errors print "E<code>" and halt: A no active partition, B bad boot flag,
 * C disk read error, D VBR lacks 55AA, E no INT13 ext, F VENEER absent,
 * G VENEER header invalid, H VENEER returned failure.
 * Link at 0x0600, flat binary, 512 bytes. EDD is mandatory here (no CHS path).
 */
.intel_syntax noprefix
.section .text,"ax",@progbits
.code16

.set RELOC_ADDR, 0x0600
.set LOAD_ADDR,  0x7c00
.set VENEER_SEG, 0x1000

.global Rc95LdrVeneerMbr
Rc95LdrVeneerMbr:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, LOAD_ADDR
    sti
    cld
    mov si, LOAD_ADDR
    mov di, RELOC_ADDR
    mov cx, 256
    rep movsw
    .byte 0xea                      /* jmp far 0000:Relocated */
    .word Relocated
    .word 0

Relocated:
    mov BYTE PTR [BootDrive], dl

    /* EDD is required to read the VENEER image. */
    mov ah, 0x41
    mov bx, 0x55aa
    int 0x13
    jc .Lnoedd
    cmp bx, 0xaa55
    jne .Lnoedd
    test cl, 1
    jz .Lnoedd

    mov ax, WORD PTR [VeneerSectors]
    test ax, ax
    jz .Lnoveneer
    cmp ax, 1024
    ja .Lnoveneer
    mov WORD PTR [Remaining], ax
    mov di, VENEER_SEG
.Lvload:
    mov cx, WORD PTR [Remaining]
    cmp cx, 32                       /* 16 KiB per request */
    jbe .Lchunk
    mov cx, 32
.Lchunk:
    push 0
    push 0
    push DWORD PTR [VeneerLba]
    push di                          /* buffer segment */
    push 0                           /* buffer offset */
    push cx                          /* sector count */
    push 0x10
    mov si, sp
    mov ah, 0x42
    mov dl, BYTE PTR [BootDrive]
    int 0x13
    jc .Lioerr
    add sp, 16
    movzx eax, cx
    add DWORD PTR [VeneerLba], eax
    sub WORD PTR [Remaining], cx
    shl cx, 5                        /* sectors -> paragraphs */
    add di, cx
    cmp WORD PTR [Remaining], 0
    jne .Lvload

    /* Validate the image header, then far-call it. */
    mov ax, VENEER_SEG
    mov es, ax
    cmp DWORD PTR es:[0], 0x31524e56
    jne .Lbadveneer
    mov ax, WORD PTR es:[4]
    mov cx, WORD PTR [VeneerSectors]
    cmp cx, 128                      /* >= 64 KiB: any 16-bit entry fits */
    jae .Lentryok
    shl cx, 9
    cmp ax, cx
    jae .Lbadveneer
.Lentryok:
    mov WORD PTR [VeneerEntry], ax
    xor ax, ax
    mov es, ax
    mov dl, BYTE PTR [BootDrive]
    .byte 0xff, 0x1e                 /* call far m16:16 */
    .word VeneerEntry
    jc .Lveneerfail
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, LOAD_ADDR
    sti

    /* Find the active entry. Boot flags other than 00/80 mean a bad table. */
    mov si, OFFSET FLAT:PartitionTable
    mov cx, 4
    xor bx, bx
.Lscan:
    mov al, BYTE PTR [si]
    cmp al, 0x80
    je .Lfound
    test al, 0x7f
    jnz .Linvalid
    add si, 16
    loop .Lscan
    mov al, 'A'
    jmp Fail
.Linvalid:
    mov al, 'B'
    jmp Fail

.Lfound:
    mov bp, si                       /* keep entry pointer for the VBR */
    mov ah, 0x41
    mov bx, 0x55aa
    mov dl, BYTE PTR [BootDrive]
    int 0x13
    jc .Lchs
    cmp bx, 0xaa55
    jne .Lchs
    test cl, 1
    jz .Lchs

    /* EDD read of one sector. DAP is built on the stack. */
    push 0
    push 0
    push DWORD PTR [bp + 8]          /* start LBA (low 32 bits) */
    push 0                           /* buffer segment */
    push LOAD_ADDR                   /* buffer offset */
    push 1                           /* sector count */
    push 0x10                        /* DAP size */
    mov si, sp
    mov ah, 0x42
    mov dl, BYTE PTR [BootDrive]
    int 0x13
    jc .Lioerr
    add sp, 16
    jmp .Lcheck

.Lchs:
    mov dh, BYTE PTR [bp + 1]
    mov cx, WORD PTR [bp + 2]
    mov dl, BYTE PTR [BootDrive]
    mov bx, LOAD_ADDR
    mov ax, 0x0201
    int 0x13
    jc .Lioerr

.Lcheck:
    cmp WORD PTR [LOAD_ADDR + 510], 0xaa55
    jne .Lnoos
    mov dl, BYTE PTR [BootDrive]
    mov si, bp
    .byte 0xea                      /* jmp far 0000:7C00 */
    .word LOAD_ADDR
    .word 0

.Lioerr:
    mov al, 'C'
    jmp Fail
.Lnoedd:
    mov al, 'E'
    jmp Fail
.Lnoveneer:
    mov al, 'F'
    jmp Fail
.Lbadveneer:
    mov al, 'G'
    jmp Fail
.Lveneerfail:
    mov al, 'H'
    jmp Fail
.Lnoos:
    mov al, 'D'

Fail:                               /* AL = error code, prints "E<code>" */
    mov cl, al
    mov al, 'E'
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    mov al, cl
    mov ah, 0x0e
    int 0x10
.Lhalt:
    cli
    hlt
    jmp .Lhalt

BootDrive:   .byte 0
Remaining:   .word 0
VeneerEntry: .word 0, VENEER_SEG

.org 0x1b0
VeneerLba:       .long 1
VeneerSectors:   .word 0
                 .word 0
DiskSignature:   .long 0
                 .word 0
PartitionTable:  .fill 64, 1, 0
.org 0x1fe
    .byte 0x55, 0xaa
.section .note.GNU-stack,"",@progbits

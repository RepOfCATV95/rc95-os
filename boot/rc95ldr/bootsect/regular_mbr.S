/*
 * PROJECT:    RC95-OS RC95LDR BIOS boot sector
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Generic MBR: start the volume boot record of the active partition
 * COPYRIGHT:  Copyright 1997-2028 RC95-OS contributors
 *
 * Contract
 *   In : DL = BIOS boot drive.
 *   Out: the active partition's first sector is loaded at 0000:7C00 and
 *        entered with DL = boot drive, DS:SI -> its 16-byte partition entry.
 *
 * Errors print "E<code>" and halt: A no active partition, B bad boot flag in
 * table, C disk read error, D VBR lacks 55AA signature.
 * Build: link at 0x0600 (the MBR relocates itself there), flat binary, 512 bytes.
 * The disk signature (0x1B8) and partition table (0x1BE) are left zeroed; the
 * partitioning tool owns them and must not overwrite the code before 0x1B8.
 * INT 13h extensions are preferred; if absent, the entry's CHS tuple is used.
 */
.intel_syntax noprefix
.section .text,"ax",@progbits
.code16

.set RELOC_ADDR, 0x0600
.set LOAD_ADDR,  0x7c00

.global Rc95LdrRegularMbr
Rc95LdrRegularMbr:
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

.org 0x1b8
DiskSignature:   .long 0
                 .word 0
PartitionTable:  .fill 64, 1, 0
.org 0x1fe
    .byte 0x55, 0xaa
.section .note.GNU-stack,"",@progbits

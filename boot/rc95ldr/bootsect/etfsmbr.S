/*
 * PROJECT:    RC95-OS RC95LDR BIOS boot sector
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    El Torito (CD/DVD) boot image: load /RC95LDR.SYS from ISO 9660 and start it
 * COPYRIGHT:  Copyright 1997-2028 RC95-OS contributors
 *
 * Boot catalog: no-emulation, load segment 0 (=07C0), boot-load-size 4
 * (4 x 512 = one 2048-byte CD sector, the whole image).
 *
 * Contract
 *   In : DL = BIOS boot drive (CD). CD INT 13h EDD reads use 2048-byte sectors.
 *   Out: /RC95LDR.SYS (root directory, ISO 9660 primary descriptor; names
 *        "RC95LDR.SYS" or "RC95LDR.SYS;1") is loaded flat at physical 0x8000
 *        and entered at 0000:8000 with DL = boot drive, DS = ES = 0.
 *        Maximum image size 0x97000 bytes (up to the EBDA/video hole at 0x9F000).
 *
 * Memory: image relocates 7C00 -> 0600 (link at 0x0600); 2 KiB scratch at
 * 0x7000; stack top 0x7C00. Joliet/Rock Ridge are not used: the plain ISO 9660
 * name must exist, which every mastering tool writes alongside Joliet.
 * Errors print a message and halt. EDD is mandatory (no CHS on CD).
 */
.intel_syntax noprefix
.section .text,"ax",@progbits
.code16

.set RELOC_ADDR, 0x0600
.set BOOT_ADDR,  0x7c00
.set SCRATCH,    0x7000
.set LOAD_ADDR,  0x8000
.set LOAD_SEG,   0x0800
.set MAX_BYTES,  0x97000

.global Rc95LdrEtfsMbr
Rc95LdrEtfsMbr:
    cli
    /* BIOS may enter at 07C0:0000 or 0000:7C00; normalise to CS = 0. */
    .byte 0xea
    .word Normalized - Rc95LdrEtfsMbr + BOOT_ADDR
    .word 0
Normalized:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, BOOT_ADDR
    sti
    cld
    mov si, BOOT_ADDR
    mov di, RELOC_ADDR
    mov cx, 1024                     /* 2048 bytes */
    rep movsw
    .byte 0xea                       /* jmp far 0000:Relocated */
    .word Relocated
    .word 0

Relocated:
    mov BYTE PTR [BootDrive], dl
    mov ah, 0x41
    mov bx, 0x55aa
    int 0x13
    jc .Lnoedd
    cmp bx, 0xaa55
    jne .Lnoedd
    test cl, 1
    jz .Lnoedd

    /* Walk the volume descriptor set (LBA 16..) for the primary descriptor. */
    mov eax, 16
.Lvd:
    mov DWORD PTR [Lba], eax
    call ReadSector
    jc .Lioerr
    cmp DWORD PTR [SCRATCH + 1], 0x30304443     /* "CD00" */
    jne .Lnoiso
    cmp BYTE PTR [SCRATCH + 5], 0x31            /* "1" */
    jne .Lnoiso
    mov al, BYTE PTR [SCRATCH]
    cmp al, 1
    je .Lpvd
    cmp al, 255                                  /* terminator: no PVD */
    je .Lnoiso
    mov eax, DWORD PTR [Lba]
    inc eax
    cmp eax, 64
    jb .Lvd
    jmp .Lnoiso

.Lpvd:
    /* Root directory record is at PVD + 156: extent +2, size +10. */
    mov eax, DWORD PTR [SCRATCH + 156 + 2]
    mov DWORD PTR [Lba], eax
    mov eax, DWORD PTR [SCRATCH + 156 + 10]
    mov DWORD PTR [Remain], eax

.Ldirsec:
    mov eax, DWORD PTR [Lba]
    call ReadSector
    jc .Lioerr
    mov si, SCRATCH
.Lrec:
    cmp BYTE PTR [si], 0                         /* padding to sector end */
    je .Lnextsec
    test BYTE PTR [si + 25], 2                   /* skip directories */
    jnz .Lskip
    movzx cx, BYTE PTR [si + 32]                 /* identifier length */
    cmp cx, 11
    je .Lcompare
    cmp cx, 13
    jne .Lskip
    cmp BYTE PTR [si + 33 + 11], 0x3b            /* ';' version suffix */
    jne .Lskip
.Lcompare:
    push si
    lea si, [si + 33]
    mov di, OFFSET FLAT:FileName
    mov cx, 11
    repe cmpsb
    pop si
    je .Lfound
.Lskip:
    movzx ax, BYTE PTR [si]
    add si, ax
    cmp si, SCRATCH + 2048
    jb .Lrec
.Lnextsec:
    inc DWORD PTR [Lba]
    cmp DWORD PTR [Remain], 2048
    jbe .Lnofile
    sub DWORD PTR [Remain], 2048
    jmp .Ldirsec

.Lfound:
    mov eax, DWORD PTR [si + 2]                  /* extent LBA */
    mov DWORD PTR [Lba], eax
    mov eax, DWORD PTR [si + 10]                 /* data length */
    test eax, eax
    jz .Lbadsize
    cmp eax, MAX_BYTES
    ja .Lbadsize
    add eax, 2047
    shr eax, 11
    mov DWORD PTR [Remain], eax                  /* sector count */
    mov WORD PTR [DestSeg], LOAD_SEG

.Lload:
    mov ax, WORD PTR [DestSeg]
    mov es, ax
    xor bx, bx
    mov eax, DWORD PTR [Lba]
    call ReadSector
    jc .Lioerr
    inc DWORD PTR [Lba]
    add WORD PTR [DestSeg], 0x80                 /* 2048 bytes = 0x80 paragraphs */
    dec DWORD PTR [Remain]
    jnz .Lload

    xor ax, ax
    mov es, ax
    mov dl, BYTE PTR [BootDrive]
    .byte 0xea                                   /* jmp far 0000:LOAD_ADDR */
    .word LOAD_ADDR
    .word 0

/* EAX = LBA, ES:BX = destination; reads one 2048-byte sector. CF = error. */
ReadSector:
    xor bx, bx
    cmp WORD PTR [DestSeg], 0
    jne .Lrs_have_dest
    mov bx, SCRATCH
    push 0
    push 0
    push eax
    push 0
    push bx
    jmp .Lrs_go
.Lrs_have_dest:
    push 0
    push 0
    push eax
    push es
    push bx
.Lrs_go:
    push 1
    push 0x10
    mov si, sp
    mov dl, BYTE PTR [BootDrive]
    mov ah, 0x42
    int 0x13
    lea sp, [si + 16]
    ret

.Lnoedd:
    mov si, OFFSET FLAT:MsgNoEdd
    jmp Fail
.Lioerr:
    mov si, OFFSET FLAT:MsgIo
    jmp Fail
.Lnoiso:
    mov si, OFFSET FLAT:MsgNoIso
    jmp Fail
.Lnofile:
    mov si, OFFSET FLAT:MsgNoFile
    jmp Fail
.Lbadsize:
    mov si, OFFSET FLAT:MsgBadSize
Fail:
    xor ax, ax
    mov ds, ax
.Lprint:
    lodsb
    test al, al
    jz .Lhalt
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp .Lprint
.Lhalt:
    cli
    hlt
    jmp .Lhalt

BootDrive:  .byte 0
.balign 4
Lba:        .long 0
Remain:     .long 0
DestSeg:    .word 0
FileName:   .ascii "RC95LDR.SYS"
MsgNoEdd:   .asciz "RC95LDR: no INT13 extensions"
MsgIo:      .asciz "RC95LDR: CD read error"
MsgNoIso:   .asciz "RC95LDR: no ISO 9660 PVD"
MsgNoFile:  .asciz "RC95LDR: RC95LDR.SYS not found"
MsgBadSize: .asciz "RC95LDR: RC95LDR.SYS size invalid"

.org 2048
.section .note.GNU-stack,"",@progbits

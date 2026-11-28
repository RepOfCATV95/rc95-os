/*
 * PROJECT:    RC95-OS RC95LDR BIOS boot sector
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    FAT32 volume boot record: load /RC95LDR.SYS and start it
 * COPYRIGHT:  Copyright 1997-2028 RC95-OS contributors
 *
 * Contract
 *   In : DL = BIOS boot drive; the VBR sits at 0000:7C00 (from the MBR or a
 *        superfloppy boot). The partition start is taken from the BPB
 *        "hidden sectors" field, as every FAT formatter writes it.
 *   Out: /RC95LDR.SYS (8.3 entry "RC95LDR SYS" in the root directory) is
 *        loaded flat at physical 0x8000 and entered at 0000:8000 with
 *        DL = boot drive, DS = ES = 0. Maximum size ends at 0x9E000.
 *
 * Scope: FAT32 only, 512-byte logical sectors, INT 13h EDD (no CHS fallback).
 * Build: link at 0x7C00, flat binary, 512 bytes. Bytes 0x03..0x59 (OEM + BPB +
 * EBPB) are written by the formatter; the installer must merge them with this
 * image (keep bytes 0x00..0x02 and 0x5A..0x1FF from this file).
 * Errors print one letter and halt: F not FAT32/unsupported BPB, D disk error,
 * N RC95LDR.SYS not found, B bad cluster chain or image too large.
 *
 * Memory: variables 0x7A00, stack top 0x7000, FAT/directory scratch 0x7E00.
 */
.intel_syntax noprefix
.section .text,"ax",@progbits
.code16

.set BPB,         0x7c00
.set BPB_BPS,     BPB + 0x0b
.set BPB_SPC,     BPB + 0x0d
.set BPB_RSVD,    BPB + 0x0e
.set BPB_NFATS,   BPB + 0x10
.set BPB_ROOTENT, BPB + 0x11
.set BPB_FATSZ16, BPB + 0x16
.set BPB_HIDDEN,  BPB + 0x1c
.set BPB_FATSZ32, BPB + 0x24
.set BPB_ROOTCLU, BPB + 0x2c

.set VARS,        0x7a00
.set V_DRIVE,     VARS + 0
.set V_FATSTART,  VARS + 4
.set V_DATASTART, VARS + 8
.set V_CURRENT,   VARS + 12
.set SCRATCH,     0x7e00
.set LOAD_ADDR,   0x8000
.set LOAD_SEG,    0x0800
.set SEG_LIMIT,   0x9e00

.global Rc95LdrFatMbr
Rc95LdrFatMbr:
    .byte 0xeb, 0x58, 0x90          /* jmp short 0x5A; nop */
    .fill 0x5a - 3, 1, 0            /* OEM name, BPB, EBPB: owned by formatter */

Start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax                      /* MOV SS holds off interrupts for SP */
    mov sp, 0x7000
    cld
    mov BYTE PTR [V_DRIVE], dl

    mov al, 'F'
    cmp WORD PTR [BPB_BPS], 512
    jne Fail
    mov ax, WORD PTR [BPB_ROOTENT]
    or ax, WORD PTR [BPB_FATSZ16]   /* both are zero on FAT32 */
    jnz Fail
    mov ah, 0x41
    mov bx, 0x55aa
    int 0x13
    jc Fail
    cmp bx, 0xaa55
    jne Fail
    test cl, 1
    jz Fail

    movzx eax, WORD PTR [BPB_RSVD]
    add eax, DWORD PTR [BPB_HIDDEN]
    mov DWORD PTR [V_FATSTART], eax
    movzx ecx, BYTE PTR [BPB_NFATS]
    imul ecx, DWORD PTR [BPB_FATSZ32]
    add eax, ecx
    mov DWORD PTR [V_DATASTART], eax

    mov eax, DWORD PTR [BPB_ROOTCLU]
.Ldir:
    mov DWORD PTR [V_CURRENT], eax
    call ClusterToLba
    movzx cx, BYTE PTR [BPB_SPC]
.Ldirsec:
    push cx
    push eax
    mov bx, SCRATCH
    call ReadSector
    mov si, SCRATCH
    mov cx, 16
.Lentry:
    cmp BYTE PTR [si], 0            /* end of directory */
    je .Lnotfound
    cmp BYTE PTR [si], 0xe5         /* deleted */
    je .Lnext
    test BYTE PTR [si + 11], 0x18   /* volume label, directory, and LFN */
    jnz .Lnext
    push si
    push cx
    mov di, OFFSET FLAT:FileName
    mov cx, 11
    repe cmpsb
    pop cx
    pop si
    je .Lfound
.Lnext:
    add si, 32
    loop .Lentry
    pop eax
    pop cx
    inc eax
    loop .Ldirsec
    mov eax, DWORD PTR [V_CURRENT]
    call NextCluster
    jc .Lnotfound
    cmp eax, 2
    jb .Lbad
    jmp .Ldir

.Lfound:
    mov ax, WORD PTR [si + 0x14]    /* first cluster high */
    shl eax, 16
    mov ax, WORD PTR [si + 0x1a]    /* first cluster low */
    cmp eax, 2
    jb .Lbad
    mov dx, LOAD_SEG
    mov es, dx
.Lfile:
    mov DWORD PTR [V_CURRENT], eax
    call ClusterToLba
    movzx cx, BYTE PTR [BPB_SPC]
.Lfilesec:
    push cx
    push eax
    xor bx, bx
    call ReadSector
    mov ax, es
    add ax, 0x20                    /* 512 bytes */
    mov es, ax
    cmp ax, SEG_LIMIT
    jae .Lbad
    pop eax
    pop cx
    inc eax
    loop .Lfilesec
    mov eax, DWORD PTR [V_CURRENT]
    call NextCluster
    jc .Ldone
    cmp eax, 2
    jb .Lbad
    jmp .Lfile

.Ldone:
    xor ax, ax
    mov es, ax
    mov dl, BYTE PTR [V_DRIVE]
    .byte 0xea                      /* jmp far 0000:LOAD_ADDR */
    .word LOAD_ADDR
    .word 0

/* EAX = cluster -> EAX = first LBA of the cluster. */
ClusterToLba:
    sub eax, 2
    movzx ecx, BYTE PTR [BPB_SPC]
    mul ecx
    add eax, DWORD PTR [V_DATASTART]
    ret

/* EAX = cluster -> EAX = next cluster. CF set at end of chain. */
NextCluster:
    push es
    mov edi, eax
    shr eax, 7                      /* 128 entries per sector */
    add eax, DWORD PTR [V_FATSTART]
    and di, 127
    shl di, 2
    xor bx, bx
    mov es, bx
    mov bx, SCRATCH
    call ReadSector
    mov eax, DWORD PTR [di + SCRATCH]
    and eax, 0x0fffffff
    pop es
    cmp eax, 0x0ffffff8
    cmc
    ret

/* EAX = LBA, ES:BX = destination. Fatal on error. */
ReadSector:
    push 0
    push 0
    push eax
    push es
    push bx
    push 1
    push 0x10
    mov si, sp
    mov dl, BYTE PTR [V_DRIVE]
    mov ah, 0x42
    int 0x13
    lea sp, [si + 16]
    mov al, 'D'
    jc Fail
    ret

.Lnotfound:
    mov al, 'N'
    jmp Fail
.Lbad:
    mov al, 'B'
Fail:                               /* AL = error code letter */
    mov ah, 0x0e
    xor bx, bx
    int 0x10
.Lhalt:
    cli
    hlt
    jmp .Lhalt

FileName: .ascii "RC95LDR SYS"

.org 0x1fe
    .byte 0x55, 0xaa
.section .note.GNU-stack,"",@progbits

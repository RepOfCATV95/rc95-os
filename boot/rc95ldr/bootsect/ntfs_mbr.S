/*
 * PROJECT:    RC95-OS RC95LDR BIOS boot sector
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    NTFS volume boot record: load /RC95LDR.SYS and start it
 * COPYRIGHT:  Copyright 1997-2028 RC95-OS contributors
 *
 * Image layout: 8 sectors (4 KiB). Sector 0 holds the BPB and a small loader;
 * sectors 1..7 live in the $Boot file's reserved area (NTFS reserves 16
 * sectors) and hold the NTFS reader. The installer writes all 8 sectors at the
 * partition start, keeping bytes 0x03..0x53 (OEM id + BPB) of sector 0 as the
 * formatter wrote them.
 *
 * Contract
 *   In : DL = BIOS boot drive; VBR at 0000:7C00. Partition start comes from
 *        the BPB "hidden sectors" field (0x1C).
 *   Out: /RC95LDR.SYS is loaded flat at physical 0x8000 and entered at
 *        0000:8000 with DL = boot drive, DS = ES = 0. Max size 0x97000 bytes.
 *
 * Supported: 512-byte sectors, INT 13h EDD, $MFT described by record 0 (the
 * $DATA runlist is followed, so a fragmented $MFT works), root directory
 * $INDEX_ROOT plus every $INDEX_ALLOCATION block (linear scan, so no collation
 * order is assumed), non-resident, uncompressed, unencrypted, non-sparse
 * $DATA. Not supported (fail closed): $ATTRIBUTE_LIST on $MFT record 0,
 * resident $DATA, LCNs >= 2^31, records/index blocks > 4 KiB.
 *
 * Errors print one letter and halt: F unsupported BPB, D disk error, M $MFT
 * record 0 unusable, X bad FILE record, U update-sequence mismatch, I no root
 * index, N file not found, R unsupported $DATA flavour, S unmapped/sparse
 * cluster, B size invalid, Z runlist too long.
 *
 * Build: link at 0x0600 (relocates itself), flat binary, 4096 bytes.
 * Memory: image 0600-15FF, vars 1F00, FILE record 2000, index block 3000,
 * runlists 4000..43FF, stack top 7C00.
 */
.intel_syntax noprefix
.section .text,"ax",@progbits
.code16

.set IMAGE_SECTORS, 8
.set BASE,       0x0600
.set BPB,        BASE
.set BPB_SPC,    BPB + 0x0d
.set BPB_HIDDEN, BPB + 0x1c
.set BPB_MFTLCN, BPB + 0x30
.set BPB_RECSZ,  BPB + 0x40
.set BPB_IDXSZ,  BPB + 0x44

.set V_DRIVE,    0x1f00
.set V_SPC,      0x1f04
.set V_RECSZ,    0x1f08
.set V_IDXSZ,    0x1f0c
.set V_TGT,      0x1f10
.set V_RSTART,   0x1f14
.set V_RLCN,     0x1f18
.set V_RLEN,     0x1f1c
.set V_OFFSZ,    0x1f20
.set V_SPARSE,   0x1f21
.set V_SUB,      0x1f24
.set V_LSEC,     0x1f28
.set V_CNT,      0x1f2c
.set V_BLK,      0x1f30
.set V_DIRSIZE,  0x1f34
.set V_DIRRUNS,  0x1f38
.set V_NSEC,     0x1f3c
.set V_SEG,      0x1f40

.set RECBUF,     0x2000
.set IDXBUF,     0x3000
.set MFTRUNS,    0x4000          /* 256 bytes */
.set FILERUNS,   0x4100          /* 512 bytes */
.set LOAD_ADDR,  0x8000
.set LOAD_SEG,   0x0800
.set SEG_LIMIT,  0x9e00
.set MAX_BYTES,  0x97000
.set STAGE2,     0x0800

.global Rc95LdrNtfsMbr
Rc95LdrNtfsMbr:
    .byte 0xeb, 0x52, 0x90          /* jmp short 0x54; nop */
    .fill 0x54 - 3, 1, 0            /* OEM id + BPB: owned by the formatter */

Start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    cld
    mov si, 0x7c00
    mov di, BASE
    mov cx, 256
    rep movsw
    .byte 0xea                      /* jmp far 0000:Relocated */
    .word Relocated
    .word 0

Relocated:
    mov BYTE PTR [V_DRIVE], dl
    mov al, 'D'
    mov ah, 0x41
    mov bx, 0x55aa
    int 0x13
    jc Fail
    cmp bx, 0xaa55
    jne Fail
    mov eax, DWORD PTR [BPB_HIDDEN]
    inc eax
    mov cx, IMAGE_SECTORS - 1
    mov bx, STAGE2
    call ReadAbs
    jmp Main

/* EAX = absolute LBA, CX = sectors, ES:BX = destination. Preserves BX/CX/SI. */
ReadAbs:
    push si
    push cx
    push bx
    push 0
    push 0
    push eax
    push es
    push bx
    push cx
    push 0x10
    mov si, sp
    mov dl, BYTE PTR [V_DRIVE]
    mov ah, 0x42
    int 0x13
    lea sp, [si + 16]
    pop bx
    pop cx
    pop si
    mov al, 'D'
    jc Fail
    ret

Fail:                               /* AL = error code letter */
    mov ah, 0x0e
    xor bx, bx
    int 0x10
.Lhalt:
    cli
    hlt
    jmp .Lhalt

.org 0x1fe
    .byte 0x55, 0xaa

/* ---------------------------- sectors 1..7 ----------------------------- */

Main:
    mov al, 'F'
    cmp WORD PTR [BPB + 0x0b], 512
    jne Fail
    movzx eax, BYTE PTR [BPB_SPC]
    test eax, eax
    jz Fail
    mov DWORD PTR [V_SPC], eax

    movsx eax, BYTE PTR [BPB_RECSZ]         /* clusters per FILE record */
    call SizeFromField
    mov DWORD PTR [V_RECSZ], eax
    movsx eax, BYTE PTR [BPB_IDXSZ]         /* clusters per index block */
    call SizeFromField
    mov DWORD PTR [V_IDXSZ], eax

    cmp DWORD PTR [BPB_MFTLCN + 4], 0
    jne Fail
    mov eax, DWORD PTR [BPB_MFTLCN]
    test eax, eax
    js Fail

    /* Synthetic one-run map so $MFT record 0 can be read. */
    mov di, MFTRUNS
    mov BYTE PTR [di], 0x44
    mov DWORD PTR [di + 1], 0x00100000
    mov DWORD PTR [di + 5], eax
    mov BYTE PTR [di + 9], 0
    xor eax, eax
    call ReadRecord
    mov al, 'M'
    mov ecx, 0x80
    xor bl, bl
    call FindAttr
    jc Fail
    cmp BYTE PTR [si + 8], 1
    jne Fail
    mov di, MFTRUNS
    mov dx, 256
    call CopyRuns                           /* real $MFT runlist */

    mov eax, 5                              /* root directory */
    call ReadRecord
    mov al, 'I'
    mov ecx, 0x90                           /* $INDEX_ROOT "$I30" */
    mov bl, 4
    call FindAttr
    jc Fail
    mov bx, WORD PTR [si + 0x14]
    add bx, si                              /* BX -> INDEX_ROOT content */
    lea si, [bx + 0x10]
    mov bp, si
    add bp, WORD PTR [bx + 0x14]            /* end of node entries */
    add si, WORD PTR [bx + 0x10]            /* first entry */
    call ScanEntries
    jnc .Lfound

    mov al, 'N'
    mov ecx, 0xa0                           /* $INDEX_ALLOCATION "$I30" */
    mov bl, 4
    call FindAttr
    jc Fail
    cmp BYTE PTR [si + 8], 1
    jne Fail
    mov eax, DWORD PTR [si + 0x30]
    mov DWORD PTR [V_DIRSIZE], eax
    mov bx, WORD PTR [si + 0x20]
    add bx, si
    mov WORD PTR [V_DIRRUNS], bx            /* points into RECBUF */
    mov DWORD PTR [V_BLK], 0
.Lblock:
    mov eax, DWORD PTR [V_BLK]
    shl eax, 9
    cmp eax, DWORD PTR [V_DIRSIZE]
    jae .Lnotfound
    mov ecx, DWORD PTR [V_IDXSZ]
    shr ecx, 9
    mov WORD PTR [V_CNT], cx
    mov bx, IDXBUF
.Lblksec:
    mov eax, DWORD PTR [V_BLK]
    mov si, WORD PTR [V_DIRRUNS]
    call ReadFileSector
    add bx, 512
    inc DWORD PTR [V_BLK]
    dec WORD PTR [V_CNT]
    jnz .Lblksec
    cmp DWORD PTR [IDXBUF], 0x58444e49      /* "INDX" */
    jne .Lblock
    mov si, IDXBUF
    call Fixup
    lea si, [IDXBUF + 0x18]
    mov bp, si
    add bp, WORD PTR [IDXBUF + 0x1c]
    add si, WORD PTR [IDXBUF + 0x18]
    call ScanEntries
    jc .Lblock

.Lfound:                                    /* EAX = MFT record number */
    call ReadRecord
    mov al, 'R'
    mov ecx, 0x80
    xor bl, bl
    call FindAttr
    jc Fail
    cmp BYTE PTR [si + 8], 1                /* resident: unsupported */
    jne Fail
    test WORD PTR [si + 0x0c], 0xc001       /* compressed/encrypted/sparse */
    jnz Fail
    mov al, 'B'
    cmp DWORD PTR [si + 0x34], 0
    jne Fail
    mov eax, DWORD PTR [si + 0x30]
    test eax, eax
    jz Fail
    cmp eax, MAX_BYTES
    ja Fail
    add eax, 511
    shr eax, 9
    mov DWORD PTR [V_NSEC], eax
    mov di, FILERUNS
    mov dx, 512
    call CopyRuns

    mov WORD PTR [V_SEG], LOAD_SEG
    mov DWORD PTR [V_LSEC], 0
.Lload:
    mov ax, WORD PTR [V_SEG]
    mov es, ax
    xor bx, bx
    mov eax, DWORD PTR [V_LSEC]
    mov si, FILERUNS
    call ReadFileSector
    add WORD PTR [V_SEG], 0x20
    inc DWORD PTR [V_LSEC]
    mov eax, DWORD PTR [V_LSEC]
    cmp eax, DWORD PTR [V_NSEC]
    jb .Lload

    xor ax, ax
    mov es, ax
    mov dl, BYTE PTR [V_DRIVE]
    .byte 0xea                              /* jmp far 0000:LOAD_ADDR */
    .word LOAD_ADDR
    .word 0

.Lnotfound:
    mov al, 'N'
    jmp Fail

/* EAX = signed BPB size field -> EAX = bytes (512..4096). */
SizeFromField:
    test eax, eax
    js .Lneg
    imul eax, DWORD PTR [V_SPC]
    shl eax, 9
    jmp .Lchk
.Lneg:
    neg eax
    mov cl, al
    mov eax, 1
    shl eax, cl
.Lchk:
    mov cl, 'F'
    cmp eax, 512
    jb .Lbadsize
    cmp eax, 4096
    ja .Lbadsize
    ret
.Lbadsize:
    mov al, 'F'
    jmp Fail

/* EAX = record number -> RECBUF holds the fixed-up FILE record. */
ReadRecord:
    mul DWORD PTR [V_RECSZ]
    shr eax, 9
    mov DWORD PTR [V_LSEC], eax
    mov ecx, DWORD PTR [V_RECSZ]
    shr ecx, 9
    mov WORD PTR [V_CNT], cx
    mov bx, RECBUF
.Lrr:
    mov eax, DWORD PTR [V_LSEC]
    mov si, OFFSET FLAT:MFTRUNS
    call ReadFileSector
    add bx, 512
    inc DWORD PTR [V_LSEC]
    dec WORD PTR [V_CNT]
    jnz .Lrr
    mov al, 'X'
    cmp DWORD PTR [RECBUF], 0x454c4946      /* "FILE" */
    jne Fail
    mov si, RECBUF
    jmp Fixup

/* SI = buffer. Apply the update sequence array to each 512-byte sector. */
Fixup:
    mov al, 'U'
    movzx bx, BYTE PTR [si + 6]
    cmp WORD PTR [si + 6], 9
    ja Fail
    mov di, WORD PTR [si + 4]
    add di, si
    mov dx, WORD PTR [di]                   /* update sequence number */
    dec bx
    jz .Lfxdone
    lea bp, [si + 510]
.Lfx:
    add di, 2
    cmp WORD PTR [bp], dx
    jne Fail
    mov ax, WORD PTR [di]
    mov WORD PTR [bp], ax
    add bp, 512
    dec bx
    jnz .Lfx
.Lfxdone:
    ret

/* ECX = type, BL = required name length (0, or 4 for "$I30"). RECBUF.
 * Out: SI -> attribute, CF set if absent. */
FindAttr:
    mov si, RECBUF
    add si, WORD PTR [si + 0x14]
.Lfa:
    cmp DWORD PTR [si], 0xffffffff
    je .Lnone
    cmp DWORD PTR [si], ecx
    jne .Lnext
    cmp BYTE PTR [si + 9], bl
    jne .Lnext
    test bl, bl
    jz .Lhit
    mov di, WORD PTR [si + 0x0a]
    add di, si
    cmp DWORD PTR [di], 0x00490024          /* "$I" */
    jne .Lnext
    cmp DWORD PTR [di + 4], 0x00300033      /* "30" */
    jne .Lnext
.Lhit:
    clc
    ret
.Lnext:
    mov eax, DWORD PTR [si + 4]
    test eax, eax
    jz .Lnone
    add si, ax
    cmp si, RECBUF + 4096
    jb .Lfa
.Lnone:
    stc
    ret

/* SI = attribute, DI = destination, DX = capacity. Copies the runlist. */
CopyRuns:
    mov al, 'Z'
    mov cx, WORD PTR [si + 4]
    mov ax, WORD PTR [si + 0x20]
    sub cx, ax
    jbe .Lcrfail
    cmp cx, dx
    jae .Lcrfail
    add si, ax
    rep movsb
    mov BYTE PTR [di], 0
    ret
.Lcrfail:
    mov al, 'Z'
    jmp Fail

/* SI = first entry, BP = end of entries. CF clear + EAX = record number. */
ScanEntries:
.Lse:
    cmp si, bp
    jae .Lsenone
    test BYTE PTR [si + 12], 2              /* last entry */
    jnz .Lsenone
    cmp WORD PTR [si + 8], 0
    je .Lsenone
    cmp BYTE PTR [si + 0x50], 11
    jne .Lsenext
    push si
    lea si, [si + 0x52]
    mov di, OFFSET FLAT:NameW
    mov cx, 11
.Lcmp:
    lodsw
    cmp ax, 0x61
    jb .Lcase
    cmp ax, 0x7a
    ja .Lcase
    sub ax, 0x20
.Lcase:
    cmp ax, WORD PTR [di]
    jne .Lmismatch
    add di, 2
    loop .Lcmp
    pop si
    cmp WORD PTR [si + 4], 0                /* record number > 32 bits */
    jne .Lsenone
    mov eax, DWORD PTR [si]
    clc
    ret
.Lmismatch:
    pop si
.Lsenext:
    movzx eax, WORD PTR [si + 8]
    add si, ax
    jmp .Lse
.Lsenone:
    stc
    ret

/* SI = runlist, EAX = file sector index, ES:BX = destination. */
ReadFileSector:
    xor edx, edx
    mov ecx, DWORD PTR [V_SPC]
    div ecx
    mov DWORD PTR [V_SUB], edx
    call RunLookup
    jc .Lunmapped
    mul DWORD PTR [V_SPC]
    add eax, DWORD PTR [V_SUB]
    add eax, DWORD PTR [BPB_HIDDEN]
    mov cx, 1
    jmp ReadAbs
.Lunmapped:
    mov al, 'S'
    jmp Fail

/* SI = runlist, EAX = VCN -> EAX = LCN. CF set if sparse or unmapped. */
RunLookup:
    mov DWORD PTR [V_TGT], eax
    xor eax, eax
    mov DWORD PTR [V_RSTART], eax
    mov DWORD PTR [V_RLCN], eax
.Lrun:
    mov al, BYTE PTR [si]
    test al, al
    jz .Lmiss
    inc si
    mov dl, al
    shr dl, 4
    mov BYTE PTR [V_OFFSZ], dl
    and al, 15
    mov cl, al
    mov al, 'Z'
    cmp cl, 4
    ja Fail
    test cl, cl
    jz Fail
    call LoadLE
    mov DWORD PTR [V_RLEN], eax
    mov cl, BYTE PTR [V_OFFSZ]
    cmp cl, 4
    ja Fail
    mov BYTE PTR [V_SPARSE], 1
    test cl, cl
    jz .Lcheck
    mov BYTE PTR [V_SPARSE], 0
    call LoadLE
    mov ch, 4                               /* sign-extend the delta */
    sub ch, BYTE PTR [V_OFFSZ]
    shl ch, 3
    mov cl, ch
    shl eax, cl
    sar eax, cl
    add DWORD PTR [V_RLCN], eax
.Lcheck:
    mov eax, DWORD PTR [V_TGT]
    sub eax, DWORD PTR [V_RSTART]
    cmp eax, DWORD PTR [V_RLEN]
    jb .Lin
    mov ecx, DWORD PTR [V_RLEN]
    add DWORD PTR [V_RSTART], ecx
    jmp .Lrun
.Lin:
    cmp BYTE PTR [V_SPARSE], 0
    jne .Lmiss
    add eax, DWORD PTR [V_RLCN]
    clc
    ret
.Lmiss:
    stc
    ret

/* SI = bytes, CL = size (0..4) -> EAX = little-endian value, SI advanced. */
LoadLE:
    xor eax, eax
    xor ch, ch
    test cl, cl
    jz .Llz
    lea di, [si + 1]
    add di, cx
    dec di
    mov si, di
.Lle:
    dec di
    shl eax, 8
    mov al, BYTE PTR [di]
    dec cx
    jnz .Lle
.Llz:
    ret

NameW:
    .word 0x52, 0x43, 0x39, 0x35, 0x4c, 0x44, 0x52, 0x2e, 0x53, 0x59, 0x53

.org IMAGE_SECTORS * 512
.section .note.GNU-stack,"",@progbits

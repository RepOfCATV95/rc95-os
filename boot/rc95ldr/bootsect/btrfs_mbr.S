/*
 * PROJECT:    RC95-OS RC95LDR BIOS boot sector
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Btrfs boot sector: load /VENEER.EXE and /RC95LDR.SYS, run VENEER, start RC95LDR
 * COPYRIGHT:  Copyright 1997-2028 RC95-OS contributors
 *
 * Btrfs has no BPB and reserves its first 64 KiB, so this image is 12 sectors
 * at the partition start (sector 0 = loader, sectors 1..11 = the Btrfs reader);
 * the superblock at 64 KiB is untouched. The installer may patch the dword at
 * 0x1B0 (PartitionLba) with the partition's start LBA; 0xFFFFFFFF (default)
 * means "take it from the MBR's DS:SI partition entry, else 0".
 *
 * Contract
 *   In : DL = BIOS boot drive, VBR at 0000:7C00, DS = 0.
 *   Out: 1. /VENEER.EXE is loaded flat at 8000:0000 (image contract identical
 *           to veneer_mbr.S: 'VNR1' signature, word entry offset at +4) and
 *           FAR CALLed with DL = boot drive, DS = ES = 0. It must preserve
 *           0000:0600..0000:9FFF and 8000:0000 upward only for itself, and
 *           RETF with CF clear.
 *        2. /RC95LDR.SYS (loaded flat at 0x8000 before VENEER runs) is entered
 *           at 0000:8000 with DL = boot drive, DS = ES = 0.
 *   Limits: RC95LDR.SYS <= 0x67000 bytes, VENEER.EXE <= 0x1D000 bytes.
 *
 * Supported: single-device Btrfs, SINGLE/DUP/RAID1-style profiles read from the
 * first stripe, nodesize 4..16 KiB, chunk tree of height <= 1, default
 * subvolume (DIR_ITEM "default" in the root tree, else subvolume 5), regular
 * uncompressed non-inline extents. Metadata/data checksums are NOT verified.
 * Fail closed (letter + halt): RAID0/5/6/10, multi-device, compression, inline
 * or preallocated extents, holes, deeper chunk trees, > 64 extents per file.
 *
 * Errors: F bad superblock/unsupported geometry, D disk, P unsupported profile
 * or device layout, C too many chunks, M unmapped logical address, L chunk
 * tree too deep, T tree node mismatch, N file/subvolume not found, R
 * unsupported extent, B size invalid, Z too many extents, G VENEER header bad,
 * H VENEER reported failure.
 *
 * Build: link at 0x0600, flat binary, 6144 bytes.
 * Memory: image 0600..1DFF, superblock 2600, vars 3600, chunk table 3700,
 * extent tables 4300 (2 x 520 bytes), child list 4700, node buffer 8000.
 */
.intel_syntax noprefix
.section .text,"ax",@progbits
.code16

.set IMAGE_SECTORS, 12
.set BASE,        0x0600
.set STAGE2,      0x0800

.set SB,          0x2600
.set V_DRIVE,     0x3600
.set V_PART,      0x3604
.set V_NSEC,      0x3608          /* sectors per node (word) */
.set V_NCHUNK,    0x360a          /* word */
.set V_CURLVL,    0x360c          /* byte */
.set V_TLEVEL,    0x360d          /* byte */
.set V_FSLVL,     0x360e          /* byte */
.set V_LOCTYPE,   0x360f          /* byte */
.set V_TROOT,     0x3610          /* qword */
.set V_FSROOT,    0x3618          /* qword */
.set V_ROOTDIR,   0x3620          /* qword */
.set V_DIRID,     0x3628          /* qword */
.set V_LOC,       0x3630          /* qword */
.set V_INO,       0x3638          /* qword */
.set V_NAME,      0x3640          /* word */
.set V_NAMELEN,   0x3642          /* word */
.set V_END,       0x3644          /* word */
.set V_EXTPTR,    0x3646          /* word */
.set V_EXTCNT,    0x3648          /* word */
.set V_SEG,       0x364a          /* word */
.set V_SEGLIM,    0x364c          /* word */
.set V_SUBVOL,    0x3650
.set V_SIZE,      0x3654
.set V_FOFF,      0x3658
.set V_DELTA,     0x365c
.set V_COV,       0x3660
.set V_MAXSIZE,   0x3664
.set V_LBA,       0x3668
.set V_LCNT,      0x366c
.set V_VSIZE,     0x3670
.set TKEY,        0x3680          /* 17 bytes */

.set CHUNKS,      0x3700          /* 128 x 24 */
.set EXTTAB0,     0x4300          /* RC95LDR.SYS: 64 x 8 + terminator */
.set EXTTAB1,     0x4510          /* VENEER.EXE */
.set CHILDREN,    0x4700
.set NODEBUF,     0x8000
.set DATA0,       NODEBUF + 0x65

.set LOAD_ADDR,   0x8000
.set LOAD_SEG,    0x0800
.set LOAD_LIMIT,  0x7000
.set LOAD_MAX,    0x67000
.set VEN_SEG,     0x8000
.set VEN_LIMIT,   0x9e00
.set VEN_MAX,     0x1d000

.global Rc95LdrBtrfsMbr
Rc95LdrBtrfsMbr:
Start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    cld
    mov bp, si                      /* DS:SI = MBR partition entry, if any */
    /* Read it now: the copy below overwrites the MBR's relocated table. */
    xor ebx, ebx
    lea cx, [bp - 0x7be]
    cmp cx, 0x30
    ja .Lnoentry
    test cl, 15
    jnz .Lnoentry
    cmp BYTE PTR [bp], 0x80
    jne .Lnoentry
    mov ebx, DWORD PTR [bp + 8]
.Lnoentry:
    mov si, 0x7c00
    mov di, BASE
    mov cx, 256
    rep movsw
    .byte 0xea                      /* jmp far 0000:Relocated */
    .word Relocated
    .word 0

Relocated:
    mov BYTE PTR [V_DRIVE], dl
    mov eax, DWORD PTR [PartitionLba]
    cmp eax, 0xffffffff
    jne .Lhavepart
    mov eax, ebx
.Lhavepart:
    mov DWORD PTR [V_PART], eax
    mov al, 'D'
    mov ah, 0x41
    mov bx, 0x55aa
    int 0x13
    jc Fail
    cmp bx, 0xaa55
    jne Fail
    mov eax, DWORD PTR [V_PART]
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

.org 0x1b0
PartitionLba: .long 0xffffffff
.org 0x1fe
    .byte 0x55, 0xaa

/* ---------------------------- sectors 1..11 ---------------------------- */

Main:
    /* Superblock: partition + 64 KiB, 8 sectors. */
    mov eax, DWORD PTR [V_PART]
    add eax, 128
    mov cx, 8
    mov bx, SB
    call ReadAbs
    mov al, 'F'
    cmp DWORD PTR [SB + 0x40], 0x5248425f   /* "_BHR" */
    jne Fail
    cmp DWORD PTR [SB + 0x44], 0x4d5f5366   /* "fS_M" */
    jne Fail
    mov al, 'P'
    cmp DWORD PTR [SB + 0x88], 1            /* num_devices */
    jne Fail
    cmp DWORD PTR [SB + 0x8c], 0
    jne Fail
    mov al, 'F'
    mov eax, DWORD PTR [SB + 0x94]          /* nodesize */
    cmp eax, 4096
    jb Fail
    cmp eax, 16384
    ja Fail
    lea ecx, [eax - 1]
    test ecx, eax
    jnz Fail
    shr eax, 9
    mov WORD PTR [V_NSEC], ax
    mov al, 'F'
    cmp DWORD PTR [SB + 0xa0], 2048         /* sys_chunk_array_size */
    ja Fail

    /* 1. Bootstrap chunk map from the superblock's system chunk array. */
    mov WORD PTR [V_NCHUNK], 0
    mov si, SB + 0x32b
    mov bp, si
    add bp, WORD PTR [SB + 0xa0]
.Lsys:
    cmp si, bp
    jae .Lsysdone
    mov al, 'F'
    cmp BYTE PTR [si + 8], 0xe4             /* CHUNK_ITEM */
    jne Fail
    lea bx, [si + 17]
    call AddChunk
    movzx ax, BYTE PTR [bx + 44]            /* num_stripes (low byte) */
    shl ax, 5
    add ax, 48 + 17
    add si, ax
    jmp .Lsys
.Lsysdone:

    /* 2. Full chunk map from the chunk tree (height 0 or 1). */
    mov eax, DWORD PTR [SB + 0x58]
    mov edx, DWORD PTR [SB + 0x5c]
    call ReadNode
    mov al, 'T'
    mov cl, BYTE PTR [SB + 0xc7]
    cmp cl, BYTE PTR [NODEBUF + 0x64]
    jne Fail
    test cl, cl
    jz .Lleafroot
    mov al, 'L'
    cmp cl, 1
    ja Fail
    mov cx, WORD PTR [NODEBUF + 0x60]       /* children */
    mov si, NODEBUF + 0x65 + 17
    mov di, CHILDREN
.Lcopychild:
    mov eax, DWORD PTR [si]
    mov DWORD PTR [di], eax
    mov eax, DWORD PTR [si + 4]
    mov DWORD PTR [di + 4], eax
    add si, 33
    add di, 8
    dec cx
    jnz .Lcopychild
    mov WORD PTR [V_END], di
    mov si, CHILDREN
.Lchild:
    mov WORD PTR [V_EXTPTR], si
    mov eax, DWORD PTR [si]
    mov edx, DWORD PTR [si + 4]
    call ReadNode
    mov al, 'T'
    cmp BYTE PTR [NODEBUF + 0x64], 0
    jne Fail
    call ScanChunkLeaf
    mov si, WORD PTR [V_EXTPTR]
    add si, 8
    cmp si, WORD PTR [V_END]
    jb .Lchild
    jmp .Lchunksdone
.Lleafroot:
    call ScanChunkLeaf
.Lchunksdone:

    /* 3. Default subvolume from the root tree. */
    mov eax, DWORD PTR [SB + 0x50]
    mov DWORD PTR [V_TROOT], eax
    mov eax, DWORD PTR [SB + 0x54]
    mov DWORD PTR [V_TROOT + 4], eax
    mov al, BYTE PTR [SB + 0xc6]
    mov BYTE PTR [V_TLEVEL], al
    mov DWORD PTR [V_DIRID], 6              /* BTRFS_ROOT_TREE_DIR_OBJECTID */
    mov DWORD PTR [V_DIRID + 4], 0
    mov WORD PTR [V_NAME], OFFSET FLAT:NameDefault
    mov WORD PTR [V_NAMELEN], 7
    mov DWORD PTR [V_SUBVOL], 5             /* FS_TREE unless "default" says otherwise */
    call LookupName
    jc .Lgotsub
    cmp DWORD PTR [V_LOC + 4], 0
    jne .Lgotsub
    mov eax, DWORD PTR [V_LOC]
    mov DWORD PTR [V_SUBVOL], eax
.Lgotsub:
    mov eax, DWORD PTR [V_SUBVOL]
    mov DWORD PTR [TKEY], eax
    mov DWORD PTR [TKEY + 4], 0
    mov BYTE PTR [TKEY + 8], 132            /* ROOT_ITEM */
    mov DWORD PTR [TKEY + 9], 0xffffffff
    mov DWORD PTR [TKEY + 13], 0xffffffff
    call SearchTree
    mov al, 'N'
    jc Fail
    call KeyMatchOT
    jne Fail
    mov bx, WORD PTR [si + 17]
    add bx, DATA0
    mov eax, DWORD PTR [bx + 176]           /* bytenr */
    mov DWORD PTR [V_FSROOT], eax
    mov eax, DWORD PTR [bx + 180]
    mov DWORD PTR [V_FSROOT + 4], eax
    mov al, BYTE PTR [bx + 238]             /* level */
    mov BYTE PTR [V_FSLVL], al
    mov eax, DWORD PTR [bx + 168]           /* root_dirid */
    mov DWORD PTR [V_ROOTDIR], eax
    mov eax, DWORD PTR [bx + 172]
    mov DWORD PTR [V_ROOTDIR + 4], eax

    /* 4. Resolve both files into extent tables before anything is loaded. */
    mov WORD PTR [V_NAME], OFFSET FLAT:NameLdr
    mov WORD PTR [V_NAMELEN], 11
    mov WORD PTR [V_EXTPTR], EXTTAB0
    mov DWORD PTR [V_MAXSIZE], LOAD_MAX
    call ResolveFile
    mov WORD PTR [V_NAME], OFFSET FLAT:NameVeneer
    mov WORD PTR [V_NAMELEN], 10
    mov WORD PTR [V_EXTPTR], EXTTAB1
    mov DWORD PTR [V_MAXSIZE], VEN_MAX
    call ResolveFile
    mov eax, DWORD PTR [V_SIZE]
    mov DWORD PTR [V_VSIZE], eax

    /* 5. Load. VENEER first (high), then RC95LDR (overwrites the node buffer). */
    mov WORD PTR [V_SEG], VEN_SEG
    mov WORD PTR [V_SEGLIM], VEN_LIMIT
    mov si, EXTTAB1
    call LoadExtents
    mov WORD PTR [V_SEG], LOAD_SEG
    mov WORD PTR [V_SEGLIM], LOAD_LIMIT
    mov si, EXTTAB0
    call LoadExtents

    /* 6. Validate and run VENEER, then start RC95LDR. */
    mov al, 'G'
    mov ax, VEN_SEG
    mov es, ax
    cmp DWORD PTR es:[0], 0x31524e56        /* "VNR1" */
    jne .Lbadveneer
    mov ax, WORD PTR es:[4]
    mov ecx, DWORD PTR [V_VSIZE]
    cmp ecx, 0x10000
    jae .Lentryok
    cmp ax, cx
    jae .Lbadveneer
.Lentryok:
    mov WORD PTR [VeneerEntry], ax
    xor ax, ax
    mov es, ax
    mov dl, BYTE PTR [V_DRIVE]
    .byte 0xff, 0x1e                        /* call far m16:16 */
    .word VeneerEntry
    jc .Lveneerfail
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov dl, BYTE PTR [V_DRIVE]
    .byte 0xea                              /* jmp far 0000:LOAD_ADDR */
    .word LOAD_ADDR
    .word 0
.Lbadveneer:
    mov al, 'G'
    jmp Fail
.Lveneerfail:
    mov al, 'H'
    jmp Fail

/* ----------------------------- chunk mapping --------------------------- */

/* SI = disk key (CHUNK_ITEM), BX = chunk item. Appends one CHUNK entry. */
AddChunk:
    mov al, 'C'
    cmp WORD PTR [V_NCHUNK], 128
    jae Fail
    imul di, WORD PTR [V_NCHUNK], 24
    add di, CHUNKS
    mov eax, DWORD PTR [si + 9]             /* logical start = key offset */
    mov DWORD PTR [di], eax
    mov eax, DWORD PTR [si + 13]
    mov DWORD PTR [di + 4], eax
    mov eax, DWORD PTR [bx]                 /* length */
    mov DWORD PTR [di + 8], eax
    mov eax, DWORD PTR [bx + 4]
    mov DWORD PTR [di + 12], eax
    mov al, 'P'
    test DWORD PTR [bx + 24], 0x1c8         /* RAID0/10/5/6 */
    jnz Fail
    cmp WORD PTR [bx + 44], 0               /* num_stripes */
    je Fail
    mov eax, DWORD PTR [bx + 56]            /* stripe 0 physical byte offset */
    mov edx, DWORD PTR [bx + 60]
    test eax, 0x1ff
    jnz Fail
    shrd eax, edx, 9
    shr edx, 9
    test edx, edx
    jnz Fail
    mov DWORD PTR [di + 16], eax
    inc WORD PTR [V_NCHUNK]
    ret

/* NODEBUF holds a leaf of the chunk tree: add every CHUNK_ITEM in it. */
ScanChunkLeaf:
    mov cx, WORD PTR [NODEBUF + 0x60]
    test cx, cx
    jz .Lscl_done
    mov si, NODEBUF + 0x65
.Lscl:
    cmp BYTE PTR [si + 8], 0xe4
    jne .Lscl_next
    mov bx, WORD PTR [si + 17]
    add bx, DATA0
    push cx
    call AddChunk
    pop cx
.Lscl_next:
    add si, 25
    dec cx
    jnz .Lscl
.Lscl_done:
    ret

/* EDX:EAX = logical byte address -> EAX = partition-relative sector. CF = unmapped. */
MapLogical:
    mov si, CHUNKS
    mov cx, WORD PTR [V_NCHUNK]
    test cx, cx
    jz .Lml_none
.Lml:
    push eax
    push edx
    sub eax, DWORD PTR [si]
    sbb edx, DWORD PTR [si + 4]
    jc .Lml_no
    cmp edx, DWORD PTR [si + 12]
    ja .Lml_no
    jb .Lml_in
    cmp eax, DWORD PTR [si + 8]
    jae .Lml_no
.Lml_in:
    shrd eax, edx, 9
    shr edx, 9
    add sp, 8
    test edx, edx
    jnz .Lml_none
    add eax, DWORD PTR [si + 16]
    clc
    ret
.Lml_no:
    pop edx
    pop eax
    add si, 24
    dec cx
    jnz .Lml
.Lml_none:
    stc
    ret

FailMap:
    mov al, 'M'
    jmp Fail
FailR:
    mov al, 'R'
    jmp Fail
FailZ:
    mov al, 'Z'
    jmp Fail

/* EDX:EAX = logical node address -> NODEBUF. */
ReadNode:
    call MapLogical
    jc FailMap
    add eax, DWORD PTR [V_PART]
    mov cx, WORD PTR [V_NSEC]
    mov bx, NODEBUF
    jmp ReadAbs

/* ------------------------------ tree search ---------------------------- */

/* SI = key in node, DI = target key. Flags as CMP (key vs target). */
CmpKey:
    mov eax, DWORD PTR [si + 4]
    cmp eax, DWORD PTR [di + 4]
    jne .Lck
    mov eax, DWORD PTR [si]
    cmp eax, DWORD PTR [di]
    jne .Lck
    mov al, BYTE PTR [si + 8]
    cmp al, BYTE PTR [di + 8]
    jne .Lck
    mov eax, DWORD PTR [si + 13]
    cmp eax, DWORD PTR [di + 13]
    jne .Lck
    mov eax, DWORD PTR [si + 9]
    cmp eax, DWORD PTR [di + 9]
.Lck:
    ret

/* ZF set if the item key at SI has TKEY's objectid and type. */
KeyMatchOT:
    mov eax, DWORD PTR [si]
    cmp eax, DWORD PTR [TKEY]
    jne .Lkm
    mov eax, DWORD PTR [si + 4]
    cmp eax, DWORD PTR [TKEY + 4]
    jne .Lkm
    mov al, BYTE PTR [si + 8]
    cmp al, BYTE PTR [TKEY + 8]
.Lkm:
    ret

/* Tree = V_TROOT/V_TLEVEL, target = TKEY. Out: SI = last item with key <= target
 * (leaf in NODEBUF), CF set if none. */
SearchTree:
    mov eax, DWORD PTR [V_TROOT]
    mov edx, DWORD PTR [V_TROOT + 4]
    mov cl, BYTE PTR [V_TLEVEL]
    mov BYTE PTR [V_CURLVL], cl
.Lst:
    call ReadNode
    mov al, 'T'
    mov cl, BYTE PTR [V_CURLVL]
    cmp cl, BYTE PTR [NODEBUF + 0x64]
    jne Fail
    mov cx, WORD PTR [NODEBUF + 0x60]       /* nritems */
    mov di, TKEY
    mov si, NODEBUF + 0x65
    cmp BYTE PTR [V_CURLVL], 0
    je .Lleaf
    test cx, cx
    jz Fail
    mov bx, si
.Lkp:
    call CmpKey
    ja .Ldescend
    mov bx, si
    add si, 33
    dec cx
    jnz .Lkp
.Ldescend:
    mov eax, DWORD PTR [bx + 17]
    mov edx, DWORD PTR [bx + 21]
    dec BYTE PTR [V_CURLVL]
    jmp .Lst
.Lleaf:
    xor bx, bx
    test cx, cx
    jz .Lleafend
.Lit:
    call CmpKey
    ja .Lleafend
    mov bx, si
    add si, 25
    dec cx
    jnz .Lit
.Lleafend:
    mov si, bx
    test bx, bx
    jz .Lnone
    clc
    ret
.Lnone:
    stc
    ret

/* SI = bytes, CX = length -> EAX = Btrfs name hash (crc32c, seed ~1, no final xor). */
Crc32c:
    mov eax, 0xfffffffe
    test cx, cx
    jz .Lcrcend
.Lcrcb:
    xor al, BYTE PTR [si]
    inc si
    mov dl, 8
.Lcrcbit:
    shr eax, 1
    jnc .Lcrcnx
    xor eax, 0x82f63b78
.Lcrcnx:
    dec dl
    jnz .Lcrcbit
    dec cx
    jnz .Lcrcb
.Lcrcend:
    ret

/* Directory V_DIRID in the tree V_TROOT/V_TLEVEL; name V_NAME/V_NAMELEN.
 * Out: CF clear, V_LOC = location key objectid, V_LOCTYPE = its type. */
LookupName:
    mov si, WORD PTR [V_NAME]
    mov cx, WORD PTR [V_NAMELEN]
    call Crc32c
    mov DWORD PTR [TKEY + 9], eax
    mov DWORD PTR [TKEY + 13], 0
    mov eax, DWORD PTR [V_DIRID]
    mov DWORD PTR [TKEY], eax
    mov eax, DWORD PTR [V_DIRID + 4]
    mov DWORD PTR [TKEY + 4], eax
    mov BYTE PTR [TKEY + 8], 84             /* DIR_ITEM */
    call SearchTree
    jc .Lln_nf
    call KeyMatchOT
    jne .Lln_nf
    mov eax, DWORD PTR [si + 9]
    cmp eax, DWORD PTR [TKEY + 9]
    jne .Lln_nf
    mov bx, WORD PTR [si + 17]
    add bx, DATA0
    mov ax, WORD PTR [si + 21]
    add ax, bx
    mov WORD PTR [V_END], ax
.Lln_item:
    cmp bx, WORD PTR [V_END]
    jae .Lln_nf
    mov cx, WORD PTR [bx + 27]              /* name_len */
    cmp cx, WORD PTR [V_NAMELEN]
    jne .Lln_next
    lea si, [bx + 30]
    mov di, WORD PTR [V_NAME]
    repe cmpsb
    je .Lln_hit
.Lln_next:
    mov ax, WORD PTR [bx + 27]
    add ax, WORD PTR [bx + 25]              /* + data_len */
    add ax, 30
    add bx, ax
    jmp .Lln_item
.Lln_hit:
    mov eax, DWORD PTR [bx]
    mov DWORD PTR [V_LOC], eax
    mov eax, DWORD PTR [bx + 4]
    mov DWORD PTR [V_LOC + 4], eax
    mov al, BYTE PTR [bx + 8]
    mov BYTE PTR [V_LOCTYPE], al
    clc
    ret
.Lln_nf:
    stc
    ret

/* Resolve V_NAME in the subvolume root directory into the extent table at
 * V_EXTPTR (entries: LBA, sector count; terminated by a zero count). */
ResolveFile:
    mov eax, DWORD PTR [V_FSROOT]
    mov DWORD PTR [V_TROOT], eax
    mov eax, DWORD PTR [V_FSROOT + 4]
    mov DWORD PTR [V_TROOT + 4], eax
    mov al, BYTE PTR [V_FSLVL]
    mov BYTE PTR [V_TLEVEL], al
    mov eax, DWORD PTR [V_ROOTDIR]
    mov DWORD PTR [V_DIRID], eax
    mov eax, DWORD PTR [V_ROOTDIR + 4]
    mov DWORD PTR [V_DIRID + 4], eax
    call LookupName
    mov al, 'N'
    jc Fail
    cmp BYTE PTR [V_LOCTYPE], 1             /* INODE_ITEM */
    jne Fail
    mov eax, DWORD PTR [V_LOC]
    mov DWORD PTR [V_INO], eax
    mov eax, DWORD PTR [V_LOC + 4]
    mov DWORD PTR [V_INO + 4], eax

    mov eax, DWORD PTR [V_INO]              /* INODE_ITEM (ino, 1, 0) */
    mov DWORD PTR [TKEY], eax
    mov eax, DWORD PTR [V_INO + 4]
    mov DWORD PTR [TKEY + 4], eax
    mov BYTE PTR [TKEY + 8], 1
    mov DWORD PTR [TKEY + 9], 0
    mov DWORD PTR [TKEY + 13], 0
    call SearchTree
    mov al, 'N'
    jc Fail
    call KeyMatchOT
    jne Fail
    mov bx, WORD PTR [si + 17]
    add bx, DATA0
    test BYTE PTR [bx + 53], 0x80           /* S_IFREG = 0100000 (mode bit 15) */
    jz Fail
    mov al, 'B'
    cmp DWORD PTR [bx + 20], 0
    jne Fail
    mov eax, DWORD PTR [bx + 16]            /* size */
    test eax, eax
    jz Fail
    cmp eax, DWORD PTR [V_MAXSIZE]
    ja Fail
    mov DWORD PTR [V_SIZE], eax

    mov DWORD PTR [V_FOFF], 0
    mov WORD PTR [V_EXTCNT], 0
.Lextent:
    mov eax, DWORD PTR [V_INO]
    mov DWORD PTR [TKEY], eax
    mov eax, DWORD PTR [V_INO + 4]
    mov DWORD PTR [TKEY + 4], eax
    mov BYTE PTR [TKEY + 8], 108            /* EXTENT_DATA */
    mov eax, DWORD PTR [V_FOFF]
    mov DWORD PTR [TKEY + 9], eax
    mov DWORD PTR [TKEY + 13], 0
    call SearchTree
    jc FailR
    call KeyMatchOT
    jne FailR
    cmp DWORD PTR [si + 13], 0
    jne FailR
    mov eax, DWORD PTR [V_FOFF]
    sub eax, DWORD PTR [si + 9]             /* delta into this extent */
    mov DWORD PTR [V_DELTA], eax
    mov bx, WORD PTR [si + 17]
    add bx, DATA0
    cmp BYTE PTR [bx + 20], 1               /* regular extent only */
    jne FailR
    cmp BYTE PTR [bx + 16], 0               /* compression */
    jne FailR
    cmp BYTE PTR [bx + 17], 0               /* encryption */
    jne FailR
    cmp WORD PTR [bx + 18], 0               /* other encoding */
    jne FailR
    cmp DWORD PTR [bx + 49], 0              /* num_bytes high */
    jne FailR
    mov ecx, DWORD PTR [bx + 45]
    sub ecx, eax
    jbe FailR
    mov DWORD PTR [V_COV], ecx
    mov eax, DWORD PTR [bx + 21]            /* disk_bytenr */
    mov edx, DWORD PTR [bx + 25]
    mov ecx, eax
    or ecx, edx
    jz FailR                                 /* hole */
    add eax, DWORD PTR [bx + 37]            /* + extent offset */
    adc edx, DWORD PTR [bx + 41]
    add eax, DWORD PTR [V_DELTA]
    adc edx, 0
    test eax, 0x1ff
    jnz FailR
    call MapLogical
    jc FailMap
    add eax, DWORD PTR [V_PART]
    mov ecx, DWORD PTR [V_COV]
    add ecx, 511
    shr ecx, 9
    cmp WORD PTR [V_EXTCNT], 64
    jae FailZ
    mov di, WORD PTR [V_EXTPTR]
    mov DWORD PTR [di], eax
    mov DWORD PTR [di + 4], ecx
    add di, 8
    mov WORD PTR [V_EXTPTR], di
    inc WORD PTR [V_EXTCNT]
    mov eax, DWORD PTR [V_COV]
    add DWORD PTR [V_FOFF], eax
    mov eax, DWORD PTR [V_FOFF]
    cmp eax, DWORD PTR [V_SIZE]
    jb .Lextent
    mov di, WORD PTR [V_EXTPTR]
    mov DWORD PTR [di + 4], 0               /* terminator */
    ret

/* SI = extent table, V_SEG/V_SEGLIM = destination. */
LoadExtents:
.Lle_ent:
    mov ecx, DWORD PTR [si + 4]
    test ecx, ecx
    jz .Lle_done
    mov eax, DWORD PTR [si]
    mov DWORD PTR [V_LBA], eax
    mov DWORD PTR [V_LCNT], ecx
.Lle_chunk:
    mov ecx, DWORD PTR [V_LCNT]
    cmp ecx, 32                             /* 16 KiB per request */
    jbe .Lle_n
    mov ecx, 32
.Lle_n:
    mov ax, WORD PTR [V_SEG]
    mov es, ax
    xor bx, bx
    mov eax, DWORD PTR [V_LBA]
    call ReadAbs
    movzx ecx, cx
    add DWORD PTR [V_LBA], ecx
    sub DWORD PTR [V_LCNT], ecx
    shl cx, 5                               /* sectors -> paragraphs */
    add WORD PTR [V_SEG], cx
    mov ax, WORD PTR [V_SEG]
    mov cl, 'B'
    cmp ax, WORD PTR [V_SEGLIM]
    ja .Lle_big
    cmp DWORD PTR [V_LCNT], 0
    jne .Lle_chunk
    add si, 8
    jmp .Lle_ent
.Lle_big:
    mov al, 'B'
    jmp Fail
.Lle_done:
    xor ax, ax
    mov es, ax
    ret

NameDefault: .ascii "default"
NameLdr:     .ascii "RC95LDR.SYS"
NameVeneer:  .ascii "VENEER.EXE"
VeneerEntry: .word 0, VEN_SEG

.org IMAGE_SECTORS * 512
.section .note.GNU-stack,"",@progbits

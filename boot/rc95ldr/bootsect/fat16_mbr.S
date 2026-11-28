/*
 * PROJECT:    RC95-OS RC95LDR BIOS boot sector
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    FAT16 volume boot record: load /RC95LDR.SYS and start it
 *
 * The formatter owns bytes 0x03..0x59 (OEM/BPB/EBPB). The installer must
 * preserve those bytes and write the remaining boot code/signature.
 * Contract: 512-byte sectors, INT 13h EDD, FAT16 fixed root directory and
 * 16-bit FAT entries. Loads the file at physical 0x8000 and enters at 0000:8000.
 * DL is preserved as the BIOS drive. Invalid geometry, read failures,
 * missing loader, and invalid cluster chains halt rather than chain-loading.
 */
.intel_syntax noprefix
.section .text,"ax",@progbits
.code16
.set B,0x7c00
.set ROOT,0x7e00
.set VAR,0x7a00
.set DRIVE,VAR
.set FAT,VAR+4
.set DATA,VAR+8
.set CUR,VAR+12
.set LEFT,VAR+16
.global Rc95LdrFat16Mbr
Rc95LdrFat16Mbr:
    .byte 0xeb,0x58,0x90
    .fill 0x5a-3,1,0
Start:
    xor ax,ax
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0x7000
    cld
    mov [DRIVE],dl
    cmp word ptr [B+0x0b],512
    jne Fail
    mov ax,word ptr [B+0x16]
    or ax,word ptr [B+0x11]
    jz Fail
    cmp byte ptr [B+0x0d],0
    je Fail
    mov ah,0x41
    mov bx,0x55aa
    int 0x13
    jc Fail
    movzx eax,word ptr [B+0x0e]
    add eax,dword ptr [B+0x1c]
    mov [FAT],eax
    movzx ecx,byte ptr [B+0x10]
    imul ecx,dword ptr [B+0x16]
    add eax,ecx
    movzx ecx,word ptr [B+0x11]
    shl ecx,5
    add ecx,511
    shr ecx,9
    mov [LEFT],ecx
    mov [CUR],eax
    add eax,ecx
    mov [DATA],eax
    /* Search the fixed-size FAT16 root directory. */
.RootSector:
    mov eax,[CUR]
    mov bx,ROOT
    call ReadSector
    mov si,ROOT
    mov bp,16
.Entry:
    cmp byte ptr [si],0
    je NotFound
    cmp byte ptr [si],0xe5
    je .Next
    test byte ptr [si+11],0x18
    jnz .Next
    push si
    mov di,offset FileName
    mov cx,11
    repe cmpsb
    pop si
    je .Found
.Next:
    add si,32
    dec bp
    jnz .Entry
    inc dword ptr [CUR]
    dec dword ptr [LEFT]
    jnz .RootSector
    jmp NotFound
.Found:
    mov eax,dword ptr [si+28]
    test eax,eax
    jz Bad
    cmp eax,0x97000
    ja Bad
    add eax,511
    shr eax,9
    mov [LEFT],eax
    mov ax,word ptr [si+26]
    cmp ax,2
    jb Bad
    mov [CUR],eax
    mov ax,0x0800
    mov es,ax
.Load:
    mov eax,[CUR]
    sub eax,2
    movzx ecx,byte ptr [B+0x0d]
    mul ecx
    add eax,[DATA]
    movzx cx,byte ptr [B+0x0d]
.ClusterSector:
    push cx
    push eax
    xor bx,bx
    call ReadSector
    mov ax,es
    add ax,0x20
    mov es,ax
    pop eax
    pop cx
    inc eax
    dec dword ptr [LEFT]
    jz .Done
    loop .ClusterSector
    /* FAT16 has 256 16-bit entries per FAT sector. */
    mov eax,[CUR]
    shr eax,8
    add eax,[FAT]
    push es
    xor dx,dx
    mov es,dx
    mov bx,ROOT
    call ReadSector
    pop es
    movzx bx,word ptr [CUR]
    and bx,255
    shl bx,1
    mov ax,word ptr [ROOT+bx]
    cmp ax,0xfff0
    jae Bad
    cmp ax,2
    jb Bad
    mov [CUR],ax
    jmp .Load
.Done:
    xor ax,ax
    mov es,ax
    mov dl,[DRIVE]
    .byte 0xea
    .word 0x8000
    .word 0
/* EAX = absolute LBA, ES:BX = 512-byte destination. */
ReadSector:
    push 0
    push 0
    push eax
    push es
    push bx
    push 1
    push 0x10
    mov si,sp
    mov dl,[DRIVE]
    mov ah,0x42
    int 0x13
    lea sp,[si+16]
    jc Fail
    ret
NotFound:
Bad:
Fail:
.Halt:
    cli
    hlt
    jmp .Halt
FileName: .ascii "RC95LDR SYS"
.org 0x1fe
.byte 0x55,0xaa
.section .note.GNU-stack,"",@progbits

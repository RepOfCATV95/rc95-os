/* GNU as implementation of the i386 native BIOS entry bridge. */
.intel_syntax noprefix
.code32
#ifdef _WIN32
#define CNAME(x) _##x
#else
#define CNAME(x) x
#endif
.equ RC95LDR_BIOS_ENTRY_MAGIC, 0x52353942

.extern CNAME(Rc95LdrBiosEntryMain)
.global CNAME(Rc95LdrBiosI386Entry)

CNAME(Rc95LdrBiosI386Entry):
    cli
    cld
    cmp eax, RC95LDR_BIOS_ENTRY_MAGIC
    jne .Lbad_protocol
    mov edx, cr0
    test edx, 1
    jz .Lbad_environment
    test edx, 0x08000000
    jnz .Lbad_environment
    mov dx, cs
    cmp dx, 0x08
    jne .Lbad_environment
    test ebx, ebx
    jz .Lbad_protocol
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov DWORD PTR [CNAME(Rc95LdrBiosEntrySavedStack)], esp
    lea esp, [CNAME(Rc95LdrBiosEntryStackTop)]
    and esp, 0xfffffff0
    push ebx
    call CNAME(Rc95LdrBiosEntryMain)
    add esp, 4
    mov esp, DWORD PTR [CNAME(Rc95LdrBiosEntrySavedStack)]
    ret
.Lbad_protocol:
    mov eax, 1
    ret
.Lbad_environment:
    mov eax, 3
    ret


.global CNAME(Rc95LdrBiosInvokeNativeEntry)

CNAME(Rc95LdrBiosInvokeNativeEntry):
    push ebx
    mov ebx, DWORD PTR [esp + 8]
    mov eax, RC95LDR_BIOS_ENTRY_MAGIC
    call CNAME(Rc95LdrBiosI386Entry)
    pop ebx
    ret


.section .data
.balign 4
CNAME(Rc95LdrBiosEntrySavedStack):
    .long 0
.section .bss
.balign 16
CNAME(Rc95LdrBiosEntryStack):
    .skip 16384
CNAME(Rc95LdrBiosEntryStackTop):

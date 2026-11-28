/* GNU as implementation of the i386 BIOS module call bridge. */
.intel_syntax noprefix
.code32
#ifdef _WIN32
#define CNAME(x) _##x
#else
#define CNAME(x) x
#endif
.global CNAME(Rc95LdrBiosCallModuleEntry)

CNAME(Rc95LdrBiosCallModuleEntry):
    mov eax, DWORD PTR [esp + 4]
    mov edx, DWORD PTR [esp + 8]
    test eax, eax
    jz .Lbad_entry
    push edx
    call eax
    add esp, 4
    ret
.Lbad_entry:
    mov eax, 1
    ret


#include "rc95ldr_ntfs_record.h"
#include <stdio.h>
#include <string.h>

static void p16(uint8_t *p, uint16_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void p32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void p64(uint8_t *p, uint64_t v)
{ unsigned i; for(i=0;i<8;i++) p[i]=(uint8_t)(v>>(i*8)); }

int main(void)
{
    uint8_t record[1024];
    RC95LDR_NTFS_FILE_RECORD_INFO fi;
    RC95LDR_NTFS_ATTRIBUTE_INFO ai;
    uint32_t next;
    memset(record, 0, sizeof(record));
    memcpy(record, "FILE", 4);
    p16(record+4, 48); p16(record+6, 3);
    p16(record+16, 7); p16(record+20, 56);
    p16(record+22, 1); p32(record+24, 96); p32(record+28, 1024);
    p16(record+40, 2);
    p16(record+48, 0xa55a);
    p16(record+50, 0x1111); p16(record+52, 0x2222);
    p16(record+510, 0xa55a); p16(record+1022, 0xa55a);

    /* Resident unnamed attribute with an eight-byte value. */
    p32(record+56, 0x10); p32(record+60, 32);
    record[64]=0; record[65]=0;
    p16(record+66, 0); p16(record+70, 1);
    p32(record+72, 8); p16(record+76, 24);
    p32(record+88, 0xffffffffu);

    if (Rc95LdrNtfsFixupFileRecord(record, sizeof(record), 512, &fi) != Rc95LdrSuccess)
        return 1;
    if (fi.SequenceNumber != 7 || fi.Flags != 1 ||
        fi.FirstAttributeOffset != 56 || fi.BytesInUse != 96 ||
        fi.NextAttributeId != 2 ||
        Rc95LdrReadU16(record+510) != 0x1111 ||
        Rc95LdrReadU16(record+1022) != 0x2222)
        return 1;
    if (Rc95LdrNtfsGetAttribute(record, sizeof(record), 56, &ai, &next) != Rc95LdrSuccess ||
        ai.Type != 0x10 || ai.NonResident || ai.ValueLength != 8 ||
        ai.ValueOffset != 24 || next != 88)
        return 1;
    if (Rc95LdrNtfsGetAttribute(record, sizeof(record), 88, &ai, &next) != Rc95LdrNotFound)
        return 1;

    /* A bad sector trailer must be rejected without partially applying fixups. */
    memset(record, 0, sizeof(record));
    memcpy(record, "FILE", 4);
    p16(record+4, 48); p16(record+6, 3);
    p16(record+20, 56); p32(record+24, 96); p32(record+28, 1024);
    p16(record+48, 0xa55a); p16(record+510, 0xa55a);
    p16(record+1022, 0xdead);
    if (Rc95LdrNtfsFixupFileRecord(record, sizeof(record), 512, &fi) != Rc95LdrCorruptData ||
        Rc95LdrReadU16(record+510) != 0xa55a)
        return 1;

    /* Malformed attribute lengths and resident value bounds fail closed. */
    memset(record, 0, sizeof(record));
    memcpy(record, "FILE", 4); p32(record+24, 128);
    p32(record+64, 0x80); p32(record+68, 16);
    if (Rc95LdrNtfsGetAttribute(record, sizeof(record), 64, &ai, &next) != Rc95LdrCorruptData)
        return 1;
    p32(record+68, 24); p32(record+80, 32); p16(record+84, 24);
    if (Rc95LdrNtfsGetAttribute(record, sizeof(record), 64, &ai, &next) != Rc95LdrCorruptData)
        return 1;

    /* A non-resident attribute must carry a bounded mapping-pairs offset. */
    memset(record, 0, sizeof(record));
    memcpy(record, "FILE", 4); p32(record+24, 128);
    p32(record+64, 0x80); p32(record+68, 64);
    record[72]=1; p64(record+80, 0); p64(record+88, 3);
    p16(record+96, 64);
    if (Rc95LdrNtfsGetAttribute(record, sizeof(record), 64, &ai, &next) != Rc95LdrCorruptData)
        return 1;

    /* Attribute-list entries are length- and name-bounded. */
    {
        uint8_t list[40];
        RC95LDR_NTFS_ATTRIBUTE_LIST_ENTRY le;
        memset(list, 0, sizeof(list));
        p32(list, 0x80); p16(list+4, 32);
        list[6]=2; list[7]=26;
        p64(list+8, 4); p64(list+16, UINT64_C(0x0007000000001234));
        p16(list+24, 9); list[26]='D'; list[27]=0; list[28]='A'; list[29]=0;
        if (Rc95LdrNtfsGetAttributeListEntry(list, sizeof(list), 0, &le, &next) != Rc95LdrSuccess ||
            le.Type != 0x80 || le.Length != 32 || le.NameLength != 2 ||
            le.LowestVcn != 4 || le.FileReference != UINT64_C(0x0007000000001234) ||
            le.Instance != 9 || next != 32)
            return 1;
        list[7]=31;
        if (Rc95LdrNtfsGetAttributeListEntry(list, sizeof(list), 0, &le, &next) != Rc95LdrCorruptData)
            return 1;
        list[7]=26; p16(list+4, 24);
        if (Rc95LdrNtfsGetAttributeListEntry(list, sizeof(list), 0, &le, &next) != Rc95LdrCorruptData)
            return 1;
    }

    /* Attribute-list references must identify the exact extension record
       and the exact attribute instance (including its UTF-16 name). */
    {
        uint8_t list[32], extension[1024];
        RC95LDR_NTFS_FILE_RECORD_INFO ei;
        memset(list, 0, sizeof(list));
        p32(list, 0x80); p16(list+4, 32);
        p64(list+8, 0);
        p64(list+16, UINT64_C(0x0007000000001234));
        p16(list+24, 5);

        memset(extension, 0, sizeof(extension));
        memcpy(extension, "FILE", 4);
        p16(extension+4, 48); p16(extension+6, 3);
        p16(extension+16, 7); p16(extension+20, 56);
        p64(extension+32, UINT64_C(0x0003000000000042));
        p32(extension+44, 0x1234);
        p32(extension+24, 96); p32(extension+28, 1024);
        p16(extension+48, 0xa55a); p16(extension+50, 0x1111); p16(extension+52, 0x2222);
        p16(extension+510, 0xa55a); p16(extension+1022, 0xa55a);
        p32(extension+56, 0x80); p32(extension+60, 40);
        extension[64]=0; extension[65]=0;
        p16(extension+66, 0); p16(extension+70, 5);
        p32(extension+72, 8); p16(extension+76, 24);
        if (Rc95LdrNtfsFixupFileRecord(extension, sizeof(extension), 512, &ei) != Rc95LdrSuccess)
            return 1;
        if (Rc95LdrNtfsValidateAttributeListTarget(
                list, sizeof(list), 0, UINT64_C(0x0003000000000042),
                extension, sizeof(extension), &ei, 56) != Rc95LdrSuccess)
            return 1;
        if (Rc95LdrNtfsValidateAttributeListTarget(
                list, sizeof(list), 0, UINT64_C(0x0003000000000043),
                extension, sizeof(extension), &ei, 56) != Rc95LdrCorruptData)
            return 1;
        p16(extension+70, 6);
        if (Rc95LdrNtfsValidateAttributeListTarget(
                list, sizeof(list), 0, UINT64_C(0x0003000000000042),
                extension, sizeof(extension), &ei, 56) != Rc95LdrNotFound)
            return 1;
    }

    puts("RC95LDR NTFS record tests passed");
    return 0;
}

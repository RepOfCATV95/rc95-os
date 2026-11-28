#include "rc95bcdwalk.h"
#include <string.h>


typedef struct _RC95BCD_CANDIDATE_BUILD {
    RC95LDR_BCD_BOOT_CANDIDATE *Candidate;
} RC95BCD_CANDIDATE_BUILD;
static RC95LDR_STATUS
Rc95BcdStoreCandidateElement(uint32_t Id,const RC95BCD_VALUE *Value,void *Context)
{
    RC95BCD_CANDIDATE_BUILD *Build=(RC95BCD_CANDIDATE_BUILD*)Context;
    RC95LDR_BCD_BOOT_CANDIDATE *c;
    if(!Build||!Build->Candidate||!Value||(!Value->Data&&Value->Length))
        return Rc95LdrInvalidParameter;
    c = Build->Candidate;
    RC95LDR_BCD_CANDIDATE_ELEMENT *e;
    RC95BCD_ELEMENT_VIEW v;
    uint32_t offset;

    if (c->ElementCount > RC95LDR_MAX_BCD_ELEMENTS ||
        c->ElementPayloadBytes > RC95LDR_MAX_BCD_PAYLOAD_BYTES ||
        c->ElementStringCharacters > RC95LDR_MAX_BCD_STRING_CHARS)
        return Rc95LdrCorruptData;

    {
        uint32_t i;
        for (i = 0; i < c->ElementCount; i++)
            if (c->Elements[i].ElementId == Id)
                return Rc95LdrCorruptData;
    }

    if (c->ElementCount >= RC95LDR_MAX_BCD_ELEMENTS ||
        Value->Length > RC95LDR_MAX_BCD_PAYLOAD_BYTES - c->ElementPayloadBytes)
        return Rc95LdrBufferTooSmall;
    e=&c->Elements[c->ElementCount]; memset(e,0,sizeof(*e));
    e->ElementId=Id; e->EncodedType=(Id>>24)&0x0fu; e->RegistryValueType=Value->Type; e->Kind=Rc95BcdElementUnknown;
    e->PayloadOffset=c->ElementPayloadBytes; e->PayloadLength=Value->Length;
    if(Value->Length) memcpy(c->ElementPayloadStorage+c->ElementPayloadBytes,Value->Data,Value->Length);
    c->ElementPayloadBytes+=Value->Length;
    { RC95LDR_STATUS DecodeStatus=Rc95BcdDecodeElement(Id,Value,&v);
      if(DecodeStatus==Rc95LdrSuccess){
        e->Kind=v.Kind; e->IntegerValue=v.IntegerValue; e->BooleanValue=v.BooleanValue;
        e->ObjectGuid=v.ObjectGuid; e->ItemCount=v.ItemCount;
        e->DeviceType=v.DeviceType; e->AdditionalOptions=v.AdditionalOptions;
        if(v.Kind==Rc95BcdElementString){
            uint32_t i,n=v.StringCharacters;
            if(c->ElementStringCharacters>=RC95LDR_MAX_BCD_STRING_CHARS || n>=RC95LDR_MAX_BCD_STRING_CHARS-c->ElementStringCharacters) return Rc95LdrBufferTooSmall;
            offset=c->ElementStringCharacters; e->StringOffset=offset; e->StringCharacters=n;
            for(i=0;i<n;i++) c->ElementStringStorage[offset+i]=Rc95LdrReadU16(v.StringData+i*2);
            c->ElementStringStorage[offset+n]=0; c->ElementStringCharacters+=n+1;
            if(Id==RC95BCD_LIBRARY_LOAD_OPTIONS_STRING){
                if(n>=RC95LDR_MAX_BCD_STRING_CHARS) return Rc95LdrBufferTooSmall;
                for(i=0;i<n;i++) c->BootOptionsStorage[i]=c->ElementStringStorage[offset+i];
                c->BootOptionsStorage[n]=0;
                c->BootOptionsCharacters=n;
            }
        }
      } else if(DecodeStatus!=Rc95LdrUnsupported) return DecodeStatus;
    }
    c->ElementCount++;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95BcdRebaseCandidateDeviceView(
    RC95LDR_BCD_BOOT_CANDIDATE *Candidate,
    uint32_t ElementId,
    RC95BCD_ELEMENT_VIEW *View)
{
    uint32_t i;
    if(!Candidate||!View)return Rc95LdrInvalidParameter;
    for(i=0;i<Candidate->ElementCount;i++){
        const RC95LDR_BCD_CANDIDATE_ELEMENT *Element=&Candidate->Elements[i];
        if(Element->ElementId!=ElementId)continue;
        if(Element->Kind!=Rc95BcdElementDevice ||
           Element->PayloadLength<8u ||
           Element->PayloadOffset>Candidate->ElementPayloadBytes ||
           Element->PayloadLength>Candidate->ElementPayloadBytes-Element->PayloadOffset)
            return Rc95LdrCorruptData;
        View->Data=Candidate->ElementPayloadStorage+Element->PayloadOffset;
        View->Length=Element->PayloadLength;
        View->DeviceData=View->Data+8;
        View->DeviceDataLength=Element->PayloadLength-8u;
        return Rc95LdrSuccess;
    }
    return Rc95LdrNotFound;
}

static void
Rc95BcdGuidToString(const RC95BCD_GUID *Guid, char Text[39])
{
    static const char Hex[]="0123456789abcdef";
    uint8_t Bytes[16];
    unsigned i, p=0;
    Bytes[0]=(uint8_t)(Guid->Data1>>24); Bytes[1]=(uint8_t)(Guid->Data1>>16);
    Bytes[2]=(uint8_t)(Guid->Data1>>8); Bytes[3]=(uint8_t)Guid->Data1;
    Bytes[4]=(uint8_t)(Guid->Data2>>8); Bytes[5]=(uint8_t)Guid->Data2;
    Bytes[6]=(uint8_t)(Guid->Data3>>8); Bytes[7]=(uint8_t)Guid->Data3;
    memcpy(Bytes+8,Guid->Data4,8);
    Text[p++]='{';
    for(i=0;i<16;i++){
        if(i==4||i==6||i==8||i==10) Text[p++]='-';
        Text[p++]=Hex[Bytes[i]>>4]; Text[p++]=Hex[Bytes[i]&15];
    }
    Text[p++]='}'; Text[p]=0;
}

static RC95LDR_STATUS
Rc95BcdGetElementView(const RC95BCD_HIVE *Hive, uint32_t ObjectCell,
                      uint32_t Id, RC95BCD_ELEMENT_VIEW *View)
{
    RC95BCD_VALUE Value;
    RC95LDR_STATUS Status=Rc95BcdFindElement(Hive,ObjectCell,Id,&Value);
    if(Status!=Rc95LdrSuccess) return Status;
    return Rc95BcdDecodeElement(Id,&Value,View);
}

RC95LDR_STATUS
Rc95BcdCollectWindowsBootCandidates(
    const RC95BCD_HIVE *Hive,
    RC95LDR_BCD_BOOT_CANDIDATE *Candidates,
    size_t CandidateCapacity,
    size_t *CandidateCount)
{
    uint32_t ManagerCell, ObjectsCell;
    RC95BCD_ELEMENT_VIEW DefaultView, OrderView;
    char DefaultText[39];
    size_t i, Count=0;
    RC95LDR_STATUS Status;

    if(!CandidateCount)
        return Rc95LdrInvalidParameter;
    /* Keep the output deterministic even when capacity is zero. */
    *CandidateCount=0;
    if(!Hive||!Candidates||!CandidateCapacity)
        return Rc95LdrInvalidParameter;
    Status=Rc95BcdFindObject(Hive,RC95BCD_BOOTMGR_GUID,&ManagerCell);
    if(Status!=Rc95LdrSuccess) return Status;
    DefaultText[0]=0;
    Status=Rc95BcdGetElementView(Hive,ManagerCell,
        RC95BCD_BOOTMGR_DEFAULT_ELEMENT,&DefaultView);
    if(Status==Rc95LdrSuccess) {
        if(DefaultView.Kind!=Rc95BcdElementObject) return Rc95LdrCorruptData;
        Rc95BcdGuidToString(&DefaultView.ObjectGuid,DefaultText);
    } else if(Status!=Rc95LdrNotFound) return Status;

    Status=Rc95BcdGetElementView(Hive,ManagerCell,
        RC95BCD_BOOTMGR_DISPLAY_ORDER_ELEMENT,&OrderView);
    if(Status!=Rc95LdrSuccess) return Status;
    if(OrderView.Kind!=Rc95BcdElementObjectList) return Rc95LdrCorruptData;

    Status=Rc95BcdFindSubKey(Hive,Hive->RootCell,"Objects",&ObjectsCell);
    if(Status!=Rc95LdrSuccess) return Status;

    for(i=0;i<OrderView.ItemCount;i++){
        RC95BCD_GUID Guid;
        char GuidText[39];
        size_t Prior;
        uint32_t ObjectCell;
        RC95LDR_BCD_BOOT_CANDIDATE *Candidate;
        RC95BCD_ELEMENT_VIEW Device, OsDevice, LoaderPath, SystemRoot;
        const uint8_t *Raw=OrderView.Items+i*16;

        /* DisplayOrder is an ordered set of object references. Duplicate
         * references are ambiguous menu entries; reject rather than silently
         * manufacture two candidates for the same BCD object. */
        for(Prior=0;Prior<i;Prior++)
            if(memcmp(OrderView.Items+Prior*16,Raw,16)==0)
                return Rc95LdrCorruptData;
        Status=Rc95BcdReadGuid(Raw,16,&Guid);
        if(Status!=Rc95LdrSuccess) return Status;
        Rc95BcdGuidToString(&Guid,GuidText);
        Status=Rc95BcdFindSubKey(Hive,ObjectsCell,GuidText,&ObjectCell);
        if(Status==Rc95LdrNotFound) continue;
        if(Status!=Rc95LdrSuccess) return Status;

        Status=Rc95BcdGetElementView(Hive,ObjectCell,
            RC95BCD_LIBRARY_APPLICATION_DEVICE,&Device);
        if(Status==Rc95LdrNotFound) continue;
        if(Status!=Rc95LdrSuccess) return Status;
        if(Device.Kind!=Rc95BcdElementDevice) return Rc95LdrCorruptData;
        Status=Rc95BcdGetElementView(Hive,ObjectCell,
            RC95BCD_LIBRARY_APPLICATION_PATH,&LoaderPath);
        if(Status==Rc95LdrNotFound) continue;
        if(Status!=Rc95LdrSuccess) return Status;
        if(LoaderPath.Kind!=Rc95BcdElementString) return Rc95LdrCorruptData;
        Status=Rc95BcdGetElementView(Hive,ObjectCell,
            RC95BCD_OSLOADER_OS_DEVICE,&OsDevice);
        if(Status==Rc95LdrNotFound) continue;
        if(Status!=Rc95LdrSuccess) return Status;
        if(OsDevice.Kind!=Rc95BcdElementDevice) return Rc95LdrCorruptData;
        Status=Rc95BcdGetElementView(Hive,ObjectCell,
            RC95BCD_OSLOADER_SYSTEM_ROOT,&SystemRoot);
        if(Status==Rc95LdrNotFound) continue;
        if(Status!=Rc95LdrSuccess) return Status;
        if(SystemRoot.Kind!=Rc95BcdElementString) return Rc95LdrCorruptData;

        if(SystemRoot.StringCharacters==0 ||
           SystemRoot.StringCharacters>=RC95LDR_MAX_SYSTEM_ROOT_CHARS ||
           LoaderPath.StringCharacters==0 ||
           LoaderPath.StringCharacters>=RC95LDR_MAX_SYSTEM_ROOT_CHARS)
            continue;
        if(Count>=CandidateCapacity) return Rc95LdrBufferTooSmall;

        /* Build directly in caller-owned storage. The candidate embeds several
         * bounded payload/string arrays and is too large for a boot stack
         * temporary. Keeping it in its final slot also avoids stale pointers
         * after a structure copy. Count is advanced only after full success. */
        Candidate=&Candidates[Count];
        memset(Candidate,0,sizeof(*Candidate));
        Candidate->ObjectId=Guid;
        Candidate->ObjectCell=ObjectCell;
        Candidate->DisplayOrder=(uint32_t)i;
        Candidate->IsDefault=(DefaultText[0]!=0 && strcmp(GuidText,DefaultText)==0);
        Candidate->IsEnabled=1;
        Candidate->IsWindowsLoader=1;
        Candidate->DeviceElementStorage=Device;
        Candidate->OsDeviceElementStorage=OsDevice;
        Candidate->DeviceElement=&Candidate->DeviceElementStorage;
        Candidate->OsDeviceElement=&Candidate->OsDeviceElementStorage;

        {
            RC95BCD_CANDIDATE_BUILD Build;
            Build.Candidate=Candidate;
            Status=Rc95BcdEnumerateElements(Hive,ObjectCell,
                Rc95BcdStoreCandidateElement,&Build);
            if(Status!=Rc95LdrSuccess) {
                memset(Candidate,0,sizeof(*Candidate));
                return Status;
            }
        }
        /* The selected object's Boolean is retained with the candidate so
         * later dispatch cannot accidentally consult another BCD object. */
        {
            uint32_t ElementIndex;
            for (ElementIndex=0; ElementIndex<Candidate->ElementCount; ++ElementIndex)
            {
                const RC95LDR_BCD_CANDIDATE_ELEMENT *Element =
                    &Candidate->Elements[ElementIndex];
                if (Element->ElementId == RC95BCD_OSLOADER_GRAPHICS_MODE_DISABLED)
                {
                    if (Element->Kind != Rc95BcdElementBoolean ||
                        Element->BooleanValue > 1u)
                    {
                        memset(Candidate,0,sizeof(*Candidate));
                        return Rc95LdrCorruptData;
                    }
                    Candidate->GraphicsModeDisabled = Element->BooleanValue;
                    break;
                }
            }
        }
        Status=Rc95BcdRebaseCandidateDeviceView(Candidate,
            RC95BCD_LIBRARY_APPLICATION_DEVICE,&Candidate->DeviceElementStorage);
        if(Status==Rc95LdrSuccess)
            Status=Rc95BcdRebaseCandidateDeviceView(Candidate,
                RC95BCD_OSLOADER_OS_DEVICE,&Candidate->OsDeviceElementStorage);
        if(Status!=Rc95LdrSuccess) {
            memset(Candidate,0,sizeof(*Candidate));
            return Status;
        }

        Candidate->LoaderPathCharacters=LoaderPath.StringCharacters;
        {
            uint32_t Ch;
            for(Ch=0;Ch<LoaderPath.StringCharacters;Ch++)
                Candidate->LoaderPathStorage[Ch]=
                    Rc95LdrReadU16(LoaderPath.StringData+Ch*2);
            Candidate->LoaderPathStorage[Ch]=0;
        }
        Candidate->LoaderPathUtf16=Candidate->LoaderPathStorage;
        Candidate->SystemRootCharacters=SystemRoot.StringCharacters;
        {
            uint32_t Ch;
            for(Ch=0;Ch<SystemRoot.StringCharacters;Ch++)
                Candidate->SystemRootStorage[Ch]=
                    Rc95LdrReadU16(SystemRoot.StringData+Ch*2);
            Candidate->SystemRootStorage[Ch]=0;
        }
        Candidate->SystemRootUtf16=Candidate->SystemRootStorage;
        ++Count;
    }
    *CandidateCount=Count;
    return Count?Rc95LdrSuccess:Rc95LdrNotFound;
}

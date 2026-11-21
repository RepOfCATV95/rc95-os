#include "rc95bcdwalk.h"
#include <string.h>

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

    if(!Hive||!Candidates||!CandidateCount||!CandidateCapacity)
        return Rc95LdrInvalidParameter;
    *CandidateCount=0;
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
        uint32_t ObjectCell;
        RC95LDR_BCD_BOOT_CANDIDATE Candidate;
        RC95BCD_ELEMENT_VIEW Device, OsDevice, LoaderPath, SystemRoot;
        const uint8_t *Raw=OrderView.Items+i*16;
        Status=Rc95BcdReadGuid(Raw,16,&Guid);
        if(Status!=Rc95LdrSuccess) return Status;
        Rc95BcdGuidToString(&Guid,GuidText);
        Status=Rc95BcdFindSubKey(Hive,ObjectsCell,GuidText,&ObjectCell);
        if(Status==Rc95LdrNotFound) continue;
        if(Status!=Rc95LdrSuccess) return Status;

        memset(&Candidate,0,sizeof(Candidate));
        Candidate.ObjectId=Guid;
        Candidate.DisplayOrder=(uint32_t)i;
        Candidate.IsDefault=(DefaultText[0]!=0 && strcmp(GuidText,DefaultText)==0);
        Candidate.IsEnabled=1;

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

        Candidate.IsWindowsLoader=1;
        if(Count>=CandidateCapacity) return Rc95LdrBufferTooSmall;
        if(SystemRoot.StringCharacters==0 ||
           SystemRoot.StringCharacters>=RC95LDR_MAX_SYSTEM_ROOT_CHARS ||
           LoaderPath.StringCharacters==0 ||
           LoaderPath.StringCharacters>=RC95LDR_MAX_SYSTEM_ROOT_CHARS)
            continue;
        Candidate.DeviceElementStorage=Device;
        Candidate.OsDeviceElementStorage=OsDevice;
        Candidate.DeviceElement=&Candidate.DeviceElementStorage;
        Candidate.OsDeviceElement=&Candidate.OsDeviceElementStorage;
        Candidate.LoaderPathCharacters=LoaderPath.StringCharacters;
        {
            uint32_t Ch;
            for(Ch=0;Ch<LoaderPath.StringCharacters;Ch++)
                Candidate.LoaderPathStorage[Ch]=
                    Rc95LdrReadU16(LoaderPath.StringData+Ch*2);
            Candidate.LoaderPathStorage[Ch]=0;
        }
        Candidate.LoaderPathUtf16=Candidate.LoaderPathStorage;
        Candidate.SystemRootCharacters=SystemRoot.StringCharacters;
        {
            uint32_t Ch;
            for(Ch=0;Ch<SystemRoot.StringCharacters;Ch++)
                Candidate.SystemRootStorage[Ch]=
                    Rc95LdrReadU16(SystemRoot.StringData+Ch*2);
            Candidate.SystemRootStorage[Ch]=0;
        }
        Candidate.SystemRootUtf16=Candidate.SystemRootStorage;
        Candidates[Count]=Candidate;
        /* Repair self-pointers after structure assignment. */
        Candidates[Count].DeviceElement=&Candidates[Count].DeviceElementStorage;
        Candidates[Count].OsDeviceElement=&Candidates[Count].OsDeviceElementStorage;
        Candidates[Count].SystemRootUtf16=Candidates[Count].SystemRootStorage;
        Candidates[Count].LoaderPathUtf16=Candidates[Count].LoaderPathStorage;
        ++Count;
    }
    *CandidateCount=Count;
    return Count?Rc95LdrSuccess:Rc95LdrNotFound;
}

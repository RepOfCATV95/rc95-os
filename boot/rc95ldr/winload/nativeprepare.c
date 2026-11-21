#include "rc95ldrnativeprepare.h"
#include <string.h>
#include <stdio.h>

typedef struct _RC95LDR_DRIVER_LOAD_STATE {
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request;
    RC95LDR_LOADED_MODULE *Modules;
    uint32_t BaseCount, Count;
} RC95LDR_DRIVER_LOAD_STATE;

/* One OS preparation transaction runs at a time in the boot application.
 * Keep the generated registry strings out of the small firmware stack. */
static char g_DriverRegistryPaths[RC95LDR_MODULE_MAX][320];

static RC95LDR_STATUS LoadHiveDriver(void *opaque,const RC95LDR_HIVE_BOOT_DRIVER *driver)
{
    RC95LDR_DRIVER_LOAD_STATE *s=(RC95LDR_DRIVER_LOAD_STATE*)opaque;
    RC95LDR_LOADED_MODULE module;
    int n;RC95LDR_STATUS status;
    if(s->BaseCount+s->Count>=RC95LDR_MODULE_MAX)return Rc95LdrOutOfBounds;
    if(!s->Request->LoadBootDriver)return Rc95LdrUnsupported;
    memset(&module,0,sizeof(module));
    status=s->Request->LoadBootDriver(s->Request->DriverLoadContext,driver,&module);
    if(status!=Rc95LdrSuccess)return status;
    if(!module.ImageBase||!module.ImageSize||!module.Path)return Rc95LdrInvalidImage;
    n=snprintf(g_DriverRegistryPaths[s->Count],sizeof(g_DriverRegistryPaths[s->Count]),
        "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%s",driver->ServiceName);
    if(n<0||(size_t)n>=sizeof(g_DriverRegistryPaths[s->Count]))return Rc95LdrOutOfBounds;
    module.Flags&=~(RC95LDR_MODULE_KERNEL|RC95LDR_MODULE_HAL);
    module.Flags|=RC95LDR_MODULE_BOOT_DRIVER;
    module.RegistryPath=g_DriverRegistryPaths[s->Count];
    s->Modules[s->BaseCount+s->Count]=module;
    s->Count++;
    return Rc95LdrSuccess;
}
static RC95LDR_NT_TARGET_VERSION SelectNtVersion(const RC95LDR_OS_PROFILE *p)
{
    if(p->OsMajorVersion==4)return Rc95LdrNtVersion4;
    if(p->OsMajorVersion==5)return Rc95LdrNtVersion5;
    if(p->OsMajorVersion==6&&p->OsMinorVersion==0)return Rc95LdrNtVersion6;
    if(p->OsMajorVersion==6&&p->OsMinorVersion==1)return Rc95LdrNtVersion61;
    if(p->OsMajorVersion==6&&p->OsMinorVersion==2)return Rc95LdrNtVersion62;
    if(p->OsMajorVersion==6&&p->OsMinorVersion==3)return Rc95LdrNtVersion63;
    if(p->OsMajorVersion==10)return Rc95LdrNtVersion10;
    return 0;
}
RC95LDR_STATUS Rc95LdrPrepareNativeLoaderBlock(
    const RC95LDR_NATIVE_PREPARE_REQUEST *request,
    RC95LDR_NATIVE_PREPARE_RESULT *result)
{
    RC95LDR_SYSTEM_HIVE hive;
    RC95LDR_NATIVE_LOADER_INPUT input;
    RC95LDR_LOADED_MODULE modules[RC95LDR_MODULE_MAX];
    RC95LDR_DRIVER_LOAD_STATE state;
    RC95LDR_STATUS status;
    RC95LDR_OS_IDENTITY identity;RC95LDR_OS_PROFILE verifiedProfile;
    size_t blockSize=0;
    RC95LDR_LOADERSTATE_RESULT loaderState;
    RC95LDR_METADATA_RESULT metadata;

    uint32_t i,haveKernel=0,haveHal=0;
    const RC95LDR_LOADED_MODULE *kernelModule=0,*halModule=0;
    RC95LDR_KERNEL_HANDOFF_CONTEXT handoff;
    uintptr_t blockBegin,blockEnd;
    if(!request||!result||request->StructureSize<sizeof(*request)||
       request->Version!=RC95LDR_NATIVE_PREPARE_VERSION||
       !request->Profile||request->Profile->StructureSize<sizeof(*request->Profile)||
       request->Profile->Version!=RC95LDR_OS_PROFILE_VERSION)
        return Rc95LdrInvalidParameter;
    if(request->Profile->OsFamily==Rc95LdrOsWindowsNtLegacy&&
       request->Profile->BootEntry!=Rc95LdrEntryNtldr)return Rc95LdrUnsupported;
    if(request->Profile->OsFamily==Rc95LdrOsReactOS&&
       request->Profile->BootEntry!=Rc95LdrEntryReactOs)return Rc95LdrUnsupported;
    if(request->Profile->OsFamily==Rc95LdrOsWindowsModern&&
       request->Profile->BootEntry!=Rc95LdrEntryWindowsWinload)return Rc95LdrUnsupported;
    if(request->Profile->OsFamily==Rc95LdrOsRc95&&
       request->Profile->BootEntry!=Rc95LdrEntryRc95Winload)return Rc95LdrUnsupported;
    memset(&identity,0,sizeof(identity));identity.StructureSize=sizeof(identity);identity.Version=RC95LDR_OS_PROFILE_VERSION;
    identity.OsFamily=request->Profile->OsFamily;identity.Firmware=request->Profile->Firmware;
    identity.BootEntry=request->Profile->BootEntry;identity.OsMajorVersion=request->Profile->OsMajorVersion;
    identity.OsMinorVersion=request->Profile->OsMinorVersion;identity.OsBuildNumber=request->Profile->OsBuildNumber;
    identity.Flags=request->Profile->Flags;
    status=Rc95LdrSelectOsProfile(&identity,&verifiedProfile);if(status!=Rc95LdrSuccess)return status;
    memset(result,0,sizeof(*result));result->StructureSize=sizeof(*result);
    result->Version=RC95LDR_NATIVE_PREPARE_VERSION;
    /* ReactOS keeps its dedicated {rosboot} -> rosload.exe path. The
       ReactOS ABI builder remains available to rosload-side integration,
       but RC95LDR does not replace rosload's own preparation sequence. */
    if(request->Profile->OsFamily==Rc95LdrOsReactOS){
        if(!request->LaunchRosLoad||!request->RosLoadPath)return Rc95LdrUnsupported;
        status=request->LaunchRosLoad(request->RosLoadContext,request);
        return status==Rc95LdrSuccess?Rc95LdrUnsupported:status;
    }
    if(!request->BaseInput||request->BaseInput->StructureSize<sizeof(*request->BaseInput)||
       request->BaseInput->Version!=RC95LDR_NATIVE_LOADER_INPUT_VERSION||!request->SystemHiveBytes||!request->SystemHiveSize||
       !request->LoaderBlockBuffer||!request->LoaderBlockCapacity)
        return Rc95LdrInvalidParameter;
    if(request->BaseInput->ModuleCount>RC95LDR_MODULE_MAX||
       (request->BaseInput->ModuleCount&&!request->BaseInput->Modules))
        return Rc95LdrInvalidParameter;
    memset(&hive,0,sizeof(hive));
    status=Rc95LdrOpenSystemHive(request->SystemHiveBytes,request->SystemHiveSize,&hive);
    if(status!=Rc95LdrSuccess)return status;
    if(request->BaseInput->ModuleCount)
        memcpy(modules,request->BaseInput->Modules,
            request->BaseInput->ModuleCount*sizeof(modules[0]));
    for(i=0;i<request->BaseInput->ModuleCount;i++){
        if(modules[i].Flags&RC95LDR_MODULE_KERNEL){if(haveKernel)return Rc95LdrInvalidImage;haveKernel=1;kernelModule=&modules[i];}
        if(modules[i].Flags&RC95LDR_MODULE_HAL){if(haveHal)return Rc95LdrInvalidImage;haveHal=1;halModule=&modules[i];}
    }
    if(!haveKernel||!haveHal)return Rc95LdrInvalidParameter;
    memset(&state,0,sizeof(state));state.Request=request;
    state.Modules=modules;state.BaseCount=request->BaseInput->ModuleCount;
    status=Rc95LdrHiveEnumerateBootDrivers(&hive,LoadHiveDriver,&state);
    if(status!=Rc95LdrSuccess)return status;
    input=*request->BaseInput;
    /* The previous generic extension pointer is not an ABI builder. */
    input.LoaderExtension=0;
    if(request->Profile->OsFamily!=Rc95LdrOsRc95){input.OsMajorVersion=request->Profile->OsMajorVersion;input.OsMinorVersion=request->Profile->OsMinorVersion;input.OsBuildNumber=request->Profile->OsBuildNumber;}
    input.FirmwareIsUefi=request->Profile->Firmware==Rc95LdrFirmwareUefi;
    input.FirmwareSystemTable=request->FirmwareSystemTable;
    input.Modules=modules;input.ModuleCount=state.BaseCount+state.Count;
    input.RegistryBase=request->SystemHiveBytes;
    if(request->SystemHiveSize>UINT32_MAX)return Rc95LdrOutOfBounds;
    input.RegistryLength=(uint32_t)request->SystemHiveSize;
    memset(&loaderState,0,sizeof(loaderState));memset(&metadata,0,sizeof(metadata));
    if(!request->MetadataInput)return Rc95LdrInvalidParameter;
    {uintptr_t mb=(uintptr_t)request->MetadataInput->Buffer,me,bb=(uintptr_t)request->LoaderBlockBuffer,be;
     if(!mb||request->MetadataInput->BufferSize>UINTPTR_MAX-mb||!bb||request->LoaderBlockCapacity>UINTPTR_MAX-bb)return Rc95LdrOutOfBounds;
     me=mb+request->MetadataInput->BufferSize;be=bb+request->LoaderBlockCapacity;
     if(mb<be&&bb<me)return Rc95LdrInvalidParameter;}
    status=Rc95LdrBuildLoaderMetadata(request->MetadataInput,&metadata);
    if(status!=Rc95LdrSuccess)return status;
    input.NlsData=metadata.NlsData;input.ArcDiskInformation=metadata.ArcDiskInformation;
    if(request->Profile->OsFamily==Rc95LdrOsWindowsNtLegacy ||
       request->Profile->OsFamily==Rc95LdrOsWindowsModern ||
       request->Profile->OsFamily==Rc95LdrOsRc95){
        RC95LDR_LOADERSTATE_INPUT stateInput;
        if(!request->LoaderStateInput||!request->PlatformInventoryInput)return Rc95LdrInvalidParameter;
        status=Rc95LdrBuildPlatformInventory(request->PlatformInventoryInput,&result->PlatformInventory);
        if(status!=Rc95LdrSuccess)return status;
        stateInput=*request->LoaderStateInput;
        stateInput.Components=result->PlatformInventory.Components;stateInput.ComponentCount=result->PlatformInventory.ComponentCount;
        if(!stateInput.PhysicalMemory)return Rc95LdrInvalidParameter;
        stateInput.NtVersion=(request->Profile->OsFamily==Rc95LdrOsRc95)?
            Rc95LdrNtVersion10:SelectNtVersion(request->Profile);
        stateInput.OsMajorVersion=request->Profile->OsMajorVersion;
        stateInput.OsMinorVersion=request->Profile->OsMinorVersion;
        status=Rc95LdrBuildNtLoaderState(&stateInput,&loaderState);
        if(status!=Rc95LdrSuccess)return status;
        if(!loaderState.ConfigurationRoot){Rc95LdrReleaseNtLoaderState(&loaderState);return Rc95LdrInvalidParameter;}
        input.ConfigurationRoot=loaderState.ConfigurationRoot;
    }
    if(request->Profile->OsFamily==Rc95LdrOsWindowsNtLegacy){
        input.NtVersion=SelectNtVersion(request->Profile);
        status=Rc95LdrBuildNtLegacyLoaderBlock(&input,request->LoaderBlockBuffer,
            request->LoaderBlockCapacity,&blockSize);
    }else if(request->Profile->OsFamily==Rc95LdrOsWindowsModern){
        input.NtVersion=SelectNtVersion(request->Profile);
        status=Rc95LdrBuildWindowsModernLoaderBlock(&input,request->LoaderBlockBuffer,
            request->LoaderBlockCapacity,&blockSize);
    }else{
        status=Rc95LdrBuildRc95NativeLoaderBlock(&input,request->LoaderBlockBuffer,
            request->LoaderBlockCapacity,&blockSize);
    }
    if(status!=Rc95LdrSuccess){Rc95LdrReleaseNtLoaderState(&loaderState);return status;}
    result->LoaderBlock=request->LoaderBlockBuffer;
    result->LoaderBlockSize=blockSize;
    result->LoadedBootDriverCount=state.Count;
    result->LoaderState=loaderState;
    result->Metadata=metadata;
    /* Publish a typed handoff contract now, but do not transfer control.
       The architecture transition remains blocked until every requirement
       (including extension/configuration/NLS/stack/page tables) is complete. */
    memset(&handoff,0,sizeof(handoff));
    handoff.Signature=RC95LDR_HANDOFF_SIGNATURE;
    handoff.StructureSize=sizeof(handoff);
    handoff.Version=RC95LDR_HANDOFF_VERSION;
    handoff.OsFamily=request->Profile->OsFamily;
    handoff.Firmware=request->Profile->Firmware;
    handoff.Architecture=input.Architecture;
    handoff.MachineType=input.MachineType;
    handoff.NtMajorVersion=request->Profile->OsMajorVersion;
    handoff.NtMinorVersion=request->Profile->OsMinorVersion;
    handoff.KernelEntry=kernelModule->EntryPoint;
    handoff.KernelBase=kernelModule->ImageBase;
    handoff.KernelSize=kernelModule->ImageSize;
    handoff.HalBase=halModule->ImageBase;
    handoff.LoaderBlockAddress=(uint64_t)(uintptr_t)request->LoaderBlockBuffer;
    handoff.LoaderBlockSize=(uint32_t)blockSize;
    handoff.MemoryRangeCount=input.MemoryRangeCount;
    handoff.FirmwareSystemTable=request->FirmwareSystemTable;
    handoff.FirmwareRuntimeServices=input.VirtualEfiRuntimeServices;
    handoff.FirmwareMemoryMap=input.FirmwareMapAddress;
    handoff.FirmwareMemoryMapSize=input.FirmwareMapSize;
    handoff.FirmwareMemoryMapDescriptorSize=input.FirmwareMapDescriptorSize;
    handoff.RequiredRequirements=RC95LDR_HANDOFF_REQUIRED;
    if(input.Architecture==Rc95LdrNtArchPowerPc||input.Architecture==Rc95LdrNtArchArm)
        handoff.RequiredRequirements&=~RC95LDR_HANDOFF_PROCESSOR_CONTROL;
    if(request->Profile->OsFamily==Rc95LdrOsWindowsNtLegacy &&
       request->Profile->OsMajorVersion==4)
        handoff.RequiredRequirements&=~RC95LDR_HANDOFF_LOADER_EXTENSION;
    handoff.CompletedRequirements=RC95LDR_HANDOFF_KERNEL_MAPPED |
        RC95LDR_HANDOFF_HAL_MAPPED | RC95LDR_HANDOFF_SYSTEM_HIVE |
        RC95LDR_HANDOFF_BOOT_DRIVERS | RC95LDR_HANDOFF_LOADER_BLOCK;
    if(input.MemoryRangeCount)handoff.CompletedRequirements|=RC95LDR_HANDOFF_MEMORY_MAP;
    if(input.ConfigurationRoot) {
        handoff.ConfigurationRoot=input.ConfigurationRoot;
        handoff.CompletedRequirements|=RC95LDR_HANDOFF_CONFIGURATION_TREE;
    }
    if(input.NlsData){handoff.NlsData=input.NlsData;handoff.CompletedRequirements|=RC95LDR_HANDOFF_NLS;}
    if(input.ArcDiskInformation){handoff.ArcDiskInformation=input.ArcDiskInformation;handoff.CompletedRequirements|=RC95LDR_HANDOFF_ARC_DISK_INFO;}
    /* No temporary extension prefix is fabricated. Non-NT4 handoff remains
       incomplete until the exact target-generation extension ABI is built. */
    if(request->ArchitectureHandoffInput){
        status=Rc95LdrPrepareArchitectureHandoff(request->ArchitectureHandoffInput,&handoff);
        if(status!=Rc95LdrSuccess){Rc95LdrReleaseNtLoaderState(&result->LoaderState);memset(&loaderState,0,sizeof(loaderState));return status;}
    }
    if(request->Profile->Firmware==Rc95LdrFirmwareUefi && request->FirmwareSystemTable &&
       input.VirtualEfiRuntimeServices && input.FirmwareMapAddress && input.FirmwareMapSize &&
       input.FirmwareMapDescriptorSize && input.FirmwareMapSize%input.FirmwareMapDescriptorSize==0)
        handoff.CompletedRequirements|=RC95LDR_HANDOFF_FIRMWARE_STATE;
    status=Rc95LdrValidateKernelHandoff(&handoff);
    if(status!=Rc95LdrSuccess&&status!=Rc95LdrUnsupported){Rc95LdrReleaseNtLoaderState(&result->LoaderState);memset(&loaderState,0,sizeof(loaderState));return status;}
    result->Handoff=handoff;
    result->HandoffReady=(status==Rc95LdrSuccess)?1u:0u;
    return Rc95LdrSuccess;
}

void Rc95LdrReleaseNativePrepareResult(RC95LDR_NATIVE_PREPARE_RESULT *result)
{
    if(!result)return;
    Rc95LdrReleaseNtLoaderState(&result->LoaderState);
    result->HandoffReady=0;
    result->Handoff.CompletedRequirements=0;
    result->LoaderBlock=NULL;
    result->LoaderBlockSize=0;
}

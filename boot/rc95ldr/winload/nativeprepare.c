#include "rc95ldrnativeprepare.h"
#include <string.h>
#include <stdio.h>
#ifdef _BLDR_
#include "rc95ldrcmboot.h"
#endif

static int
Rc95LdrContainsOptionInsensitive(const char *Text,size_t Length,const char *Option)
{
    size_t i,j,n;
    if(!Text||!Option)return 0;
    n=strlen(Option);
    if(!n||n>Length)return 0;
    for(i=0;i<=Length-n;++i){
        for(j=0;j<n;++j){
            unsigned char a=(unsigned char)Text[i+j],b=(unsigned char)Option[j];
            if(a>='a'&&a<='z')a=(unsigned char)(a-('a'-'A'));
            if(b>='a'&&b<='z')b=(unsigned char)(b-('a'-'A'));
            if(a!=b)break;
        }
        if(j==n)return 1;
    }
    return 0;
}

typedef struct _RC95LDR_DRIVER_LOAD_STATE {
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request;
    RC95LDR_LOADED_MODULE *Modules;
    uint32_t BaseCount, Count;
    char (*RegistryPaths)[320];
} RC95LDR_DRIVER_LOAD_STATE;

static int IsSafeServiceName(const char *Name, size_t Capacity)
{
    size_t i, length;
    if (!Name) return 0;
    for (length = 0; length < Capacity && Name[length]; ++length) { }
    if (!length || length == Capacity) return 0;
    if ((length == 1 && Name[0] == '.') ||
        (length == 2 && Name[0] == '.' && Name[1] == '.')) return 0;
    for (i = 0; i < length; ++i)
    {
        unsigned char ch = (unsigned char)Name[i];
        /* Service names are single registry-key components. Reject path
         * separators, control bytes and registry path metacharacters. */
        if (ch < 0x20 || ch == 0x7f || ch == '/' || ch == '\\' ||
            ch == ':' ) return 0;
    }
    return 1;
}

static int IsBoundedNonemptyPath(const char *Path, size_t Capacity)
{
    size_t i;
    if (!Path || !Path[0]) return 0;
    for (i = 0; i < Capacity && Path[i]; ++i)
    {
        unsigned char ch = (unsigned char)Path[i];
        if (ch < 0x20 || ch == 0x7f) return 0;
    }
    return i != 0 && i < Capacity;
}

static RC95LDR_STATUS LoadHiveDriver(void *opaque,const RC95LDR_HIVE_BOOT_DRIVER *driver)
{
    RC95LDR_DRIVER_LOAD_STATE *s=(RC95LDR_DRIVER_LOAD_STATE*)opaque;
    RC95LDR_LOADED_MODULE module;
    int n;RC95LDR_STATUS status;
    if (!s || !s->Request || !driver ||
        !IsSafeServiceName(driver->ServiceName, sizeof(driver->ServiceName)) ||
        !IsBoundedNonemptyPath(driver->ImagePath, sizeof(driver->ImagePath)))
        return Rc95LdrCorruptData;
    if(s->BaseCount+s->Count>=RC95LDR_MODULE_MAX)return Rc95LdrOutOfBounds;
    if(!s->Request->LoadBootDriver){
        if(s->Request->ReportBootError)s->Request->ReportBootError(
            s->Request->BootErrorContext,"RC95LDR: no LoadBootDriver callback is installed; refusing to publish an unbacked boot-driver module.");
        return Rc95LdrUnsupported;
    }
    memset(&module,0,sizeof(module));
    status=s->Request->LoadBootDriver(s->Request->DriverLoadContext,driver,&module);
    if(status!=Rc95LdrSuccess){
        if(s->Request->ReportBootError)s->Request->ReportBootError(
            s->Request->BootErrorContext,"RC95LDR: LoadBootDriver failed to load a required SYSTEM-hive boot service.");
        return status;
    }
    if(!module.ImageBase||!module.ImageSize||!module.Path ||
       !module.PeSections || !module.PeSectionCount ||
       module.PeSectionCount > RC95LDR_PE_MAX_SECTIONS ||
       !module.PeSizeOfHeaders || module.PeSizeOfHeaders > module.ImageSize ||
       !module.PhysicalBase || !module.PhysicalSize ||
       module.PhysicalSize < module.ImageSize ||
       (module.PhysicalBase & (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)) ||
       (module.PhysicalSize & (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)) ||
       module.PhysicalBase > UINT64_MAX - module.PhysicalSize) {
        if(s->Request->ReportBootError)s->Request->ReportBootError(
            s->Request->BootErrorContext,"RC95LDR: LoadBootDriver returned missing PE section metadata or a malformed physical extent (zero base/size, undersized allocation, unaligned range, or address overflow). The module was rejected.");
        return Rc95LdrInvalidImage;
    }
    if(!s->Request->ValidateModuleAllocation ||
       s->Request->ValidateModuleAllocation(s->Request->AllocationValidationContext,
          module.ImageBase,module.PhysicalBase,module.ImageSize,module.PhysicalSize)!=Rc95LdrSuccess) {
        if(s->Request->ReportBootError)s->Request->ReportBootError(
            s->Request->BootErrorContext,"RC95LDR: LoadBootDriver physical extent is not attested by the platform allocator ledger; callback may have fabricated an address or reported a virtual pointer as physical.");
        return Rc95LdrInvalidImage;
    }
    n=snprintf(s->RegistryPaths[s->Count],sizeof(s->RegistryPaths[s->Count]),
        "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%s",driver->ServiceName);
    if(n<0||(size_t)n>=sizeof(s->RegistryPaths[s->Count]))return Rc95LdrOutOfBounds;
    module.Flags&=~(RC95LDR_MODULE_KERNEL|RC95LDR_MODULE_HAL);
    module.Flags|=RC95LDR_MODULE_BOOT_DRIVER;
    module.RegistryPath=s->RegistryPaths[s->Count];
    s->Modules[s->BaseCount+s->Count]=module;
    s->Count++;
    return Rc95LdrSuccess;
}
static int DriverPathEndsWith(const char *Path, const char *Name)
{
    size_t n, m;
    const char *base;
    if (!Path || !Name) return 0;
    base = Path;
    for (; *Path; ++Path)
        if (*Path == '/' || *Path == '\\') base = Path + 1;
    n = strlen(base); m = strlen(Name);
    if (n != m) return 0;
    while (n) {
        unsigned char a = (unsigned char)base[--n];
        unsigned char b = (unsigned char)Name[n];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

RC95LDR_STATUS
Rc95LdrValidateBootResourceModule(
    const RC95LDR_LOADED_MODULE *Module,
    RC95LDR_VALIDATE_MODULE_ALLOCATION ValidateAllocation,
    void *ValidationContext)
{
    if (!Module)
        return Rc95LdrInvalidParameter;

    if (Module->Flags != RC95LDR_MODULE_RESOURCE ||
        !Module->ImageBase || !Module->ImageSize ||
        Module->ImageSize > UINT32_MAX || Module->EntryPoint != 0 ||
        !Module->PhysicalBase || !Module->PhysicalSize ||
        !Module->PeSections || !Module->PeSectionCount ||
        Module->PeSectionCount > RC95LDR_PE_MAX_SECTIONS ||
        !Module->PeSizeOfHeaders || Module->PeSizeOfHeaders > Module->ImageSize ||
        Module->PhysicalSize < Module->ImageSize ||
        (Module->PhysicalBase & (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)) ||
        (Module->PhysicalSize & (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)) ||
        Module->PhysicalBase > UINT64_MAX - Module->PhysicalSize ||
        !IsBoundedNonemptyPath(Module->Path, 512u) ||
        !DriverPathEndsWith(Module->Path, "bootres.dll"))
        return Rc95LdrInvalidImage;

    if (!ValidateAllocation)
        return Rc95LdrUnsupported;

    if (ValidateAllocation(ValidationContext,
            Module->ImageBase, Module->PhysicalBase,
            Module->ImageSize, Module->PhysicalSize) != Rc95LdrSuccess)
        return Rc95LdrInvalidImage;

    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrAppendBootResourceModule(
    const RC95LDR_LOADED_MODULE *Module,
    RC95LDR_VALIDATE_MODULE_ALLOCATION ValidateAllocation,
    void *ValidationContext,
    RC95LDR_LOADED_MODULE *Modules,
    uint32_t *ModuleCount,
    uint32_t ModuleCapacity,
    uint32_t *Added)
{
    if (!ModuleCount || !Added || (ModuleCapacity && !Modules))
        return Rc95LdrInvalidParameter;

    *Added = 0;
    if (!Module)
        return Rc95LdrSuccess; /* graphicsmodedisabled */

    if (Rc95LdrValidateBootResourceModule(Module,
            ValidateAllocation, ValidationContext) != Rc95LdrSuccess)
        return Rc95LdrSuccess; /* Optional resource: fail closed, not boot. */

    if (*ModuleCount >= ModuleCapacity)
        return Rc95LdrSuccess; /* Preserve slots for required boot modules. */

    Modules[*ModuleCount] = *Module;
    ++*ModuleCount;
    *Added = 1;
    return Rc95LdrSuccess;
}

/* The I386 multiprocessor HAL also belongs in the boot-driver list.
 * BaseInput already supplies the HAL module itself; this adds its boot
 * driver representation without duplicating an explicitly enumerated image. */
static RC95LDR_STATUS LoadI386MultiprocessorHalDriver(RC95LDR_DRIVER_LOAD_STATE *State)
{
    RC95LDR_HIVE_BOOT_DRIVER driver;
    const char *fileName;
    uint32_t i;
    if (State->Request->BaseInput->Architecture != Rc95LdrNtArchI386)
        return Rc95LdrSuccess;
    fileName = (State->Request->Profile->OsFamily == Rc95LdrOsRc95) ?
        "HALMP.DLL" : "HALMPS.DLL";
    for (i = 0; i < State->BaseCount + State->Count; ++i) {
        if (DriverPathEndsWith(State->Modules[i].Path, fileName)) {
            RC95LDR_LOADED_MODULE module;
            int n;
            /* If the HAL image was already mapped as the HAL module, add a
             * second loader-list record for its boot-driver role. If hive
             * enumeration already loaded it, it is already represented. */
            if (i >= State->BaseCount)
                return Rc95LdrSuccess;
            if (State->BaseCount + State->Count >= RC95LDR_MODULE_MAX)
                return Rc95LdrOutOfBounds;
            module = State->Modules[i];
            module.Flags &= ~(RC95LDR_MODULE_KERNEL | RC95LDR_MODULE_HAL);
            module.Flags |= RC95LDR_MODULE_BOOT_DRIVER;
            n = snprintf(State->RegistryPaths[State->Count],
                sizeof(State->RegistryPaths[State->Count]),
                "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%s",
                (State->Request->Profile->OsFamily == Rc95LdrOsRc95) ?
                    "HALMP" : "HALMPS");
            if (n < 0 || (size_t)n >= sizeof(State->RegistryPaths[State->Count]))
                return Rc95LdrOutOfBounds;
            module.RegistryPath = State->RegistryPaths[State->Count];
            State->Modules[State->BaseCount + State->Count] = module;
            ++State->Count;
            return Rc95LdrSuccess;
        }
    }

    memset(&driver, 0, sizeof(driver));
    memcpy(driver.ServiceName,
        (State->Request->Profile->OsFamily == Rc95LdrOsRc95) ? "HALMP" : "HALMPS",
        (State->Request->Profile->OsFamily == Rc95LdrOsRc95) ? 6 : 7);
    memcpy(driver.ImagePath, "System32\\", 9);
    memcpy(driver.ImagePath + 9, fileName, strlen(fileName) + 1);
    memcpy(driver.Group, "System Bus Extender", 20);
    driver.Start = 0; /* SERVICE_BOOT_START */
    driver.Type = 1;  /* SERVICE_KERNEL_DRIVER */
    driver.ErrorControl = 1;
    return LoadHiveDriver(State, &driver);
}

static void ReportMpsCompatibilityError(const RC95LDR_NATIVE_PREPARE_REQUEST *Request,
    RC95LDR_NATIVE_PREPARE_RESULT *Result, uint32_t Revision)
{
    unsigned minor=(Revision==1)?1u:(Revision==2)?2u:0u;
    if(minor){
        (void)snprintf(Result->ErrorMessage,sizeof(Result->ErrorMessage),
            "HALMPS.DLL is not supported in Intel MPS version 1.%u. RC95-OS must be reinstalled using an MPS 1.4-compatible system.",
            minor);
    }else{
        (void)snprintf(Result->ErrorMessage,sizeof(Result->ErrorMessage),
            "HALMPS.DLL requires Intel MPS version 1.4 (detected revision %u). RC95-OS must be reinstalled using an MPS 1.4-compatible system.",
            Revision);
    }
    if(Request->ReportBootError)
        Request->ReportBootError(Request->BootErrorContext,Result->ErrorMessage);
}

static RC95LDR_STATUS ReportNativePrepareFailure(
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request,
    RC95LDR_NATIVE_PREPARE_RESULT *Result,
    const char *Stage,
    RC95LDR_STATUS Status)
{
    if (Result) {
        (void)snprintf(Result->ErrorMessage, sizeof(Result->ErrorMessage),
            "RC95LDR native preparation failed during %s (status 0x%08X).",
            Stage ? Stage : "an unknown stage", (unsigned)Status);
    }
    if (Request && Request->ReportBootError && Result)
        Request->ReportBootError(Request->BootErrorContext, Result->ErrorMessage);
    return Status;
}

static void ReleaseProfileLoaderState(RC95LDR_NATIVE_PREPARE_RESULT *Result)
{
    if (!Result) return;
    if (Result->Handoff.OsFamily == Rc95LdrOsRc95)
        Rc95LdrReleaseRc95LoaderState(&Result->Rc95LoaderState);
    else
        Rc95LdrReleaseNtLoaderState(&Result->LoaderState);
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
#ifndef _BLDR_
    RC95LDR_SYSTEM_HIVE hive;
#endif
    RC95LDR_NATIVE_LOADER_INPUT input;
    RC95LDR_RC95_LOADER_INPUT rc95BlockInput;
    RC95LDR_LOADED_MODULE modules[RC95LDR_MODULE_MAX];
    RC95LDR_DRIVER_LOAD_STATE state;
    RC95LDR_STATUS status;
    RC95LDR_OS_IDENTITY identity;RC95LDR_OS_PROFILE verifiedProfile;
    size_t blockSize=0;
    RC95LDR_LOADERSTATE_RESULT loaderState;
    RC95LDR_RC95_LOADERSTATE_RESULT rc95LoaderState;
    RC95LDR_RC95_CONFIG_COMPONENT_INPUT rc95Components[RC95LDR_CONFIG_COMPONENT_MAX];
    uint64_t configurationRoot = 0;
    RC95LDR_METADATA_RESULT metadata;
    RC95LDR_MEMORY_RESERVATION_SET reservationSet;
    char RamdiskLoadOptions[8192];

    uint32_t i,haveKernel=0,haveHal=0,bootResourceAdded=0;
    RC95LDR_LOADED_MODULE *kernelModule=0;
    const RC95LDR_LOADED_MODULE *halModule=0;
    RC95LDR_KERNEL_HANDOFF_CONTEXT handoff;
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
    result->Handoff.OsFamily=request->Profile->OsFamily;
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
#ifndef _BLDR_
    memset(&hive,0,sizeof(hive));
    status=Rc95LdrOpenSystemHive(request->SystemHiveBytes,request->SystemHiveSize,&hive);
    if(status!=Rc95LdrSuccess)return ReportNativePrepareFailure(request,result,"opening and validating the SYSTEM registry hive",status);
#endif
    if(request->BaseInput->ModuleCount)
        memcpy(modules,request->BaseInput->Modules,
            request->BaseInput->ModuleCount*sizeof(modules[0]));
    for(i=0;i<request->BaseInput->ModuleCount;i++){
        if(modules[i].Flags&RC95LDR_MODULE_KERNEL){
            if(haveKernel || !request->KernelPhysicalBase ||
               !request->KernelPhysicalSize ||
               request->KernelPhysicalSize < modules[i].ImageSize ||
               (request->KernelPhysicalBase & (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)) ||
               (request->KernelPhysicalSize & (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)) ||
               request->KernelPhysicalBase > UINT64_MAX - request->KernelPhysicalSize)
                return Rc95LdrInvalidImage;
            /* osload owns the kernel AllocatePages result, so this physical
               extent is authoritative. Reject stale or conflicting metadata. */
            if ((modules[i].PhysicalBase || modules[i].PhysicalSize) &&
                (modules[i].PhysicalBase != request->KernelPhysicalBase ||
                 modules[i].PhysicalSize != request->KernelPhysicalSize))
                return Rc95LdrInvalidImage;
            modules[i].PhysicalBase = request->KernelPhysicalBase;
            modules[i].PhysicalSize = request->KernelPhysicalSize;
            haveKernel=1;
            kernelModule=&modules[i];
        }
        if(modules[i].Flags&RC95LDR_MODULE_HAL){
            if(haveHal || !modules[i].PhysicalBase || !modules[i].PhysicalSize ||
               modules[i].PhysicalSize < modules[i].ImageSize ||
               (modules[i].PhysicalBase & (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)) ||
               (modules[i].PhysicalSize & (RC95LDR_MEMORYMAP_PAGE_SIZE - 1u)) ||
               modules[i].PhysicalBase > UINT64_MAX - modules[i].PhysicalSize)
                return Rc95LdrInvalidImage;
            haveHal=1;
            halModule=&modules[i];
        }
    }
    if(!haveKernel||!haveHal)return ReportNativePrepareFailure(request,result,"validating the required kernel and HAL module records",Rc95LdrInvalidParameter);
    if(!request->PlatformInventoryInput)return Rc95LdrInvalidParameter;
    status=Rc95LdrBuildPlatformInventory(request->PlatformInventoryInput,
        &result->PlatformInventory);
    if(status!=Rc95LdrSuccess)return ReportNativePrepareFailure(request,result,"building the firmware platform inventory",status);
    /* AMD64 is deliberately stricter than I386: all three requirements are
       fatal and independently diagnosable. Do not run this gate for I386. */
    if (request->BaseInput->Architecture == Rc95LdrNtArchAmd64) {
        if (!result->PlatformInventory.Topology.AcpiPresent) {
            (void)snprintf(result->ErrorMessage, sizeof(result->ErrorMessage),
                "RC95LDR AMD64 requirement failed: ACPI firmware tables are required but were not found.");
            if (request->ReportBootError)
                request->ReportBootError(request->BootErrorContext, result->ErrorMessage);
            return Rc95LdrUnsupported;
        }
        if (!result->PlatformInventory.Topology.MadtPresent ||
            result->PlatformInventory.Topology.IoControllerCount == 0) {
            (void)snprintf(result->ErrorMessage, sizeof(result->ErrorMessage),
                "RC95LDR AMD64 requirement failed: an ACPI MADT containing at least one I/O APIC is required.");
            if (request->ReportBootError)
                request->ReportBootError(request->BootErrorContext, result->ErrorMessage);
            return Rc95LdrUnsupported;
        }
        if (!request->CheckAmd64CpuSupport ||
            request->CheckAmd64CpuSupport(request->CpuFeatureContext) != Rc95LdrSuccess) {
            (void)snprintf(result->ErrorMessage, sizeof(result->ErrorMessage),
                "RC95LDR AMD64 requirement failed: a 64-bit-capable CPU with AMD64 long-mode support is required.");
            if (request->ReportBootError)
                request->ReportBootError(request->BootErrorContext, result->ErrorMessage);
            return Rc95LdrUnsupported;
        }
    }
    /* HALMPS.DLL requires Intel MP Specification 1.4. Do not let an
       incompatible firmware revision reach kernel/HAL initialization. */
    if(request->BaseInput->Architecture==Rc95LdrNtArchI386 &&
       request->Profile->OsFamily!=Rc95LdrOsRc95 &&
       result->PlatformInventory.Topology.MpsPresent &&
       result->PlatformInventory.Topology.MpsSpecificationRevision!=4u){
        ReportMpsCompatibilityError(request,result,
            result->PlatformInventory.Topology.MpsSpecificationRevision);
        return Rc95LdrUnsupported;
    }
    memset(&state,0,sizeof(state));state.Request=request;
    state.Modules=modules;state.BaseCount=request->BaseInput->ModuleCount;
     state.RegistryPaths=result->DriverRegistryPaths;
#ifdef _BLDR_
    status=Rc95LdrCmEnumerateBootDrivers(request->SystemHiveBytes,
        request->SystemHiveSize,LoadHiveDriver,&state);
#else
    status=Rc95LdrHiveEnumerateBootDrivers(&hive,LoadHiveDriver,&state);
#endif
    if(status!=Rc95LdrSuccess)return ReportNativePrepareFailure(request,result,"enumerating and loading SYSTEM-hive boot-start drivers",status);
    status=LoadI386MultiprocessorHalDriver(&state);
    if(status!=Rc95LdrSuccess)return ReportNativePrepareFailure(request,result,"adding the architecture-specific multiprocessor HAL driver",status);
    /* Optional graphics resources are appended after boot-start drivers so
       they cannot consume a slot needed by a required SYSTEM-hive driver.
       A NULL BootResourceModule is the graphicsmodedisabled path. */
    if (request->Profile->OsFamily == Rc95LdrOsRc95) {
        status = Rc95LdrAppendBootResourceModule(
            request->BootResourceModule,
            request->ValidateModuleAllocation,
            request->AllocationValidationContext,
            &modules[state.BaseCount],
            &state.Count,
            RC95LDR_MODULE_MAX - state.BaseCount,
            &bootResourceAdded);
        if (status != Rc95LdrSuccess)
            return ReportNativePrepareFailure(request,result,
                "admitting the optional boot resource module",status);
        result->LoadedBootResource = bootResourceAdded;
    }
    if (!request->MemoryReservations || !request->NativeAllocations ||
        !request->FirmwareMemoryRanges || !request->FirmwareMemoryRangeCount)
        return Rc95LdrInvalidParameter;
    reservationSet = *request->MemoryReservations;
    if (request->Profile->Firmware == Rc95LdrFirmwareBios) {
        int supplied = request->BiosFirmwareStateAddress ||
            request->BiosFirmwareStateSize || request->BiosFirmwareStateVersion ||
            request->BiosFirmwareStatePhysicalBase ||
            request->BiosFirmwareStateAllocationSize;
        if (supplied) {
            size_t ai;
            int found = 0;
            if (!request->BiosFirmwareStateAddress ||
                request->BiosFirmwareStateAddress > UINTPTR_MAX ||
                request->BiosFirmwareStateSize < 16u ||
                request->BiosFirmwareStateVersion != 1u ||
                !request->BiosFirmwareStatePhysicalBase ||
                !request->BiosFirmwareStateAllocationSize ||
                request->NativeAllocationCount > RC95LDR_MEMORYMAP_MAX_RESERVATIONS ||
                request->BiosFirmwareStateSize > request->BiosFirmwareStateAllocationSize ||
                (request->BiosFirmwareStatePhysicalBase & 0xfffu) ||
                (request->BiosFirmwareStateAllocationSize & 0xfffu) ||
                request->BiosFirmwareStatePhysicalBase >
                    UINT64_MAX - request->BiosFirmwareStateAllocationSize)
                return Rc95LdrInvalidParameter;
            for (ai = 0; ai < request->NativeAllocationCount; ++ai) {
                const RC95LDR_NATIVE_ALLOCATION *a = &request->NativeAllocations[ai];
                if (a->Kind == RC95LDR_ALLOC_BIOS_FIRMWARE_STATE &&
                    a->MemoryType == LoaderMemoryData &&
                    a->PhysicalBase == request->BiosFirmwareStatePhysicalBase &&
                    a->Length == request->BiosFirmwareStateAllocationSize)
                    found = 1;
            }
            if (!found) return Rc95LdrInvalidParameter;
        }
    } else if (request->BiosFirmwareStateAddress ||
               request->BiosFirmwareStateSize || request->BiosFirmwareStateVersion ||
               request->BiosFirmwareStatePhysicalBase ||
               request->BiosFirmwareStateAllocationSize) {
        return Rc95LdrInvalidParameter;
    }
    status = Rc95LdrRegisterNativeAllocations(modules,
        state.BaseCount + state.Count, request->NativeAllocations,
        request->NativeAllocationCount,
        request->Profile->Firmware == Rc95LdrFirmwareUefi,
        &reservationSet);
    if (status != Rc95LdrSuccess) return ReportNativePrepareFailure(request,result,"validating and reserving loader-owned physical allocations",status);
    input=*request->BaseInput;
    if (request->RamdiskPhysicalBase || request->RamdiskAllocationSize ||
        request->RamdiskImageOffset || request->RamdiskImageLength) {
        size_t ai, OptionLength=0; int FoundRamdisk=0; int n;
        if ((request->Profile->OsFamily!=Rc95LdrOsWindowsNtLegacy &&
             request->Profile->OsFamily!=Rc95LdrOsWindowsModern) ||
            !request->RamdiskPhysicalBase || !request->RamdiskAllocationSize ||
            !request->RamdiskImageLength ||
            request->RamdiskPhysicalBase>UINT64_MAX-request->RamdiskAllocationSize ||
            request->RamdiskImageOffset>request->RamdiskAllocationSize ||
            request->RamdiskImageLength>request->RamdiskAllocationSize-request->RamdiskImageOffset ||
            (request->RamdiskPhysicalBase&0xfffu) ||
            (request->RamdiskAllocationSize&0xfffu) || !request->NativeAllocations ||
            request->NativeAllocationCount>RC95LDR_MEMORYMAP_MAX_RESERVATIONS)
            return Rc95LdrInvalidParameter;
        if (input.LoadOptions) {
            while (OptionLength<sizeof(RamdiskLoadOptions)-128u &&
                   input.LoadOptions[OptionLength]) ++OptionLength;
            if (OptionLength==sizeof(RamdiskLoadOptions)-128u)
                return Rc95LdrBufferTooSmall;
        }
        for (ai=0;ai<request->NativeAllocationCount;++ai) {
            const RC95LDR_NATIVE_ALLOCATION *A=&request->NativeAllocations[ai];
            if (A->Kind==RC95LDR_ALLOC_RAMDISK && A->MemoryType==LoaderXIPRom &&
                A->PhysicalBase==request->RamdiskPhysicalBase &&
                A->Length==request->RamdiskAllocationSize) FoundRamdisk=1;
        }
        if (!FoundRamdisk) return Rc95LdrInvalidParameter;
        if (Rc95LdrContainsOptionInsensitive(input.LoadOptions,OptionLength,
                "RDIMAGEOFFSET") ||
            Rc95LdrContainsOptionInsensitive(input.LoadOptions,OptionLength,
                "RDIMAGELENGTH"))
            return Rc95LdrCorruptData;
        n=snprintf(RamdiskLoadOptions,sizeof(RamdiskLoadOptions),
            "%.*s%sRDIMAGEOFFSET=%llu RDIMAGELENGTH=%llu",
            (int)OptionLength,input.LoadOptions?input.LoadOptions:"",
            OptionLength?" ":"",
            (unsigned long long)request->RamdiskImageOffset,
            (unsigned long long)request->RamdiskImageLength);
        if(n<0||(size_t)n>=sizeof(RamdiskLoadOptions))return Rc95LdrBufferTooSmall;
        input.ArcBootDevice="ramdisk(0)";
        input.LoadOptions=RamdiskLoadOptions;
    }
    /* The previous generic extension pointer is not an ABI builder. */
    input.LoaderExtension=0;
    input.OsMajorVersion=request->Profile->OsMajorVersion;
    input.OsMinorVersion=request->Profile->OsMinorVersion;
    input.OsBuildNumber=request->Profile->OsBuildNumber;
    input.FirmwareIsUefi=request->Profile->Firmware==Rc95LdrFirmwareUefi;
    input.FirmwareSystemTable=request->FirmwareSystemTable;
    input.Modules=modules;input.ModuleCount=state.BaseCount+state.Count;
    input.RegistryBase=request->SystemHiveBytes;
    if(request->SystemHiveSize>UINT32_MAX)return Rc95LdrOutOfBounds;
    input.RegistryLength=(uint32_t)request->SystemHiveSize;
    memset(&loaderState,0,sizeof(loaderState));
    memset(&rc95LoaderState,0,sizeof(rc95LoaderState));
    memset(&metadata,0,sizeof(metadata));
    if(!request->MetadataInput)return Rc95LdrInvalidParameter;
    {uintptr_t mb=(uintptr_t)request->MetadataInput->Buffer,me,bb=(uintptr_t)request->LoaderBlockBuffer,be;
     if(!mb||request->MetadataInput->BufferSize>UINTPTR_MAX-mb||!bb||request->LoaderBlockCapacity>UINTPTR_MAX-bb)return Rc95LdrOutOfBounds;
     me=mb+request->MetadataInput->BufferSize;be=bb+request->LoaderBlockCapacity;
     if(mb<be&&bb<me)return Rc95LdrInvalidParameter;}
    status=Rc95LdrBuildLoaderMetadata(request->MetadataInput,&metadata);
    if(status!=Rc95LdrSuccess)return ReportNativePrepareFailure(request,result,"building NLS and ARC disk metadata",status);
    input.NlsData=metadata.NlsData;input.ArcDiskInformation=metadata.ArcDiskInformation;
    if (request->Profile->OsFamily == Rc95LdrOsRc95) {
        RC95LDR_RC95_LOADERSTATE_INPUT rc95Input;
        uint32_t componentIndex;
        if (!request->Rc95LoaderStateInput ||
            !request->Rc95LoaderStateInput->PhysicalMemory ||
            result->PlatformInventory.ComponentCount > RC95LDR_CONFIG_COMPONENT_MAX)
            return Rc95LdrInvalidParameter;
        for (componentIndex = 0;
             componentIndex < result->PlatformInventory.ComponentCount;
             ++componentIndex) {
            const RC95LDR_CONFIG_COMPONENT_INPUT *source =
                &result->PlatformInventory.Components[componentIndex];
            rc95Components[componentIndex].Class = source->Class;
            rc95Components[componentIndex].Type = source->Type;
            rc95Components[componentIndex].Flags = source->Flags;
            rc95Components[componentIndex].Version = source->Version;
            rc95Components[componentIndex].Revision = source->Revision;
            rc95Components[componentIndex].Key = source->Key;
            rc95Components[componentIndex].AffinityMask = source->AffinityMask;
            rc95Components[componentIndex].Identifier = source->Identifier;
            rc95Components[componentIndex].ConfigurationData = source->ConfigurationData;
            rc95Components[componentIndex].ConfigurationDataLength = source->ConfigurationDataLength;
            rc95Components[componentIndex].ParentIndex = source->ParentIndex;
        }
        memset(&rc95Input, 0, sizeof(rc95Input));
        rc95Input.StructureSize = sizeof(rc95Input);
        rc95Input.Version = RC95LDR_RC95_LOADERSTATE_VERSION;
        rc95Input.Components = rc95Components;
        rc95Input.ComponentCount = result->PlatformInventory.ComponentCount;
        rc95Input.PhysicalMemory = request->Rc95LoaderStateInput->PhysicalMemory;
        status = Rc95LdrBuildRc95LoaderState(&rc95Input, &rc95LoaderState);
        if (status != Rc95LdrSuccess)
            return ReportNativePrepareFailure(request,result,
                "building the RC95-owned hardware configuration tree",status);
        configurationRoot = rc95LoaderState.ConfigurationRoot;
        if (!configurationRoot) {
            Rc95LdrReleaseRc95LoaderState(&rc95LoaderState);
            return Rc95LdrInvalidParameter;
        }
        status = Rc95LdrReserveMemoryRange(&reservationSet,
            rc95LoaderState.Allocation.PhysicalBase,
            rc95LoaderState.Allocation.PageCount * RC95LDR_PHYSMEM_PAGE_SIZE,
            LoaderMemoryData);
        if (status != Rc95LdrSuccess) {
            Rc95LdrReleaseRc95LoaderState(&rc95LoaderState);
            return ReportNativePrepareFailure(request,result,
                "reserving memory for the RC95 configuration tree",status);
        }
    } else if (request->Profile->OsFamily == Rc95LdrOsWindowsNtLegacy ||
               request->Profile->OsFamily == Rc95LdrOsWindowsModern) {
        RC95LDR_LOADERSTATE_INPUT stateInput;
        if (!request->LoaderStateInput) return Rc95LdrInvalidParameter;
        stateInput = *request->LoaderStateInput;
        stateInput.Components = result->PlatformInventory.Components;
        stateInput.ComponentCount = result->PlatformInventory.ComponentCount;
        if (!stateInput.PhysicalMemory) return Rc95LdrInvalidParameter;
        stateInput.NtVersion = SelectNtVersion(request->Profile);
        stateInput.OsMajorVersion = request->Profile->OsMajorVersion;
        stateInput.OsMinorVersion = request->Profile->OsMinorVersion;
        status = Rc95LdrBuildNtLoaderState(&stateInput, &loaderState);
        if (status != Rc95LdrSuccess)
            return ReportNativePrepareFailure(request,result,
                "building the Windows NT loader state and configuration tree",status);
        if (!loaderState.ConfigurationRoot) {
            Rc95LdrReleaseNtLoaderState(&loaderState);
            return Rc95LdrInvalidParameter;
        }
        status = Rc95LdrReserveMemoryRange(&reservationSet,
            loaderState.Allocation.PhysicalBase,
            loaderState.Allocation.PageCount * RC95LDR_PHYSMEM_PAGE_SIZE,
            LoaderMemoryData);
        if (status != Rc95LdrSuccess) {
            Rc95LdrReleaseNtLoaderState(&loaderState);
            return ReportNativePrepareFailure(request,result,
                "reserving memory for the NT configuration tree",status);
        }
        configurationRoot = loaderState.ConfigurationRoot;
    }
    input.ConfigurationRoot = (RC95LDR_NT_PTR)configurationRoot;
    status = Rc95LdrBuildFinalMemoryMap(request->FirmwareMemoryRanges,
        request->FirmwareMemoryRangeCount, reservationSet.Entries,
        reservationSet.Count, &result->FinalMemoryMap);
    if (status != Rc95LdrSuccess) {
        Rc95LdrReleaseNtLoaderState(&loaderState);
        Rc95LdrReleaseRc95LoaderState(&rc95LoaderState);
        return ReportNativePrepareFailure(request,result,"constructing the final firmware and loader memory map",status);
    }
    input.MemoryRanges = (const RC95LDR_MEMORY_RANGE *)result->FinalMemoryMap.Ranges;
    input.MemoryRangeCount = result->FinalMemoryMap.RangeCount;
    if(request->Profile->OsFamily==Rc95LdrOsWindowsNtLegacy){
        input.NtVersion=SelectNtVersion(request->Profile);
        status=Rc95LdrBuildNtLegacyLoaderBlock(&input,request->LoaderBlockBuffer,
            request->LoaderBlockCapacity,&blockSize);
    }else if(request->Profile->OsFamily==Rc95LdrOsWindowsModern){
        input.NtVersion=SelectNtVersion(request->Profile);
        status=Rc95LdrBuildWindowsModernLoaderBlock(&input,request->LoaderBlockBuffer,
            request->LoaderBlockCapacity,&blockSize);
    } else {
        memset(&rc95BlockInput, 0, sizeof(rc95BlockInput));
        rc95BlockInput.StructureSize = sizeof(rc95BlockInput);
        rc95BlockInput.Version = RC95LDR_RC95_LOADER_INPUT_VERSION;
        rc95BlockInput.OsMajorVersion = input.OsMajorVersion;
        rc95BlockInput.OsMinorVersion = input.OsMinorVersion;
        rc95BlockInput.OsBuildNumber = input.OsBuildNumber;
        rc95BlockInput.Architecture = (uint32_t)input.Architecture;
        rc95BlockInput.MachineType = input.MachineType;
        rc95BlockInput.FirmwareIsUefi = input.FirmwareIsUefi;
        rc95BlockInput.FirmwareVersion = input.FirmwareVersion;
        rc95BlockInput.FirmwareSystemTable = input.FirmwareSystemTable;
        rc95BlockInput.FirmwareServices = input.VirtualEfiRuntimeServices;
        rc95BlockInput.FirmwareMap = input.FirmwareMapAddress;
        rc95BlockInput.FirmwareMapSize = input.FirmwareMapSize;
        rc95BlockInput.FirmwareMapDescriptorSize = input.FirmwareMapDescriptorSize;
        rc95BlockInput.RegistryLength = input.RegistryLength;
        rc95BlockInput.SystemHive = input.RegistryBase;
        rc95BlockInput.ModuleCount = input.ModuleCount;
        rc95BlockInput.Modules = input.Modules;
        rc95BlockInput.MemoryRangeCount = input.MemoryRangeCount;
        rc95BlockInput.MemoryRanges = input.MemoryRanges;
        rc95BlockInput.BootDevice = input.ArcBootDevice;
        rc95BlockInput.HalDevice = input.ArcHalDevice;
        rc95BlockInput.SystemRoot = input.NtBootPath;
        rc95BlockInput.LoadOptions = input.LoadOptions;
        rc95BlockInput.ConfigurationRoot = (uint64_t)input.ConfigurationRoot;
        rc95BlockInput.NlsData = (uint64_t)input.NlsData;
        rc95BlockInput.ArcDiskInformation = (uint64_t)input.ArcDiskInformation;
        status = Rc95LdrBuildRc95NativeLoaderBlock(&rc95BlockInput,
            request->LoaderBlockBuffer, request->LoaderBlockCapacity, &blockSize);
    }
    if(status!=Rc95LdrSuccess){Rc95LdrReleaseNtLoaderState(&loaderState);Rc95LdrReleaseRc95LoaderState(&rc95LoaderState);return ReportNativePrepareFailure(request,result,"building the selected profile loader block",status);}
    result->LoaderBlock=request->LoaderBlockBuffer;
    result->LoaderBlockSize=blockSize;
    result->LoadedBootDriverCount=state.Count-bootResourceAdded;
    if (request->Profile->OsFamily == Rc95LdrOsRc95)
        result->Rc95LoaderState = rc95LoaderState;
    else
        result->LoaderState = loaderState;
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
    handoff.OsMajorVersion=request->Profile->OsMajorVersion;
    handoff.OsMinorVersion=request->Profile->OsMinorVersion;
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
    handoff.RequiredRequirements = (request->Profile->OsFamily == Rc95LdrOsRc95) ?
        RC95LDR_HANDOFF_RC95_REQUIRED : RC95LDR_HANDOFF_REQUIRED;
    if(input.Architecture==Rc95LdrNtArchPowerPc||input.Architecture==Rc95LdrNtArchArm)
        handoff.RequiredRequirements&=~RC95LDR_HANDOFF_PROCESSOR_CONTROL;
    if(request->Profile->OsFamily==Rc95LdrOsWindowsNtLegacy &&
       request->Profile->OsMajorVersion==4)
        handoff.RequiredRequirements&=~RC95LDR_HANDOFF_LOADER_EXTENSION;
    if (request->RamdiskPhysicalBase) {
        handoff.RamdiskPhysicalBase=request->RamdiskPhysicalBase;
        handoff.RamdiskAllocationSize=request->RamdiskAllocationSize;
        handoff.RamdiskImageOffset=request->RamdiskImageOffset;
        handoff.RamdiskImageLength=request->RamdiskImageLength;
    }
    handoff.CompletedRequirements=RC95LDR_HANDOFF_KERNEL_MAPPED |
        RC95LDR_HANDOFF_HAL_MAPPED | RC95LDR_HANDOFF_SYSTEM_HIVE |
        RC95LDR_HANDOFF_BOOT_DRIVERS | RC95LDR_HANDOFF_LOADER_BLOCK;
    if(input.MemoryRangeCount)handoff.CompletedRequirements|=RC95LDR_HANDOFF_MEMORY_MAP;
    if (request->RamdiskPhysicalBase)
        handoff.CompletedRequirements|=RC95LDR_HANDOFF_RAMDISK;
    if(input.ConfigurationRoot) {
        handoff.ConfigurationRoot=input.ConfigurationRoot;
        handoff.CompletedRequirements|=RC95LDR_HANDOFF_CONFIGURATION_TREE;
    }
    if(input.NlsData){handoff.NlsData=input.NlsData;handoff.CompletedRequirements|=RC95LDR_HANDOFF_NLS;}
    if(input.ArcDiskInformation){handoff.ArcDiskInformation=input.ArcDiskInformation;handoff.CompletedRequirements|=RC95LDR_HANDOFF_ARC_DISK_INFO;}
    /* No temporary extension prefix is fabricated. Non-NT4 handoff remains
       incomplete until the exact target-generation extension ABI is built. */
    if (request->Profile->OsFamily == Rc95LdrOsRc95 &&
        !request->ArchitectureHandoffInput) {
        ReleaseProfileLoaderState(result);
        memset(&loaderState, 0, sizeof(loaderState));
        memset(&rc95LoaderState, 0, sizeof(rc95LoaderState));
        return ReportNativePrepareFailure(request, result,
            "requiring RC95-owned stack, page tables, CPU state and register frame",
            Rc95LdrUnsupported);
    }
    if (request->ArchitectureHandoffInput) {
        const RC95LDR_ARCH_HANDOFF_INPUT *arch =
            request->ArchitectureHandoffInput;
        /* The frame must enter the exact PE-mapped kernel and receive the
         * exact loader block just built above. A platform may not substitute
         * a stale entry point or a different loader-block pointer. */
        if (arch->StructureSize < sizeof(*arch) ||
            arch->Version != RC95LDR_ARCH_HANDOFF_INPUT_VERSION ||
            arch->Architecture != input.Architecture ||
            arch->KernelEntry != kernelModule->EntryPoint ||
            arch->LoaderBlockArgument !=
                (uint64_t)(uintptr_t)request->LoaderBlockBuffer) {
            ReleaseProfileLoaderState(result);
            memset(&loaderState, 0, sizeof(loaderState));
            memset(&rc95LoaderState, 0, sizeof(rc95LoaderState));
            return ReportNativePrepareFailure(request, result,
                "binding the architecture register frame to the mapped kernel and native loader block",
                Rc95LdrInvalidImage);
        }
        status = Rc95LdrPrepareArchitectureHandoff(arch, &handoff);
        if (status != Rc95LdrSuccess) {
            ReleaseProfileLoaderState(result);
            memset(&loaderState, 0, sizeof(loaderState));
            memset(&rc95LoaderState, 0, sizeof(rc95LoaderState));
            return ReportNativePrepareFailure(request, result,
                "preparing architecture-specific processor and firmware state",
                status);
        }
    }
    if(request->Profile->Firmware==Rc95LdrFirmwareUefi && request->FirmwareSystemTable &&
       input.VirtualEfiRuntimeServices && input.FirmwareMapAddress && input.FirmwareMapSize &&
       input.FirmwareMapDescriptorSize && input.FirmwareMapSize%input.FirmwareMapDescriptorSize==0)
        handoff.CompletedRequirements|=RC95LDR_HANDOFF_FIRMWARE_STATE;
    if (request->Profile->Firmware == Rc95LdrFirmwareBios &&
        request->BiosFirmwareStateAddress) {
        handoff.BiosFirmwareStateAddress = request->BiosFirmwareStateAddress;
        handoff.BiosFirmwareStateSize = request->BiosFirmwareStateSize;
        handoff.BiosFirmwareStateVersion = request->BiosFirmwareStateVersion;
        handoff.CompletedRequirements |= RC95LDR_HANDOFF_FIRMWARE_STATE;
    }
    status=Rc95LdrValidateKernelHandoff(&handoff);
    if(status!=Rc95LdrSuccess&&status!=Rc95LdrUnsupported){Rc95LdrReleaseNtLoaderState(&result->LoaderState);Rc95LdrReleaseRc95LoaderState(&result->Rc95LoaderState);memset(&loaderState,0,sizeof(loaderState));memset(&rc95LoaderState,0,sizeof(rc95LoaderState));return ReportNativePrepareFailure(request,result,"validating the final kernel handoff contract",status);}
    *request->MemoryReservations = reservationSet;
    result->Handoff=handoff;
    result->HandoffReady=(status==Rc95LdrSuccess)?1u:0u;
    return Rc95LdrSuccess;
}

void Rc95LdrReleaseNativePrepareResult(RC95LDR_NATIVE_PREPARE_RESULT *result)
{
    if(!result)return;
    ReleaseProfileLoaderState(result);
    result->HandoffReady=0;
    result->Handoff.CompletedRequirements=0;
    result->LoaderBlock=NULL;
    result->LoaderBlockSize=0;
    memset(result->DriverRegistryPaths,0,sizeof(result->DriverRegistryPaths));
}

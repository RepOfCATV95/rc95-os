/*
* PROJECT:         ReactOS Kernel
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            ntoskrnl/include/internal/po.h
* PURPOSE:         Internal header for the Power Manager
* PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
*/

#include <guiddef.h>
#include <poclass.h>
#include <reactos/hiberfil.h>

//
// Define this if you want debugging support
//
#define _PO_DEBUG_                                      0x00

//
// These define the Debug Masks Supported
//
#define PO_STATE_DEBUG                                  0x01

//
// Debug/Tracing support
//
#if _PO_DEBUG_
#ifdef NEW_DEBUG_SYSTEM_IMPLEMENTED // enable when Debug Filters are implemented
#define POTRACE DbgPrintEx
#else
#define POTRACE(x, ...)                                 \
    if (x & PopTraceLevel) DbgPrint(__VA_ARGS__)
#endif
#else
#define POTRACE(x, fmt, ...) DPRINT(fmt, ##__VA_ARGS__)
#endif

typedef enum _POP_POLICY_DEVICE_TYPE
{
    PolicyDeviceSystemButton = 0,
    PolicyDeviceThermalZone = 1,
    PolicyDeviceBattery = 2,
    PolicyDeviceMemory = 3,
    PolicyInitiatePowerActionAPI = 4,
    PolicySetPowerStateAPI = 5,
    PolicyImmediateDozeS4 = 6,
    PolicySystemIdle = 7,
    PolicyDeviceMax = 8,
} POP_POLICY_DEVICE_TYPE;

typedef struct _POP_HIBER_CONTEXT
{
    BOOLEAN WriteToFile;
    BOOLEAN ReserveLoaderMemory;
    BOOLEAN ReserveFreeMemory;
    BOOLEAN VerifyOnWake;
    BOOLEAN Reset;
    UCHAR HiberFlags;
    BOOLEAN LinkFile;
    HANDLE LinkFileHandle;
    PKSPIN_LOCK Lock;
    BOOLEAN MapFrozen;
    RTL_BITMAP MemoryMap;
    LIST_ENTRY ClonedRanges;
    ULONG ClonedRangeCount;
    PLIST_ENTRY NextCloneRange;
    PFN_NUMBER NextPreserve;
    PMDL LoaderMdl;
    PMDL Clones;
    PUCHAR NextClone;
    ULONG NoClones;
    PMDL Spares;
    ULONGLONG PagesOut;
    PVOID IoPage;
    PVOID CurrentMcb;
    PVOID DumpStack;
    PKPROCESSOR_STATE WakeState;
    ULONG NoRanges;
    ULONG_PTR HiberVa;
    PHYSICAL_ADDRESS HiberPte;
    NTSTATUS Status;
    PPO_MEMORY_IMAGE MemoryImage;
    PPO_MEMORY_RANGE_ARRAY TableHead;
    PVOID CompressionWorkspace;
    PUCHAR CompressedWriteBuffer;
    PULONG PerformanceStats;
    PVOID CompressionBlock;
    PVOID DmaIO;
    PVOID TemporaryHeap;
    PO_HIBER_PERF PerfInfo;
    PO_HIBER_IMAGE_HEADER ImageHeader;
    ULONG HiberPercentComplete;
    BOOLEAN HiberStatusScreenInitialized;
    volatile ULONG ResumeFlag;
} POP_HIBER_CONTEXT, *PPOP_HIBER_CONTEXT;

NTSTATUS
NTAPI
PopHiberSystemHandler(
    _In_ PVOID SystemContext
);

NTSTATUS
NTAPI
PopHiberFinished(
    _In_ NTSTATUS Status
);

VOID
NTAPI
PopHiberUpdateComplete(
    _In_ ULONG Percentage
);

NTSTATUS
NTAPI
PopSaveHiberContextStates(
    _Inout_ PPOP_HIBER_CONTEXT Context
);

NTSTATUS
NTAPI
PopHiberWriteImageHeader(
    _Inout_ PPOP_HIBER_CONTEXT Context
);

NTSTATUS
NTAPI
PopHiberOpenFile(
    _In_ ULONG64 Size
);

VOID
NTAPI
PopHiberCloseFile(
    VOID
);

VOID
NTAPI
PopHiberReadPolicy(
    _Out_ PULONG Enabled,
    _Out_ PULONG Percent
);

VOID
NTAPI
PopHiberInvalidate(
    VOID
);

VOID
NTAPI
PopHiberRelease(
    VOID
);

typedef struct _PO_NOTIFY_ORDER_LEVEL
{
    KEVENT LevelReady;
    ULONG DeviceCount;
    ULONG ActiveCount;
    LIST_ENTRY WaitSleep;
    LIST_ENTRY ReadySleep;
    LIST_ENTRY Pending;
    LIST_ENTRY Complete;
    LIST_ENTRY ReadyS0;
    LIST_ENTRY WaitS0;
} PO_NOTIFY_ORDER_LEVEL, *PPO_NOTIFY_ORDER_LEVEL;

typedef struct _POP_SHUTDOWN_BUG_CHECK
{
    HANDLE ThreadHandle;
    HANDLE ThreadId;
    HANDLE ProcessId;
    ULONG Code;
    ULONG_PTR Parameter1;
    ULONG_PTR Parameter2;
    ULONG_PTR Parameter3;
    ULONG_PTR Parameter4;
} POP_SHUTDOWN_BUG_CHECK, *PPOP_SHUTDOWN_BUG_CHECK;

typedef struct _POP_DEVICE_POWER_IRP
{
    SINGLE_LIST_ENTRY Free;
    PIRP Irp;
    PPO_DEVICE_NOTIFY Notify;
    LIST_ENTRY Pending;
    LIST_ENTRY Complete;
    LIST_ENTRY Abort;
    LIST_ENTRY Failed;
} POP_DEVICE_POWER_IRP, *PPOP_DEVICE_POWER_IRP;

typedef struct _PO_DEVICE_NOTIFY_ORDER
{
    ULONG DevNodeSequence;
    PDEVICE_OBJECT *WarmEjectPdoPointer;
    PO_NOTIFY_ORDER_LEVEL OrderLevel[8];
} PO_DEVICE_NOTIFY_ORDER, *PPO_DEVICE_NOTIFY_ORDER;

typedef struct _POP_DEVICE_SYS_STATE
{
    UCHAR IrpMinor;
    SYSTEM_POWER_STATE SystemState;
    PKEVENT Event;
    KSPIN_LOCK SpinLock;
    PKTHREAD Thread;
    BOOLEAN GetNewDeviceList;
    PO_DEVICE_NOTIFY_ORDER Order;
    NTSTATUS Status;
    PDEVICE_OBJECT FailedDevice;
    BOOLEAN Waking;
    BOOLEAN Cancelled;
    BOOLEAN IgnoreErrors;
    BOOLEAN IgnoreNotImplemented;
    BOOLEAN _WaitAny;
    BOOLEAN _WaitAll;
    LIST_ENTRY PresentIrpQueue;
    POP_DEVICE_POWER_IRP Head;
    POP_DEVICE_POWER_IRP PowerIrpState[20];
} POP_DEVICE_SYS_STATE, *PPOP_DEVICE_SYS_STATE;

typedef struct _POP_POWER_ACTION
{
    UCHAR Updates;
    UCHAR State;
    BOOLEAN Shutdown;
    POWER_ACTION Action;
    SYSTEM_POWER_STATE LightestState;
    ULONG Flags;
    NTSTATUS Status;
    UCHAR IrpMinor;
    SYSTEM_POWER_STATE SystemState;
    SYSTEM_POWER_STATE NextSystemState;
    PPOP_SHUTDOWN_BUG_CHECK ShutdownBugCode;
    PPOP_DEVICE_SYS_STATE DevState;
    PPOP_HIBER_CONTEXT HiberContext;
    ULONGLONG WakeTime;
    ULONGLONG SleepTime;
} POP_POWER_ACTION, *PPOP_POWER_ACTION;

typedef enum _POP_DEVICE_IDLE_TYPE
{
    DeviceIdleNormal,
    DeviceIdleDisk,
} POP_DEVICE_IDLE_TYPE, *PPOP_DEVICE_IDLE_TYPE;

typedef struct _POWER_CHANNEL_SUMMARY
{
    ULONG Signature;
    ULONG TotalCount;
    ULONG D0Count;
    LIST_ENTRY NotifyList;
} POWER_CHANNEL_SUMMARY, *PPOWER_CHANNEL_SUMMARY;

typedef struct  _DEVICE_OBJECT_POWER_EXTENSION
{
    ULONG IdleCount;
    ULONG ConservationIdleTime;
    ULONG PerformanceIdleTime;
    PDEVICE_OBJECT DeviceObject;
    LIST_ENTRY IdleList;
    DEVICE_POWER_STATE State;
    LIST_ENTRY NotifySourceList;
    LIST_ENTRY NotifyTargetList;
    POWER_CHANNEL_SUMMARY PowerChannelSummary;
    LIST_ENTRY Volume;
} DEVICE_OBJECT_POWER_EXTENSION, *PDEVICE_OBJECT_POWER_EXTENSION;

typedef struct _POP_SHUTDOWN_WAIT_ENTRY
{
    struct _POP_SHUTDOWN_WAIT_ENTRY *NextEntry;
    PETHREAD Thread;
} POP_SHUTDOWN_WAIT_ENTRY, *PPOP_SHUTDOWN_WAIT_ENTRY;

//
// Initialization routines
//
CODE_SEG("INIT")
BOOLEAN
NTAPI
PoInitSystem(
    _In_ ULONG BootPhase
);

CODE_SEG("INIT")
VOID
NTAPI
PoInitializePrcb(
    _In_ PKPRCB Prcb
);

VOID
NTAPI
PopInitShutdownList(
    VOID
);

//
// I/O Routines
//
VOID
NTAPI
PoInitializeDeviceObject(
    _Inout_ PDEVOBJ_EXTENSION DeviceObjectExtension
);

VOID
NTAPI
PoVolumeDevice(
    _In_ PDEVICE_OBJECT DeviceObject
);

VOID
NTAPI
PoRemoveVolumeDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

//
// Power State routines
//
NTSTATUS
NTAPI
PopSetSystemPowerState(
    _In_ SYSTEM_POWER_STATE PowerState,
    _In_ POWER_ACTION PowerAction
);

NTSTATUS
NTAPI
PopEnterSleepState(
    _In_ SYSTEM_POWER_STATE SystemState
);

VOID
NTAPI
PopCleanupPowerState(
    _Inout_ PUCHAR PowerState
);

NTSTATUS
NTAPI
PopAddRemoveSysCapsCallback(
    _In_ PVOID NotificationStructure,
    _In_ PVOID Context
);

//
// Notifications
//
VOID
NTAPI
PoNotifySystemTimeSet(
    VOID
);

//
// Shutdown routines
//
VOID
NTAPI
PopReadShutdownPolicy(
    VOID
);

VOID
NTAPI
PopGracefulShutdown(
    _In_opt_ PVOID Context
);

VOID
NTAPI
PopFlushVolumes(
    _In_ BOOLEAN ShuttingDown
);

//
// Global data inside the Power Manager
//
extern PDEVICE_NODE PopSystemPowerDeviceNode;
extern PKWIN32_POWERSTATE_CALLOUT PopStateCallout;
extern KGUARDED_MUTEX PopVolumeLock;
extern LIST_ENTRY PopVolumeDevices;
extern KSPIN_LOCK PopDopeGlobalLock;
extern POP_POWER_ACTION PopAction;
extern SYSTEM_POWER_CAPABILITIES PopCapabilities;
extern PPOP_HIBER_CONTEXT PopHiberContext;


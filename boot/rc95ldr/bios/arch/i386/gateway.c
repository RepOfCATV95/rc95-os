#include "../../include/rc95ldrbios.h"
#include <string.h>

/* The trampoline and its data live below 1 MiB at fixed, RC95LDR-owned
 * physical addresses. Startup must reserve these ranges before allocation. */
#define RC95LDR_BIOS_GATEWAY_PHYSICAL 0x5000u
#define RC95LDR_BIOS_GATEWAY_LIMIT   0x1000u
#define RC95LDR_BIOS_PACKET_PHYSICAL 0x6000u
#define RC95LDR_BIOS_PACKET_LENGTH   0x0060u

extern const uint8_t Rc95LdrI386GatewayImageStart[];
extern const uint8_t Rc95LdrI386GatewayImageEnd[];
extern void Rc95LdrI386InvokeGateway(void);
extern int Rc95LdrI386GatewayCheckMode(void);


/*
 * Native BIOS services use the identity map established by the real-mode
 * stage. The rejecting interrupt stub exists only so the service descriptor
 * passes validation while the gateway image is installed; the initializer
 * never exposes it as a usable BIOS interrupt backend.
 */
static RC95LDR_STATUS
Rc95LdrBiosNativeMapLowMemory(void *Context, uint32_t PhysicalAddress,
                              size_t Length, void **Mapping)
{
    uint64_t End = (uint64_t)PhysicalAddress + (uint64_t)Length;
    (void)Context;
    if (!Mapping || !Length || End > 0x100000u)
        return Rc95LdrOutOfBounds;
    *Mapping = (void *)(uintptr_t)PhysicalAddress;
    return Rc95LdrSuccess;
}

static void
Rc95LdrBiosNativeUnmapLowMemory(void *Context, void *Mapping, size_t Length)
{
    (void)Context;
    (void)Mapping;
    (void)Length;
}

static RC95LDR_STATUS
Rc95LdrBiosNativeInterruptUnavailable(void *Context, uint8_t Interrupt,
                                      RC95LDR_BIOS_REGISTERS *Registers)
{
    (void)Context;
    (void)Interrupt;
    (void)Registers;
    return Rc95LdrUnsupported;
}

RC95LDR_STATUS
Rc95LdrBiosInitializeNativeServices(RC95LDR_BIOS_SERVICES *Services,
                                    uint8_t BootDrive)
{
    if (!Services) return Rc95LdrInvalidParameter;
    memset(Services, 0, sizeof(*Services));
    Services->StructureSize = sizeof(*Services);
    Services->Version = RC95LDR_BIOS_ABI_VERSION;
    Services->InvokeInterrupt = Rc95LdrBiosNativeInterruptUnavailable;
    Services->MapLowMemory = Rc95LdrBiosNativeMapLowMemory;
    Services->UnmapLowMemory = Rc95LdrBiosNativeUnmapLowMemory;
    Services->ScratchPhysical = 0x8000u;
    Services->ScratchLength = 0x10000u;
    Services->BootDrive = BootDrive;
    return Rc95LdrBiosValidateServices(Services);
}

static volatile uint32_t Rc95LdrGatewayReady;
static RC95LDR_BIOS_SERVICES *Rc95LdrGatewayServices;

static RC95LDR_STATUS
Rc95LdrBiosInstallRealModeGateway(const RC95LDR_BIOS_SERVICES *Services)
{
    size_t ImageLength;
    void *Mapping = NULL;
    RC95LDR_STATUS Status = Rc95LdrBiosValidateServices(Services);

    if (Status != Rc95LdrSuccess) return Status;
    ImageLength = (size_t)(Rc95LdrI386GatewayImageEnd -
                           Rc95LdrI386GatewayImageStart);
    if (!ImageLength || ImageLength > RC95LDR_BIOS_GATEWAY_LIMIT)
        return Rc95LdrBufferTooSmall;

    Status = Services->MapLowMemory(Services->Context,
        RC95LDR_BIOS_GATEWAY_PHYSICAL, RC95LDR_BIOS_GATEWAY_LIMIT, &Mapping);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Mapping) return Rc95LdrInvalidParameter;
    memcpy(Mapping, Rc95LdrI386GatewayImageStart, ImageLength);
    if (ImageLength < RC95LDR_BIOS_GATEWAY_LIMIT)
        memset((uint8_t *)Mapping + ImageLength, 0x90,
               RC95LDR_BIOS_GATEWAY_LIMIT - ImageLength);
    Services->UnmapLowMemory(Services->Context, Mapping,
                             RC95LDR_BIOS_GATEWAY_LIMIT);
    Rc95LdrGatewayReady = 1;
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS
Rc95LdrBiosRealModeInvoke(void *Context, uint8_t Interrupt,
    RC95LDR_BIOS_REGISTERS *Registers)
{
    const RC95LDR_BIOS_SERVICES *Services = Rc95LdrGatewayServices;
    (void)Context;
    uint8_t *Packet;
    void *Mapping = NULL;
    RC95LDR_STATUS Status;

    if (!Registers || !Rc95LdrGatewayReady)
        return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosValidateServices(Services);
    if (Status != Rc95LdrSuccess) return Status;
    Status = Services->MapLowMemory(Services->Context,
        RC95LDR_BIOS_PACKET_PHYSICAL, RC95LDR_BIOS_PACKET_LENGTH, &Mapping);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Mapping) return Rc95LdrInvalidParameter;

    Packet = (uint8_t *)Mapping;
    memset(Packet, 0, RC95LDR_BIOS_PACKET_LENGTH);
    memcpy(Packet, Registers, sizeof(*Registers));
    Packet[sizeof(*Registers)] = Interrupt;
    Services->UnmapLowMemory(Services->Context, Mapping,
                             RC95LDR_BIOS_PACKET_LENGTH);

    /* The thunk executes with interrupts masked and restores the caller's
     * GDT, IDT, segment registers, stack, and general registers on return. */
    Rc95LdrI386InvokeGateway();

    Mapping = NULL;
    Status = Services->MapLowMemory(Services->Context,
        RC95LDR_BIOS_PACKET_PHYSICAL, RC95LDR_BIOS_PACKET_LENGTH, &Mapping);
    if (Status != Rc95LdrSuccess) return Status;
    if (!Mapping) return Rc95LdrInvalidParameter;
    memcpy(Registers, Mapping, sizeof(*Registers));
    Services->UnmapLowMemory(Services->Context, Mapping,
                             RC95LDR_BIOS_PACKET_LENGTH);
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosEnableNativeGateway(RC95LDR_BIOS_SERVICES *Services)
{
    RC95LDR_STATUS Status;
    if (!Services) return Rc95LdrInvalidParameter;
    if (!Rc95LdrI386GatewayCheckMode()) return Rc95LdrUnsupported;
    {
        RC95LDR_BIOS_INVOKE_INTERRUPT Previous = Services->InvokeInterrupt;
        Rc95LdrGatewayServices = Services;
        Services->InvokeInterrupt = Rc95LdrBiosRealModeInvoke;
        Status = Rc95LdrBiosInstallRealModeGateway(Services);
        if (Status != Rc95LdrSuccess) {
            Services->InvokeInterrupt = Previous;
            Rc95LdrGatewayServices = NULL;
            return Status;
        }
    }
    return Rc95LdrSuccess;
}

RC95LDR_STATUS
Rc95LdrBiosPrepareNativePlatform(RC95LDR_BIOS_SERVICES *Services,
    RC95LDR_MEMORY_RESERVATION_SET *Reservations)
{
    RC95LDR_STATUS Status;
    if (!Services || !Reservations) return Rc95LdrInvalidParameter;
    Status = Rc95LdrBiosAddMemoryReservations(Services, Reservations);
    if (Status != Rc95LdrSuccess) return Status;
    return Rc95LdrBiosEnableNativeGateway(Services);
}

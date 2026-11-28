#include "rc95ldrrc95loaderstate.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>

#define RC95LDR_RC95_CONFIG_IDENTIFIER_MAX 255u

static size_t
AlignRc95(size_t Value, size_t Alignment)
{
    if (Value > SIZE_MAX - (Alignment - 1u))
        return SIZE_MAX;
    return (Value + Alignment - 1u) & ~(Alignment - 1u);
}

static int
AddRc95(size_t *Value, size_t Amount)
{
    if (*Value > SIZE_MAX - Amount)
        return 0;
    *Value += Amount;
    return 1;
}

void
Rc95LdrReleaseRc95LoaderState(RC95LDR_RC95_LOADERSTATE_RESULT *Result)
{
    if (!Result)
        return;
    if (Result->OwnsAllocation)
        Rc95LdrPhysMemRelease(&Result->PhysicalMemory, &Result->Allocation);
    memset(Result, 0, sizeof(*Result));
}

RC95LDR_STATUS
Rc95LdrBuildRc95LoaderState(
    const RC95LDR_RC95_LOADERSTATE_INPUT *Input,
    RC95LDR_RC95_LOADERSTATE_RESULT *Output)
{
    RC95LDR_RC95_LOADERSTATE_RESULT Temporary;
    size_t Strings = 0, Data = 0, NodesOffset, Required, i;
    uint64_t Pages, AllocationBytes;
    uint32_t RootCount = 0;
    uint8_t *Buffer;
    RC95LDR_RC95_CONFIG_NODE *Nodes;
    RC95LDR_STATUS Status;

    if (!Input || !Output ||
        Input->StructureSize < sizeof(*Input) ||
        Input->Version != RC95LDR_RC95_LOADERSTATE_VERSION ||
        !Input->PhysicalMemory ||
        !Input->ComponentCount ||
        Input->ComponentCount > RC95LDR_RC95_CONFIG_COMPONENT_MAX ||
        !Input->Components)
        return Rc95LdrInvalidParameter;

    /* Validate the complete tree and calculate its storage before allocating.
       Parent indices must precede children, which makes cycles impossible. */
    for (i = 0; i < Input->ComponentCount; ++i)
    {
        const RC95LDR_RC95_CONFIG_COMPONENT_INPUT *Component = &Input->Components[i];
        size_t IdentifierLength = 0;

        if (!Component->Identifier ||
            Component->ParentIndex >= (int32_t)i ||
            Component->ParentIndex < -1 ||
            Component->ConfigurationDataLength > 65536u ||
            (Component->ConfigurationDataLength && !Component->ConfigurationData))
            return Rc95LdrInvalidParameter;

        while (IdentifierLength <= RC95LDR_RC95_CONFIG_IDENTIFIER_MAX &&
               Component->Identifier[IdentifierLength])
            ++IdentifierLength;
        if (!IdentifierLength ||
            IdentifierLength > RC95LDR_RC95_CONFIG_IDENTIFIER_MAX)
            return Rc95LdrInvalidParameter;

        if (Component->ParentIndex < 0)
            ++RootCount;

        if (!AddRc95(&Strings, IdentifierLength + 1u))
            return Rc95LdrIntegerOverflow;
        Data = AlignRc95(Data, 8u);
        if (Data == SIZE_MAX ||
            !AddRc95(&Data, Component->ConfigurationDataLength))
            return Rc95LdrIntegerOverflow;
    }

    if (RootCount != 1u)
        return Rc95LdrInvalidParameter;

    NodesOffset = AlignRc95(Strings, 8u);
    if (NodesOffset == SIZE_MAX ||
        Input->ComponentCount > (SIZE_MAX - NodesOffset) / sizeof(*Nodes))
        return Rc95LdrIntegerOverflow;

    Required = AlignRc95(NodesOffset +
        (size_t)Input->ComponentCount * sizeof(*Nodes), 8u);
    if (Required == SIZE_MAX || !AddRc95(&Required, Data))
        return Rc95LdrIntegerOverflow;
    if (!Required)
        Required = 8u;
    if (Required > UINT64_MAX - (RC95LDR_PHYSMEM_PAGE_SIZE - 1u))
        return Rc95LdrIntegerOverflow;

    Pages = (Required + RC95LDR_PHYSMEM_PAGE_SIZE - 1u) /
            RC95LDR_PHYSMEM_PAGE_SIZE;
    if (Pages > UINT64_MAX / RC95LDR_PHYSMEM_PAGE_SIZE)
        return Rc95LdrIntegerOverflow;
    AllocationBytes = Pages * RC95LDR_PHYSMEM_PAGE_SIZE;

    memset(&Temporary, 0, sizeof(Temporary));
    Temporary.PhysicalMemory = *Input->PhysicalMemory;
    Status = Rc95LdrPhysMemAllocate(&Temporary.PhysicalMemory, Pages, 1u,
        RC95LDR_PHYSMEM_ALLOC_ZERO,
        RC95LDR_PHYSMEM_MAP_WRITABLE | RC95LDR_PHYSMEM_MAP_CACHED,
        &Temporary.Allocation);
    if (Status != Rc95LdrSuccess)
        return Status;
    Temporary.OwnsAllocation = 1u;

    Status = Rc95LdrPhysMemValidate(&Temporary.Allocation);
    if (Status != Rc95LdrSuccess ||
        Temporary.Allocation.MappingLength < Required ||
        Temporary.Allocation.PageCount < Pages ||
        Temporary.Allocation.PhysicalBase > UINT64_MAX - AllocationBytes)
    {
        Rc95LdrReleaseRc95LoaderState(&Temporary);
        return Status == Rc95LdrSuccess ? Rc95LdrInvalidImage : Status;
    }

    Buffer = (uint8_t *)Temporary.Allocation.WritableMapping;
    Nodes = (RC95LDR_RC95_CONFIG_NODE *)(void *)(Buffer + NodesOffset);

    {
        size_t StringOffset = 0;
        size_t DataOffset = NodesOffset +
            (size_t)Input->ComponentCount * sizeof(*Nodes);
        uint64_t Root = 0;

        for (i = 0; i < Input->ComponentCount; ++i)
        {
            const RC95LDR_RC95_CONFIG_COMPONENT_INPUT *Component =
                &Input->Components[i];
            size_t IdentifierLength = strlen(Component->Identifier);
            uint64_t NodeAddress = (uint64_t)(uintptr_t)&Nodes[i];
            uint64_t *SiblingLink;

            memcpy(Buffer + StringOffset, Component->Identifier,
                   IdentifierLength + 1u);
            Nodes[i].Identifier = (uint64_t)(uintptr_t)(Buffer + StringOffset);
            Nodes[i].IdentifierLength = (uint32_t)IdentifierLength;
            StringOffset += IdentifierLength + 1u;

            Nodes[i].Class = Component->Class;
            Nodes[i].Type = Component->Type;
            Nodes[i].Flags = Component->Flags;
            Nodes[i].Key = Component->Key;
            Nodes[i].AffinityMask = Component->AffinityMask;
            Nodes[i].Version = Component->Version;
            Nodes[i].Revision = Component->Revision;
            Nodes[i].DataLength = Component->ConfigurationDataLength;

            if (Component->ConfigurationDataLength)
            {
                DataOffset = AlignRc95(DataOffset, 8u);
                if (DataOffset == SIZE_MAX ||
                    DataOffset > Required ||
                    Component->ConfigurationDataLength > Required - DataOffset)
                {
                    Status = Rc95LdrCorruptData;
                    goto Failure;
                }
                Nodes[i].ConfigurationData =
                    (uint64_t)(uintptr_t)(Buffer + DataOffset);
                memcpy(Buffer + DataOffset, Component->ConfigurationData,
                       Component->ConfigurationDataLength);
                DataOffset += Component->ConfigurationDataLength;
            }

            if (Component->ParentIndex < 0)
            {
                Root = NodeAddress;
            }
            else
            {
                RC95LDR_RC95_CONFIG_NODE *Parent =
                    &Nodes[(uint32_t)Component->ParentIndex];
                Nodes[i].Parent = (uint64_t)(uintptr_t)Parent;
                SiblingLink = &Parent->FirstChild;
                while (*SiblingLink)
                {
                    RC95LDR_RC95_CONFIG_NODE *Sibling =
                        (RC95LDR_RC95_CONFIG_NODE *)(uintptr_t)*SiblingLink;
                    SiblingLink = &Sibling->NextSibling;
                }
                *SiblingLink = NodeAddress;
            }
        }

        if (!Root)
        {
            Status = Rc95LdrCorruptData;
            goto Failure;
        }
        Temporary.ConfigurationRoot = Root;
    }

    Temporary.BytesUsed = Required;
    *Output = Temporary;
    return Rc95LdrSuccess;

Failure:
    Rc95LdrReleaseRc95LoaderState(&Temporary);
    return Status;
}

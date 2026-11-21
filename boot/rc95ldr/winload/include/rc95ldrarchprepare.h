#ifndef _RC95LDR_ARCH_PREPARE_H_
#define _RC95LDR_ARCH_PREPARE_H_
#include "rc95ldrhandoff.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RC95LDR_ARCH_HANDOFF_INPUT_VERSION 3u
typedef struct _RC95LDR_I386_REGISTER_FRAME {
    uint32_t Eax,Ebx,Ecx,Edx,Esi,Edi,Ebp,Eip,Esp,EFlags;
    uint16_t Cs,Ss,Ds,Es,Fs,Gs;
} RC95LDR_I386_REGISTER_FRAME;
typedef struct _RC95LDR_AMD64_REGISTER_FRAME {
    uint64_t Rax,Rbx,Rcx,Rdx,Rsi,Rdi,Rbp,Rsp,R8,R9,R10,R11,R12,R13,R14,R15;
    uint64_t Rip,RFlags;
    uint16_t Cs,Ss,Ds,Es,Fs,Gs;
} RC95LDR_AMD64_REGISTER_FRAME;
typedef struct _RC95LDR_POWERPC_REGISTER_FRAME {
    uint32_t Gpr[32],Nip,Msr,Lr,Ctr,Cr,Xer;
} RC95LDR_POWERPC_REGISTER_FRAME;
typedef struct _RC95LDR_ARM_REGISTER_FRAME {
    uint32_t R[16],Cpsr;
} RC95LDR_ARM_REGISTER_FRAME;
typedef union _RC95LDR_ARCH_REGISTER_FRAME {
    RC95LDR_I386_REGISTER_FRAME I386;
    RC95LDR_AMD64_REGISTER_FRAME Amd64;
    RC95LDR_POWERPC_REGISTER_FRAME PowerPc;
    RC95LDR_ARM_REGISTER_FRAME Arm;
} RC95LDR_ARCH_REGISTER_FRAME;
typedef struct _RC95LDR_ARCH_HANDOFF_INPUT {
    uint32_t StructureSize, Version;
    RC95LDR_NT_TARGET_ARCH Architecture;
    uint64_t StackBase, StackSize, StackPointer, PageTableRoot;
    uint64_t ArchitectureState, CpuState;
    RC95LDR_PROCESSOR_CONTROL ProcessorControl;
    uint64_t KernelEntry,LoaderBlockArgument;
    uint32_t StatusRegister,Reserved;
    uint64_t GeneralRegisters[32];
    void *RegisterFrameBuffer;
    size_t RegisterFrameCapacity;
} RC95LDR_ARCH_HANDOFF_INPUT;
/* I386 carries an embedded PRCB offset within its PCR; AMD64 carries an
 * independently allocated PRCB. Both layouts are architecture-discriminated. */
RC95LDR_STATUS Rc95LdrPrepareArchitectureHandoff(
    const RC95LDR_ARCH_HANDOFF_INPUT *Input,
    RC95LDR_KERNEL_HANDOFF_CONTEXT *Context);
#ifdef __cplusplus
}
#endif
#endif

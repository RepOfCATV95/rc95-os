#ifndef _RC95LDR_RESUME_H_
#define _RC95LDR_RESUME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Non-returning architecture handoff entry points.
 *
 * The state buffer is the kernel PO_MEMORY_IMAGE/PROCESSOR_STATE image
 * validated by resume.c. The layout is intentionally the native kernel ABI:
 * I386 special-register block at 0x2cc (total 0x320 bytes), AMD64 special
 * register block at offset zero (total 0x5b0 bytes). Do not pack, translate,
 * or synthesize this structure in the loader.
 *
 * Flags use RC95LDR_HIBER_FLAG_HAS_FPU_STATE and
 * RC95LDR_HIBER_FLAG_HAS_FX_STATE from rc95ldrwinload.h.
 * These routines disable interrupts, switch to the saved address space,
 * restore descriptor/control/debug/FPU state and transfer with IRET/IRETQ.
 * They must never return.
 */

#if defined(_MSC_VER)
#define RC95LDR_RESUME_NORETURN __declspec(noreturn)
#if defined(_M_IX86)
#define RC95LDR_RESUME_STDCALL __stdcall
#else
#define RC95LDR_RESUME_STDCALL
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define RC95LDR_RESUME_NORETURN __attribute__((noreturn))
#if defined(__i386__)
#define RC95LDR_RESUME_STDCALL __attribute__((stdcall))
#else
#define RC95LDR_RESUME_STDCALL
#endif
#else
#define RC95LDR_RESUME_NORETURN
#define RC95LDR_RESUME_STDCALL
#endif

#if defined(_M_IX86) || defined(_X86_) || defined(__i386__) || defined(i386)
RC95LDR_RESUME_NORETURN void RC95LDR_RESUME_STDCALL
Rc95LdrI386ResumeTrampoline(const void *ProcessorState, uint32_t Flags);
#elif defined(_M_AMD64) || defined(_M_X64) || defined(_AMD64_) || defined(__x86_64__) || defined(__amd64__)
RC95LDR_RESUME_NORETURN void
Rc95LdrAmd64ResumeTrampoline(const void *ProcessorState, uint32_t Flags);
#endif

#undef RC95LDR_RESUME_NORETURN
#undef RC95LDR_RESUME_STDCALL

#ifdef __cplusplus
}
#endif
#endif

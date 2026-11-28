#include "rc95ldrdependency.h"

/* NT 5.x through Windows 11 (NT 10.0) is the first implementation target.
 * NT 4.0 candidates remain catalogued but are deliberately marked legacy. */
#define C(file, role, phase, arch, min, max, flags) \
    { file, role, phase, arch, min, max, flags }
static const RC95LDR_DEPENDENCY_CANDIDATE Candidates[] = {
    C("ntoskrnl.exe", Rc95LdrDependencyKernel, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 4, 4, RC95LDR_DEP_FLAG_NT4_ONLY),
    C("ntoskrnl.exe", Rc95LdrDependencyKernel, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, 0),
    C("ntkrnlmp.exe", Rc95LdrDependencyKernel, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, 0),

    C("hal.dll", Rc95LdrDependencyHal, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 4, 4, RC95LDR_DEP_FLAG_NT4_ONLY),
    C("hal.dll", Rc95LdrDependencyHal, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, 0),
    C("halacpi.dll", Rc95LdrDependencyHal, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("halaacpi.dll", Rc95LdrDependencyHal, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("halmp.dll", Rc95LdrDependencyHal, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_I386, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("halmps.dll", Rc95LdrDependencyHal, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_I386, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("halapic.dll", Rc95LdrDependencyHal, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_I386, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("halmacpi.dll", Rc95LdrDependencyHal, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_I386, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),

    C("bootvid.dll", Rc95LdrDependencyBootVideo, Rc95LdrDependencyKernelSupport, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("ksecdd.sys", Rc95LdrDependencySecurity, Rc95LdrDependencyKernelSupport, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("portcls.sys", Rc95LdrDependencyAudio, Rc95LdrDependencyKernelSupport, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),

    C("pciide.sys", Rc95LdrDependencyPciIde, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_HARDWARE_MATCH | RC95LDR_DEP_FLAG_OPTIONAL),
    C("pciidex.sys", Rc95LdrDependencyPciIde, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_HARDWARE_MATCH | RC95LDR_DEP_FLAG_OPTIONAL),

    C("fastfat.sys", Rc95LdrDependencyFilesystem, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("ntfs.sys", Rc95LdrDependencyFilesystem, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("ext2.sys", Rc95LdrDependencyFilesystem, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("cdfs.sys", Rc95LdrDependencyFilesystem, Rc95LdrDependencyKernelSupport, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("udfs.sys", Rc95LdrDependencyFilesystem, Rc95LdrDependencyKernelSupport, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),
    C("btrfs.sys", Rc95LdrDependencyFilesystem, Rc95LdrDependencyKernelSupport, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL),

    C("usbohci.sys", Rc95LdrDependencyUsb, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_HARDWARE_MATCH | RC95LDR_DEP_FLAG_OPTIONAL),
    C("usbuhci.sys", Rc95LdrDependencyUsb, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_I386, 5, 10, RC95LDR_DEP_FLAG_HARDWARE_MATCH | RC95LDR_DEP_FLAG_OPTIONAL),
    C("usbehci.sys", Rc95LdrDependencyUsb, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_HARDWARE_MATCH | RC95LDR_DEP_FLAG_OPTIONAL),
    C("usbxhci.sys", Rc95LdrDependencyUsb, Rc95LdrDependencyBootCritical, RC95LDR_DEP_ARCH_ALL, 6, 10, RC95LDR_DEP_FLAG_HARDWARE_MATCH | RC95LDR_DEP_FLAG_OPTIONAL),

    C("ntdll.dll", Rc95LdrDependencyNativeRuntime, Rc95LdrDependencyUserSubsystem, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_USER_MODE),
    C("kernel32.dll", Rc95LdrDependencyWin32Runtime, Rc95LdrDependencyUserSubsystem, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_USER_MODE),
    C("win32k.sys", Rc95LdrDependencyWin32k, Rc95LdrDependencyKernelSupport, RC95LDR_DEP_ARCH_ALL, 5, 10, RC95LDR_DEP_FLAG_OPTIONAL)
};
#undef C

static int CandidateMatches(const RC95LDR_DEPENDENCY_CANDIDATE *c,
    RC95LDR_DEPENDENCY_ROLE role, uint32_t major, RC95LDR_NT_TARGET_ARCH architecture)
{
    uint8_t arch;
    if (architecture == Rc95LdrNtArchI386) arch = RC95LDR_DEP_ARCH_I386;
    else if (architecture == Rc95LdrNtArchAmd64) arch = RC95LDR_DEP_ARCH_AMD64;
    else return 0;
    return c->Role == role && major >= c->MinimumMajor &&
        major <= c->MaximumMajor && (c->ArchitectureMask & arch) != 0;
}

size_t Rc95LdrCountDependencyCandidates(RC95LDR_DEPENDENCY_ROLE role,
    uint32_t major, RC95LDR_NT_TARGET_ARCH architecture)
{
    size_t i, count = 0;
    if (major < 4 || major > 10) return 0;
    for (i = 0; i < RC95LDR_ARRAY_COUNT(Candidates); ++i)
        if (CandidateMatches(&Candidates[i], role, major, architecture)) ++count;
    return count;
}

const RC95LDR_DEPENDENCY_CANDIDATE *Rc95LdrGetDependencyCandidate(
    RC95LDR_DEPENDENCY_ROLE role, uint32_t major,
    RC95LDR_NT_TARGET_ARCH architecture, size_t index)
{
    size_t i, found = 0;
    if (major < 4 || major > 10) return NULL;
    for (i = 0; i < RC95LDR_ARRAY_COUNT(Candidates); ++i) {
        if (!CandidateMatches(&Candidates[i], role, major, architecture)) continue;
        if (found++ == index) return &Candidates[i];
    }
    return NULL;
}

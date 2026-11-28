#include "rc95ldrdependency.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    const RC95LDR_DEPENDENCY_CANDIDATE *c;
    size_t n;
    n=Rc95LdrCountDependencyCandidates(Rc95LdrDependencyKernel,5,Rc95LdrNtArchI386);
    c=Rc95LdrGetDependencyCandidate(Rc95LdrDependencyKernel,5,Rc95LdrNtArchI386,0);
    assert(n==2 && c && strcmp(c->FileName,"ntoskrnl.exe")==0);
    c=Rc95LdrGetDependencyCandidate(Rc95LdrDependencyKernel,4,Rc95LdrNtArchI386,0);
    assert(c && (c->Flags & RC95LDR_DEP_FLAG_NT4_ONLY));
    assert(Rc95LdrCountDependencyCandidates(Rc95LdrDependencyKernel,4,Rc95LdrNtArchI386)==1);
    c=Rc95LdrGetDependencyCandidate(Rc95LdrDependencyKernel,5,Rc95LdrNtArchI386,1);
    assert(c && strcmp(c->FileName,"ntkrnlmp.exe")==0);
    n=Rc95LdrCountDependencyCandidates(Rc95LdrDependencyUsb,5,Rc95LdrNtArchAmd64);
    assert(n==2 && Rc95LdrGetDependencyCandidate(Rc95LdrDependencyUsb,5,Rc95LdrNtArchAmd64,1));
    n=Rc95LdrCountDependencyCandidates(Rc95LdrDependencyUsb,6,Rc95LdrNtArchAmd64);
    assert(n==3 && Rc95LdrGetDependencyCandidate(Rc95LdrDependencyUsb,6,Rc95LdrNtArchAmd64,2));
    n=Rc95LdrCountDependencyCandidates(Rc95LdrDependencyUsb,5,Rc95LdrNtArchPowerPc);
    assert(n==0 && Rc95LdrGetDependencyCandidate(Rc95LdrDependencyUsb,5,Rc95LdrNtArchPowerPc,0)==0);
    n=Rc95LdrCountDependencyCandidates(Rc95LdrDependencyNativeRuntime,4,Rc95LdrNtArchI386);
    assert(n==0 && Rc95LdrGetDependencyCandidate(Rc95LdrDependencyNativeRuntime,4,Rc95LdrNtArchI386,0)==0);
    return 0;
}

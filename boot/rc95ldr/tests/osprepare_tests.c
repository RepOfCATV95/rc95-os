#include "rc95ldrosprepare.h"
#include <assert.h>
#include <string.h>

static unsigned RosLoadCalls;
static RC95LDR_STATUS LaunchRosLoad(void *Context,
    const RC95LDR_NATIVE_PREPARE_REQUEST *Request)
{
    (void)Context;
    assert(Request->RosLoadPath != 0);
    ++RosLoadCalls;
    return Rc95LdrIoError; /* Test callback models a reported launch failure. */
}
static void TestReactOsDelegation(void)
{
    RC95LDR_OS_IDENTITY identity;
    RC95LDR_OS_PROFILE profile;
    RC95LDR_NATIVE_PREPARE_REQUEST request;
    RC95LDR_NATIVE_PREPARE_RESULT result;
    uint16_t rosloadPath[]={'r','o','s','l','o','a','d','.','e','x','e',0};

    memset(&identity,0,sizeof(identity));
    identity.StructureSize=sizeof(identity);
    identity.Version=RC95LDR_OS_PROFILE_VERSION;
    identity.OsFamily=Rc95LdrOsReactOS;
    identity.Firmware=Rc95LdrFirmwareBios;
    identity.BootEntry=Rc95LdrEntryReactOs;
    assert(Rc95LdrSelectOsProfile(&identity,&profile)==Rc95LdrSuccess);

    memset(&request,0,sizeof(request));
    request.StructureSize=sizeof(request);
    request.Version=RC95LDR_NATIVE_PREPARE_VERSION;
    request.Profile=&profile;
    request.RosLoadPath=rosloadPath;
    request.LaunchRosLoad=LaunchRosLoad;
    memset(&result,0,sizeof(result));

    RosLoadCalls=0;
    assert(Rc95LdrPrepareOperatingSystem(&request,&result)==Rc95LdrIoError);
    assert(RosLoadCalls==1);
}
static void TestReactOsRequiresRosLoad(void)
{
    RC95LDR_OS_IDENTITY identity;
    RC95LDR_OS_PROFILE profile;
    RC95LDR_NATIVE_PREPARE_REQUEST request;
    RC95LDR_NATIVE_PREPARE_RESULT result;
    uint16_t rosloadPath[]={'r','o','s','l','o','a','d','.','e','x','e',0};

    memset(&identity,0,sizeof(identity));
    identity.StructureSize=sizeof(identity);
    identity.Version=RC95LDR_OS_PROFILE_VERSION;
    identity.OsFamily=Rc95LdrOsReactOS;
    identity.Firmware=Rc95LdrFirmwareBios;
    identity.BootEntry=Rc95LdrEntryReactOs;
    assert(Rc95LdrSelectOsProfile(&identity,&profile)==Rc95LdrSuccess);

    memset(&request,0,sizeof(request));
    request.StructureSize=sizeof(request);
    request.Version=RC95LDR_NATIVE_PREPARE_VERSION;
    request.Profile=&profile;
    request.RosLoadPath=rosloadPath;
    memset(&result,0,sizeof(result));
    assert(Rc95LdrPrepareOperatingSystem(&request,&result)==Rc95LdrUnsupported);
}
int main(void)
{
    TestReactOsDelegation();
    TestReactOsRequiresRosLoad();
    return 0;
}

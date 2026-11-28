#include "rc95ldrserviceorder.h"
#include "rc95ldrsystemhive.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>


static void TestServiceGroupOrder(void)
{
    const uint8_t groups[] = {
        'B',0,0,0, 'a',0,0,0, 'Z',0,0,0, 0,0
    };
    const uint8_t empty[] = { 0,0,0,0 };
    const uint8_t duplicate[] = { 'A',0,0,0, 'a',0,0,0, 0,0 };
    const uint8_t malformed[] = { 'A',0,0,0, 'B',0 };
    const uint8_t path[] = { '.',0,'.',0,0,0 };
    const uint8_t nonAscii[] = { 0xe9,0,0,0,0,0 };
    const uint8_t supplementary[] = { 0x3d,0xd8,0x00,0xde,0,0,0,0 };
    const uint8_t unicodeThenAscii[] = { 0xe9,0,0,0, 'A',0,0,0, 0,0 };
    const uint8_t asciiThenUnicode[] = { 'A',0,0,0, 0xe9,0,0,0, 0,0 };
    const uint8_t loneHighSurrogate[] = { 0x00,0xd8,0,0,0,0,0,0 };
    uint32_t order = 0xfeedu;

    assert(Rc95LdrFindServiceGroupOrder(groups, sizeof(groups), "b", &order)
           == Rc95LdrSuccess && order == 0);
    assert(Rc95LdrFindServiceGroupOrder(groups, sizeof(groups), "A", &order)
           == Rc95LdrSuccess && order == 1);
    assert(Rc95LdrFindServiceGroupOrder(groups, sizeof(groups), "missing", &order)
           == Rc95LdrNotFound);
    assert(order == 1); /* failures do not publish an output */
    assert(Rc95LdrFindServiceGroupOrder(empty, sizeof(empty), "A", &order)
           == Rc95LdrNotFound);
    assert(Rc95LdrFindServiceGroupOrder(duplicate, sizeof(duplicate), "A", &order)
           == Rc95LdrCorruptData);
    assert(Rc95LdrFindServiceGroupOrder(malformed, sizeof(malformed), "A", &order)
           == Rc95LdrCorruptData);
    assert(Rc95LdrFindServiceGroupOrder(path, sizeof(path), ".", &order)
           == Rc95LdrCorruptData);
    /* Valid non-ASCII groups are ignored for an unrelated ASCII lookup. */
    assert(Rc95LdrFindServiceGroupOrder(nonAscii, sizeof(nonAscii), "A", &order)
           == Rc95LdrNotFound);
    assert(Rc95LdrFindServiceGroupOrder(unicodeThenAscii,
        sizeof(unicodeThenAscii), "A", &order) == Rc95LdrSuccess && order == 1);
    assert(Rc95LdrFindServiceGroupOrder(asciiThenUnicode,
        sizeof(asciiThenUnicode), "A", &order) == Rc95LdrSuccess && order == 0);
    /* Valid supplementary UTF-16 is skipped; unpaired surrogates remain corrupt. */
    assert(Rc95LdrFindServiceGroupOrder(supplementary, sizeof(supplementary),
                                        "A", &order) == Rc95LdrNotFound);
    assert(Rc95LdrFindServiceGroupOrder(loneHighSurrogate,
                                        sizeof(loneHighSurrogate), "A", &order)
           == Rc95LdrCorruptData);
    assert(Rc95LdrFindServiceGroupOrder(groups, sizeof(groups), "", &order)
           == Rc95LdrInvalidParameter);
}

static void TestServiceTagOrder(void)
{
    /* GroupOrderList REG_BINARY: count=3, then tags 9, 2, 7. */
    const uint8_t tags[] = {
        3,0,0,0, 9,0,0,0, 2,0,0,0, 7,0,0,0
    };
    const uint8_t empty[] = { 0,0,0,0 };
    const uint8_t duplicate[] = { 2,0,0,0, 5,0,0,0, 5,0,0,0 };
    const uint8_t truncated[] = { 2,0,0,0, 5,0,0,0 };
    uint32_t order = 0xfeedu;

    assert(Rc95LdrFindServiceTagOrder(tags, sizeof(tags), 9, &order)
           == Rc95LdrSuccess && order == 0);
    assert(Rc95LdrFindServiceTagOrder(tags, sizeof(tags), 2, &order)
           == Rc95LdrSuccess && order == 1);
    assert(Rc95LdrFindServiceTagOrder(tags, sizeof(tags), 7, &order)
           == Rc95LdrSuccess && order == 2);
    assert(Rc95LdrFindServiceTagOrder(tags, sizeof(tags), 3, &order)
           == Rc95LdrNotFound && order == 2);
    assert(Rc95LdrFindServiceTagOrder(empty, sizeof(empty), 1, &order)
           == Rc95LdrNotFound);
    assert(Rc95LdrFindServiceTagOrder(duplicate, sizeof(duplicate), 5, &order)
           == Rc95LdrCorruptData);
    assert(Rc95LdrFindServiceTagOrder(truncated, sizeof(truncated), 5, &order)
           == Rc95LdrCorruptData);
    assert(Rc95LdrFindServiceTagOrder(tags, sizeof(tags) - 1u, 9, &order)
           == Rc95LdrCorruptData);
}


static uint32_t g_DependencyCount;
static char g_Dependencies[RC95LDR_SERVICE_MAX_DEPENDENCIES]
    [RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS + 1u];
static RC95LDR_STATUS CaptureDependency(void *context, const char *name)
{
    (void)context;
    assert(g_DependencyCount < RC95LDR_SERVICE_MAX_DEPENDENCIES);
    strcpy(g_Dependencies[g_DependencyCount++], name);
    return Rc95LdrSuccess;
}

static RC95LDR_STATUS StopDependency(void *context, const char *name)
{
    uint32_t *calls = (uint32_t *)context;
    assert(name && *name);
    ++*calls;
    return Rc95LdrUnsupported;
}

static void TestServiceDependencies(void)
{
    const uint8_t list[] = { 'A',0, 'l',0, 'p',0, 'h',0, 'a',0, 0,0,
                             'B',0, 'e',0, 't',0, 'a',0, 0,0, 0,0 };
    const uint8_t empty[] = { 0,0, 0,0 };
    const uint8_t duplicate[] = { 'A',0, 0,0, 'a',0, 0,0, 0,0 };
    const uint8_t malformed[] = { 'A',0, 0,0, 'B',0, 0,0 };
    const uint8_t nonAscii[] = { 0xe9,0, 0,0, 0,0 };
    const uint8_t supplementary[] = { 'S',0, 0x3d,0xd8,0x00,0xde, 0,0, 0,0 };
    const uint8_t loneLowSurrogate[] = { 'S',0, 0x00,0xde, 0,0, 0,0 };
    g_DependencyCount = 0;
    assert(Rc95LdrEnumerateServiceDependencies(list, sizeof(list),
        CaptureDependency, 0) == Rc95LdrSuccess);
    assert(g_DependencyCount == 2);
    assert(strcmp(g_Dependencies[0], "Alpha") == 0);
    assert(strcmp(g_Dependencies[1], "Beta") == 0);
    g_DependencyCount = 0;
    assert(Rc95LdrEnumerateServiceDependencies(empty, sizeof(empty),
        CaptureDependency, 0) == Rc95LdrSuccess && g_DependencyCount == 0);
    {
        uint32_t calls = 0;
        assert(Rc95LdrEnumerateServiceDependencies(list, sizeof(list),
            StopDependency, &calls) == Rc95LdrUnsupported);
        assert(calls == 1); /* callback errors stop publication immediately */
    }
    assert(Rc95LdrEnumerateServiceDependencies(duplicate, sizeof(duplicate),
        CaptureDependency, 0) == Rc95LdrCorruptData && g_DependencyCount == 0);
    assert(Rc95LdrEnumerateServiceDependencies(malformed, sizeof(malformed),
        CaptureDependency, 0) == Rc95LdrCorruptData && g_DependencyCount == 0);
    assert(Rc95LdrEnumerateServiceDependencies(nonAscii, sizeof(nonAscii),
        CaptureDependency, 0) == Rc95LdrUnsupported && g_DependencyCount == 0);
    assert(Rc95LdrEnumerateServiceDependencies(supplementary,
        sizeof(supplementary), CaptureDependency, 0)
        == Rc95LdrUnsupported && g_DependencyCount == 0);
    assert(Rc95LdrEnumerateServiceDependencies(loneLowSurrogate,
        sizeof(loneLowSurrogate), CaptureDependency, 0)
        == Rc95LdrCorruptData && g_DependencyCount == 0);
}


static void TestServiceDependencyBounds(void)
{
    const uint8_t control[] = { 'A',0, 1,0, 0,0 };
    const uint8_t slash[] = { 'A',0, '/',0, 0,0 };
    const uint8_t dot[] = { '.',0, 0,0 };
    const uint8_t dotdot[] = { '.',0, '.',0, 0,0 };
    uint8_t tooMany[(RC95LDR_SERVICE_MAX_DEPENDENCIES + 1u) * 8u + 2u];
    uint8_t tooLong[(RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS + 1u) * 2u + 4u];
    uint8_t exactLimit[RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS * 2u + 4u];
    uint8_t limitList[RC95LDR_SERVICE_MAX_DEPENDENCIES * 8u + 2u];
    size_t offset = 0, i;
    g_DependencyCount = 0;
    assert(Rc95LdrEnumerateServiceDependencies(control, sizeof(control),
        CaptureDependency, 0) == Rc95LdrCorruptData);
    assert(Rc95LdrEnumerateServiceDependencies(slash, sizeof(slash),
        CaptureDependency, 0) == Rc95LdrCorruptData);
    assert(Rc95LdrEnumerateServiceDependencies(dot, sizeof(dot),
        CaptureDependency, 0) == Rc95LdrCorruptData);
    assert(Rc95LdrEnumerateServiceDependencies(dotdot, sizeof(dotdot),
        CaptureDependency, 0) == Rc95LdrCorruptData);

    /* Build 65 distinct three-character service names. The parser must
       reject the complete list before exposing even its first dependency. */
    for (i = 0; i < RC95LDR_SERVICE_MAX_DEPENDENCIES + 1u; ++i) {
        char name[4];
        unsigned value = (unsigned)i;
        name[0] = 'S';
        name[1] = (char)('A' + value / 26u);
        name[2] = (char)('A' + value % 26u);
        name[3] = 0;
        tooMany[offset++] = (uint8_t)name[0]; tooMany[offset++] = 0;
        tooMany[offset++] = (uint8_t)name[1]; tooMany[offset++] = 0;
        tooMany[offset++] = (uint8_t)name[2]; tooMany[offset++] = 0;
        tooMany[offset++] = 0; tooMany[offset++] = 0;
    }
    tooMany[offset++] = 0; tooMany[offset++] = 0;
    /* The configured limit itself is inclusive. Build a distinct 64-entry
       list; truncating the 65-entry fixture would leave a partial name. */
    memcpy(limitList, tooMany, RC95LDR_SERVICE_MAX_DEPENDENCIES * 8u);
    limitList[RC95LDR_SERVICE_MAX_DEPENDENCIES * 8u] = 0;
    limitList[RC95LDR_SERVICE_MAX_DEPENDENCIES * 8u + 1u] = 0;
    assert(Rc95LdrEnumerateServiceDependencies(limitList, sizeof(limitList),
        CaptureDependency, 0) == Rc95LdrSuccess);
    assert(g_DependencyCount == RC95LDR_SERVICE_MAX_DEPENDENCIES);
    g_DependencyCount = 0;
    assert(Rc95LdrEnumerateServiceDependencies(tooMany, offset,
        CaptureDependency, 0) == Rc95LdrUnsupported);
    assert(g_DependencyCount == 0);
    /* A malformed terminator must remain corruption even after the parser
       has already observed more than the supported number of entries. */
    assert(Rc95LdrEnumerateServiceDependencies(tooMany, offset - 2u,
        CaptureDependency, 0) == Rc95LdrCorruptData);
    assert(g_DependencyCount == 0);

    for (i = 0; i < RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS; ++i) {
        exactLimit[i * 2u] = 'A';
        exactLimit[i * 2u + 1u] = 0;
    }
    exactLimit[RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS * 2u] = 0;
    exactLimit[RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS * 2u + 1u] = 0;
    exactLimit[RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS * 2u + 2u] = 0;
    exactLimit[RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS * 2u + 3u] = 0;
    assert(Rc95LdrEnumerateServiceDependencies(exactLimit, sizeof(exactLimit),
        CaptureDependency, 0) == Rc95LdrSuccess);
    assert(g_DependencyCount == 1);
    assert(strlen(g_Dependencies[0]) == RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS);
    g_DependencyCount = 0;

    for (i = 0; i < RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS + 1u; ++i) {
        tooLong[i * 2u] = 'A';
        tooLong[i * 2u + 1u] = 0;
    }
    tooLong[(RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS + 1u) * 2u] = 0;
    tooLong[(RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS + 1u) * 2u + 1u] = 0;
    tooLong[(RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS + 1u) * 2u + 2u] = 0;
    tooLong[(RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS + 1u) * 2u + 3u] = 0;
    assert(Rc95LdrEnumerateServiceDependencies(tooLong, sizeof(tooLong),
        CaptureDependency, 0) == Rc95LdrCorruptData);
    assert(g_DependencyCount == 0);
}

static void TestMaximumServiceGraph(void)
{
    RC95LDR_SERVICE_DEPENDENCY_EDGE edges[
        RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES *
        RC95LDR_SERVICE_MAX_DEPENDENCIES];
    uint32_t output[RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES];
    uint32_t workspace[RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES * 3u];
    uint32_t edgeCount = 0, dependent, duplicate, i;

    /* Exercise the largest valid dependency set for 64 candidates: each of
       the 63 dependents declares the maximum 64 repeated edges to service
       63. Parallel edges must all be balanced by the topological sorter. */
    for (dependent = 0;
         dependent + 1u < RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES;
         ++dependent) {
        for (duplicate = 0;
             duplicate < RC95LDR_SERVICE_MAX_DEPENDENCIES;
             ++duplicate) {
            edges[edgeCount].Prerequisite =
                RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES - 1u;
            edges[edgeCount].Dependent = dependent;
            ++edgeCount;
        }
    }

    assert(edgeCount ==
        (RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES - 1u) *
        RC95LDR_SERVICE_MAX_DEPENDENCIES);
    assert(Rc95LdrResolveServiceOrder(
        RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES,
        edges, edgeCount, output,
        RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES,
        workspace,
        RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES * 3u)
        == Rc95LdrSuccess);
    assert(output[0] == RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES - 1u);
    for (i = 1; i < RC95LDR_HIVE_MAX_BOOT_DRIVER_CANDIDATES; ++i)
        assert(output[i] == i - 1u);
}

int main(void)
{
    TestMaximumServiceGraph();
    TestServiceDependencies();
    TestServiceDependencyBounds();
    TestServiceGroupOrder();
    TestServiceTagOrder();
    const RC95LDR_SERVICE_DEPENDENCY_EDGE edges[] = {
        { 2, 0 }, /* service 2 must load before service 0 */
        { 1, 0 },
        { 3, 2 }
    };
    const RC95LDR_SERVICE_DEPENDENCY_EDGE cycle[] = {
        { 0, 1 }, { 1, 2 }, { 2, 0 }
    };
    const RC95LDR_SERVICE_DEPENDENCY_EDGE duplicate[] = {
        { 0, 1 }, { 0, 1 }
    };
    const RC95LDR_SERVICE_DEPENDENCY_EDGE selfDependency[] = {
        { 1, 1 }
    };
    uint32_t order[4], workspace[12];

    assert(Rc95LdrResolveServiceOrder(4, edges, 3, order, 4,
                                      workspace, 12) == Rc95LdrSuccess);
    /* Ready set starts {1,3}; stable tie-break chooses 1, then 3, 2, 0. */
    assert(order[0] == 1 && order[1] == 3 &&
           order[2] == 2 && order[3] == 0);

    order[0] = 0xfeedu;
    assert(Rc95LdrResolveServiceOrder(3, cycle, 3, order, 4,
                                      workspace, 9) == Rc95LdrCorruptData);
    assert(order[0] == 0xfeedu);

    /* A service cannot depend on itself; reject it before publishing output. */
    assert(Rc95LdrResolveServiceOrder(2, selfDependency, 1, order, 2,
                                      workspace, 6) == Rc95LdrCorruptData);
    assert(order[0] == 0xfeedu);
    assert(Rc95LdrResolveServiceOrder(2, duplicate, 2, order, 4,
                                      workspace, 6) == Rc95LdrSuccess);
    assert(order[0] == 0 && order[1] == 1);
    {
        const RC95LDR_SERVICE_DEPENDENCY_EDGE invalid[] = { { 0, 2 } };
        assert(Rc95LdrResolveServiceOrder(2, invalid, 1, order, 4,
                                          workspace, 6) == Rc95LdrCorruptData);
    }
    assert(Rc95LdrResolveServiceOrder(2, 0, 0, order, 1,
                                      workspace, 4) == Rc95LdrBufferTooSmall);
    assert(Rc95LdrResolveServiceOrder(2, 0, 0, order, 2,
                                      workspace, 5) == Rc95LdrBufferTooSmall);
    assert(Rc95LdrResolveServiceOrder(0, 0, 0, order, 4,
                                      workspace, 8) == Rc95LdrInvalidParameter);

    /* With no edges, original input order is preserved deterministically. */
    assert(Rc95LdrResolveServiceOrder(4, 0, 0, order, 4,
                                      workspace, 12) == Rc95LdrSuccess);
    assert(order[0] == 0 && order[1] == 1 &&
           order[2] == 2 && order[3] == 3);

    /* An invalid graph must not publish a partial output order. */
    order[0] = 0xbeefu;
    {
        const RC95LDR_SERVICE_DEPENDENCY_EDGE invalid[] = { { 1, 0 }, { 2, 9 } };
        assert(Rc95LdrResolveServiceOrder(3, invalid, 2, order, 3,
                                          workspace, 9) == Rc95LdrCorruptData);
        assert(order[0] == 0xbeefu);
    }

    /* Aliasing output with workspace would make the final memcpy overlap. */
    assert(Rc95LdrResolveServiceOrder(2, 0, 0, workspace, 2,
                                      workspace, 6) == Rc95LdrInvalidParameter);
    /* Workspace is zeroed up front; it must not contain the input edge list. */
    assert(Rc95LdrResolveServiceOrder(2,
              (const RC95LDR_SERVICE_DEPENDENCY_EDGE *)workspace, 1,
              order, 2, workspace, 6) == Rc95LdrInvalidParameter);
    return 0;
}

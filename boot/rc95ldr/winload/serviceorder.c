#include "rc95ldrserviceorder.h"
#include <string.h>
#include <stdint.h>

static int RangesOverlap(const void *a, size_t aSize,
                         const void *b, size_t bSize)
{
    uintptr_t aStart, bStart, aEnd, bEnd;
    if (!aSize || !bSize) return 0;
    aStart = (uintptr_t)a;
    bStart = (uintptr_t)b;
    if (aSize > UINTPTR_MAX - aStart || bSize > UINTPTR_MAX - bStart)
        return 1; /* Treat an unrepresentable range as unsafe. */
    aEnd = aStart + aSize;
    bEnd = bStart + bSize;
    return aStart < bEnd && bStart < aEnd;
}


static uint32_t ServiceOrderU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t ServiceOrderU16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int ServiceOrderRawEqual(const uint8_t *a, const uint8_t *b, size_t chars)
{
    size_t i;
    for (i = 0; i < chars; ++i) {
        unsigned char x = (unsigned char)a[i * 2u];
        unsigned char y = (unsigned char)b[i * 2u];
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + ('a' - 'A'));
        if (x != y || a[i * 2u + 1u] != 0 || b[i * 2u + 1u] != 0)
            return 0;
    }
    return 1;
}

static int ServiceOrderNameEqual(const uint8_t *raw, size_t rawChars,
                                 const char *name)
{
    size_t i;
    for (i = 0; i < rawChars; ++i) {
        unsigned char a = (unsigned char)raw[i * 2u];
        unsigned char b = (unsigned char)name[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b || raw[i * 2u + 1u] != 0) return 0;
    }
    return name[rawChars] == 0;
}

RC95LDR_STATUS Rc95LdrFindServiceGroupOrder(
    const void *multiSz, size_t length, const char *group, uint32_t *order)
{
    const uint8_t *raw = (const uint8_t *)multiSz;
    size_t offset = 0;
    uint32_t index = 0, found = UINT32_MAX;

    if (!raw || !group || !*group || !order)
        return Rc95LdrInvalidParameter;
    if (strlen(group) > RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS)
        return Rc95LdrInvalidParameter;
    if (length < 4u || (length & 1u) ||
        ServiceOrderU16(raw + length - 2u) != 0 ||
        ServiceOrderU16(raw + length - 4u) != 0)
        return Rc95LdrCorruptData;

    if (length == 4u) return Rc95LdrNotFound;
    while (offset < length - 2u) {
        size_t start = offset, chars, prior = 0;
        if (ServiceOrderU16(raw + offset) == 0)
            return Rc95LdrCorruptData;
        while (offset < length - 2u &&
               ServiceOrderU16(raw + offset) != 0) {
            uint16_t ch = ServiceOrderU16(raw + offset);
            if (ch < 0x20u || ch == 0x7fu ||
                ch == '\\' || ch == '/' || ch == ':')
                return Rc95LdrCorruptData;
            if (ch >= 0xd800u && ch <= 0xdbffu) {
                uint16_t low;
                if (offset + 2u >= length - 2u)
                    return Rc95LdrCorruptData;
                low = ServiceOrderU16(raw + offset + 2u);
                if (low < 0xdc00u || low > 0xdfffu)
                    return Rc95LdrCorruptData;
                offset += 4u;
                continue;
            }
            if (ch >= 0xdc00u && ch <= 0xdfffu)
                return Rc95LdrCorruptData;
            offset += 2u;
        }
        if (offset >= length - 2u) return Rc95LdrCorruptData;
        chars = (offset - start) / 2u;
        if (!chars || chars > RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS ||
            (chars == 1u && raw[start] == '.' && raw[start + 1u] == 0) ||
            (chars == 2u && raw[start] == '.' && raw[start + 1u] == 0 &&
             raw[start + 2u] == '.' && raw[start + 3u] == 0))
            return Rc95LdrCorruptData;

        /* Reject duplicate group identities case-insensitively. Re-scan the
         * already validated prefix so this remains allocation-free. */
        while (prior < start) {
            size_t priorStart = prior, priorChars;
            while (prior < start && ServiceOrderU16(raw + prior) != 0)
                prior += 2u;
            priorChars = (prior - priorStart) / 2u;
            if (priorChars == chars &&
                ServiceOrderRawEqual(raw + priorStart, raw + start, chars))
                return Rc95LdrCorruptData;
            if (prior < start) prior += 2u;
        }

        if (chars == strlen(group) &&
            ServiceOrderNameEqual(raw + start, chars, group)) {
            if (found != UINT32_MAX) return Rc95LdrCorruptData;
            found = index;
        }
        ++index;
        offset += 2u; /* skip this entry's NUL */
    }
    if (offset != length - 2u) return Rc95LdrCorruptData;
    /* Non-ASCII names are valid UTF-16 but cannot match this ASCII query.
       They must not prevent ranking an unrelated representable group. */
    if (found == UINT32_MAX) return Rc95LdrNotFound;
    *order = found;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS Rc95LdrFindServiceTagOrder(
    const void *binary, size_t length, uint32_t tag, uint32_t *order)
{
    const uint8_t *raw = (const uint8_t *)binary;
    uint32_t count, i, found = UINT32_MAX;

    if (!raw || !order) return Rc95LdrInvalidParameter;
    if (length < 4u) return Rc95LdrCorruptData;
    count = ServiceOrderU32(raw);
    /* The portable boot-driver set is bounded to 64 entries. Permit a
     * somewhat larger registry list but keep duplicate checking bounded. */
    if (count > 256u || length != 4u + (size_t)count * 4u)
        return Rc95LdrCorruptData;

    for (i = 0; i < count; ++i) {
        uint32_t current = ServiceOrderU32(raw + 4u + (size_t)i * 4u);
        uint32_t j;
        for (j = 0; j < i; ++j) {
            if (ServiceOrderU32(raw + 4u + (size_t)j * 4u) == current)
                return Rc95LdrCorruptData;
        }
        if (current == tag) {
            if (found != UINT32_MAX) return Rc95LdrCorruptData;
            found = i;
        }
    }
    if (found == UINT32_MAX) return Rc95LdrNotFound;
    *order = found;
    return Rc95LdrSuccess;
}

RC95LDR_STATUS Rc95LdrResolveServiceOrder(
    uint32_t serviceCount,
    const RC95LDR_SERVICE_DEPENDENCY_EDGE *edges,
    uint32_t edgeCount,
    uint32_t *output,
    size_t outputCapacity,
    uint32_t *workspace,
    size_t workspaceCount)
{
    uint32_t *indegree, *emitted, *ordered;
    uint32_t i, placed = 0;

    if (!serviceCount || !output || !workspace ||
        (edgeCount && !edges))
        return Rc95LdrInvalidParameter;
    if ((size_t)serviceCount > SIZE_MAX / (3u * sizeof(uint32_t)) ||
        (size_t)serviceCount > SIZE_MAX / sizeof(uint32_t) ||
        (size_t)edgeCount > SIZE_MAX / sizeof(*edges) ||
        outputCapacity < serviceCount ||
        workspaceCount < (size_t)serviceCount * 3u)
        return Rc95LdrBufferTooSmall;

    /* Workspace is cleared before graph processing and the final output is
     * copied from its private ordered[] region. Reject aliases that would
     * erase the caller's edge list or make that final memcpy overlap. */
    if (RangesOverlap(output, (size_t)serviceCount * sizeof(*output),
                      workspace, (size_t)serviceCount * 3u * sizeof(*workspace)) ||
        (edgeCount && RangesOverlap(edges, (size_t)edgeCount * sizeof(*edges),
                                    workspace, (size_t)serviceCount * 3u * sizeof(*workspace))))
        return Rc95LdrInvalidParameter;

    indegree = workspace;
    emitted = workspace + serviceCount;
    ordered = workspace + (size_t)serviceCount * 2u;
    memset(workspace, 0, (size_t)serviceCount * 3u * sizeof(*workspace));

    for (i = 0; i < edgeCount; ++i) {
        uint32_t prerequisite = edges[i].Prerequisite;
        uint32_t dependent = edges[i].Dependent;
        if (prerequisite >= serviceCount || dependent >= serviceCount ||
            prerequisite == dependent)
            return Rc95LdrCorruptData;
        /* Repeated declarations are harmless parallel edges: each increments
         * and is later decremented exactly once. The hive adapter may choose
         * to reject duplicate names as a stricter registry-policy check. */
        if (indegree[dependent] == UINT32_MAX)
            return Rc95LdrIntegerOverflow;
        ++indegree[dependent];
    }

    while (placed < serviceCount) {
        uint32_t selected = UINT32_MAX;
        for (i = 0; i < serviceCount; ++i) {
            if (!emitted[i] && indegree[i] == 0) {
                selected = i;
                break; /* deterministic: lowest original index */
            }
        }
        if (selected == UINT32_MAX)
            return Rc95LdrCorruptData; /* dependency cycle */

        ordered[placed++] = selected;
        emitted[selected] = 1;
        for (i = 0; i < edgeCount; ++i) {
            if (edges[i].Prerequisite == selected) {
                uint32_t dependent = edges[i].Dependent;
                if (!indegree[dependent] || emitted[dependent])
                    return Rc95LdrCorruptData;
                --indegree[dependent];
            }
        }
    }
    memcpy(output, ordered, (size_t)serviceCount * sizeof(*output));
    return Rc95LdrSuccess;
}


/* Validate a dependency REG_MULTI_SZ completely before invoking the caller.
 * The portable hive adapter intentionally accepts ASCII service identities
 * only; it must never truncate or substitute an unrepresentable name. */
RC95LDR_STATUS Rc95LdrEnumerateServiceDependencies(
    const void *multiSz, size_t length,
    RC95LDR_SERVICE_DEPENDENCY_CALLBACK callback, void *context)
{
    const uint8_t *raw = (const uint8_t *)multiSz;
    size_t offset = 0;
    uint32_t count = 0;
    int unsupported = 0;

    if (!raw || !callback) return Rc95LdrInvalidParameter;
    if (length < 4u || (length & 1u) ||
        ServiceOrderU16(raw + length - 2u) != 0 ||
        ServiceOrderU16(raw + length - 4u) != 0)
        return Rc95LdrCorruptData;

    /* First pass: validate bounds, component syntax, ASCII representation,
       and case-insensitive uniqueness without allocating a name table. */
    while (offset < length - 2u && ServiceOrderU16(raw + offset) != 0) {
        size_t start = offset, chars, prior = 0;
        while (offset < length - 2u &&
               ServiceOrderU16(raw + offset) != 0) {
            uint16_t ch = ServiceOrderU16(raw + offset);
            if (ch < 0x20u || ch == 0x7fu ||
                ch == '\\' || ch == '/' || ch == ':')
                return Rc95LdrCorruptData;
            if (ch >= 0xd800u && ch <= 0xdbffu) {
                uint16_t low;
                if (offset + 2u >= length - 2u)
                    return Rc95LdrCorruptData;
                low = ServiceOrderU16(raw + offset + 2u);
                if (low < 0xdc00u || low > 0xdfffu)
                    return Rc95LdrCorruptData;
                unsupported = 1;
                offset += 4u;
                continue;
            }
            if (ch >= 0xdc00u && ch <= 0xdfffu)
                return Rc95LdrCorruptData;
            if (ch > 0x7fu) unsupported = 1;
            offset += 2u;
        }
        if (offset >= length - 2u) return Rc95LdrCorruptData;
        chars = (offset - start) / 2u;
        if (!chars || chars > RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS ||
            (chars == 1u && raw[start] == '.' && raw[start + 1u] == 0) ||
            (chars == 2u && raw[start] == '.' && raw[start + 1u] == 0 &&
             raw[start + 2u] == '.' && raw[start + 3u] == 0))
            return Rc95LdrCorruptData;
        while (prior < start) {
            size_t priorStart = prior, priorChars;
            while (prior < start && ServiceOrderU16(raw + prior) != 0)
                prior += 2u;
            priorChars = (prior - priorStart) / 2u;
            if (priorChars == chars &&
                ServiceOrderRawEqual(raw + priorStart, raw + start, chars))
                return Rc95LdrCorruptData;
            if (prior < start) prior += 2u;
        }
        if (++count > RC95LDR_SERVICE_MAX_DEPENDENCIES)
            unsupported = 1;
        offset += 2u;
    }
    if (offset == 0u && length == 4u) return Rc95LdrSuccess;
    if (offset != length - 2u) return Rc95LdrCorruptData;
    if (unsupported) return Rc95LdrUnsupported;

    /* Second pass: every name is now known to be representable and unique.
       Callback failures stop publication at the caller's chosen boundary. */
    offset = 0;
    while (offset < length - 2u && ServiceOrderU16(raw + offset) != 0) {
        char name[RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS + 1u];
        size_t start = offset, chars, i;
        while (ServiceOrderU16(raw + offset) != 0) offset += 2u;
        chars = (offset - start) / 2u;
        for (i = 0; i < chars; ++i) name[i] = (char)raw[start + i * 2u];
        name[chars] = 0;
        {
            RC95LDR_STATUS status = callback(context, name);
            if (status != Rc95LdrSuccess) return status;
        }
        offset += 2u;
    }
    return Rc95LdrSuccess;
}

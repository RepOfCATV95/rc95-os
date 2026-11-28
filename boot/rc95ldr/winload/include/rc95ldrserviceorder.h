#ifndef _RC95LDR_SERVICE_ORDER_H_
#define _RC95LDR_SERVICE_ORDER_H_
#include "rc95ldr.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RC95LDR_SERVICE_ORDER_VERSION 1u
#define RC95LDR_SERVICE_GROUP_MAX_NAME_CHARS 256u
#define RC95LDR_SERVICE_MAX_DEPENDENCIES 64u

typedef RC95LDR_STATUS (*RC95LDR_SERVICE_DEPENDENCY_CALLBACK)(
    void *Context, const char *ServiceName);

/* Parse a UTF-16LE REG_MULTI_SZ DependOnService list. The complete list is
 * validated (terminators, component bounds, ASCII names and duplicates)
 * before callbacks begin. Empty lists succeed; non-empty lists are capped
 * at RC95LDR_SERVICE_MAX_DEPENDENCIES. No allocation is performed. */
RC95LDR_STATUS Rc95LdrEnumerateServiceDependencies(
    const void *MultiSz, size_t Length,
    RC95LDR_SERVICE_DEPENDENCY_CALLBACK Callback, void *Context);

/* An edge means Prerequisite must appear before Dependent. Indices refer to
 * the caller's service array; names are resolved by the hive adapter. */
typedef struct _RC95LDR_SERVICE_DEPENDENCY_EDGE {
    uint32_t Prerequisite;
    uint32_t Dependent;
} RC95LDR_SERVICE_DEPENDENCY_EDGE;

/* Parse a UTF-16LE REG_MULTI_SZ ServiceGroupOrder list and return the
 * zero-based position of the ASCII Group query. Valid non-ASCII entries are
 * structurally validated and skipped; they do not block an unrelated ASCII
 * match. The complete list is checked for malformed UTF-16, duplicate names
 * and terminators before Order is published. Missing Group returns NotFound. */
RC95LDR_STATUS Rc95LdrFindServiceGroupOrder(
    const void *MultiSz, size_t Length, const char *Group, uint32_t *Order);

/* Parse a GroupOrderList REG_BINARY value: DWORD count followed by count
 * DWORD service tags. Returns the zero-based position of Tag. The complete
 * value is length-checked and duplicate tags are rejected before publishing
 * Order. Missing Tag returns NotFound; no allocation is performed. */
RC95LDR_STATUS Rc95LdrFindServiceTagOrder(
    const void *Binary, size_t Length, uint32_t Tag, uint32_t *Order);

/* Stable Kahn topological sort. Among currently-ready nodes, the lowest
 * input index wins. Workspace contains 3 * ServiceCount uint32_t entries:
 * indegree[], emitted[] (0 or 1), then a private order buffer. Output is
 * copied only after the complete graph succeeds, so failures leave it intact. No allocation is performed.
 * Output and workspace must not overlap; edges must not overlap workspace
 * because workspace is cleared before graph processing. Output may overlap
 * edges because output is not written until all edges have been consumed.
 * With no edges, input order is preserved. Missing names and service/group
 * expansion are adapter responsibilities.
 */
RC95LDR_STATUS Rc95LdrResolveServiceOrder(
    uint32_t ServiceCount,
    const RC95LDR_SERVICE_DEPENDENCY_EDGE *Edges,
    uint32_t EdgeCount,
    uint32_t *OutputOrder,
    size_t OutputCapacity,
    uint32_t *Workspace,
    size_t WorkspaceCount);

#ifdef __cplusplus
}
#endif
#endif

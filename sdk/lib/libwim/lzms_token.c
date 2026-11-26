/*
 * Clean-room LZMS token executor.
 *
 * Semantics cross-checked against the independently published wimlib LZMS
 * decompressor (src/lzms_decompress.c; Eric Biggers, 2013-2016):
 *   https://github.com/ebiggers/wimlib/blob/master/src/lzms_decompress.c
 *
 * This file is an independent implementation.  In particular, repeat queues
 * use the format's four-slot delayed-insertion rule, and delta matches use
 * A[i] = B[i] + C[i] - D[i] modulo 256.
 */
#include <string.h>
#include <libwim/lzms_token.h>

static LZMS_TOKEN_STATUS
CheckOutput(const LZMS_TOKEN_DECODER *d, uint64_t n)
{
    uint64_t limit = d->MaximumOutputSize;
    if (n > UINT64_MAX - d->Produced)
        return LZMS_TOKEN_BAD_STATE;
    if (d->ExpectedOutputSize &&
        (limit == 0 || d->ExpectedOutputSize < limit))
        limit = d->ExpectedOutputSize;
    if (limit && (d->Produced > limit || n > limit - d->Produced))
        return LZMS_TOKEN_OUTPUT_LIMIT;
    if (n > UINT64_MAX - d->Dictionary->TotalBytes)
        return LZMS_TOKEN_BAD_STATE;
    return LZMS_TOKEN_OK;
}

static void
InitializeRepeatQueues(LZMS_TOKEN_DECODER *d)
{
    unsigned i;
    for (i = 0; i < LZMS_TOKEN_REP_QUEUE_SIZE; ++i) {
        d->RepDistances[i] = i + 1;
        d->DeltaRepDistances[i] = i + 1;
        d->DeltaRepPowers[i] = 0;
        d->RepValid[i] = 1;
        d->DeltaRepValid[i] = 1;
    }
}

/* Select rank 0..2, accounting for the one-item delayed queue insertion. */
static uint32_t
SelectLzRepeat(LZMS_TOKEN_DECODER *d, uint8_t rank)
{
    unsigned slot = rank + (d->PreviousItemType & 1u);
    uint32_t value = d->RepDistances[slot];
    unsigned i;
    for (i = slot; i != 0; --i) {
        d->RepDistances[i] = d->RepDistances[i - 1];
        d->RepValid[i] = d->RepValid[i - 1];
    }
    d->RepDistances[0] = value;
    d->RepValid[0] = 1;
    return value;
}

static void
RememberLzExplicit(LZMS_TOKEN_DECODER *d, uint32_t distance)
{
    d->RepDistances[3] = d->RepDistances[2];
    d->RepDistances[2] = d->RepDistances[1];
    d->RepDistances[1] = d->RepDistances[0];
    d->RepValid[3] = d->RepValid[2];
    d->RepValid[2] = d->RepValid[1];
    d->RepValid[1] = d->RepValid[0];
    d->RepDistances[0] = distance;
    d->RepValid[0] = 1;
}

static void
SelectDeltaRepeat(LZMS_TOKEN_DECODER *d, uint8_t rank,
                  uint32_t *rawDistance, uint8_t *power)
{
    unsigned slot = rank + (d->PreviousItemType >> 1);
    unsigned i;
    *rawDistance = d->DeltaRepDistances[slot];
    *power = d->DeltaRepPowers[slot];
    for (i = slot; i != 0; --i) {
        d->DeltaRepDistances[i] = d->DeltaRepDistances[i - 1];
        d->DeltaRepPowers[i] = d->DeltaRepPowers[i - 1];
        d->DeltaRepValid[i] = d->DeltaRepValid[i - 1];
    }
    d->DeltaRepDistances[0] = *rawDistance;
    d->DeltaRepPowers[0] = *power;
    d->DeltaRepValid[0] = 1;
}

static void
RememberDeltaExplicit(LZMS_TOKEN_DECODER *d, uint32_t rawDistance,
                      uint8_t power)
{
    d->DeltaRepDistances[3] = d->DeltaRepDistances[2];
    d->DeltaRepDistances[2] = d->DeltaRepDistances[1];
    d->DeltaRepDistances[1] = d->DeltaRepDistances[0];
    d->DeltaRepPowers[3] = d->DeltaRepPowers[2];
    d->DeltaRepPowers[2] = d->DeltaRepPowers[1];
    d->DeltaRepPowers[1] = d->DeltaRepPowers[0];
    d->DeltaRepValid[3] = d->DeltaRepValid[2];
    d->DeltaRepValid[2] = d->DeltaRepValid[1];
    d->DeltaRepValid[1] = d->DeltaRepValid[0];
    d->DeltaRepDistances[0] = rawDistance;
    d->DeltaRepPowers[0] = power;
    d->DeltaRepValid[0] = 1;
}

LZMS_TOKEN_STATUS
LzmsTokenDecoderInitialize(LZMS_TOKEN_DECODER *d, LZMS_DICTIONARY *dict,
                           uint64_t expected, uint64_t maximum)
{
    if (!d || !dict || !dict->Window || !dict->WindowSize ||
        (maximum && expected > maximum))
        return LZMS_TOKEN_INVALID_ARGUMENT;
    memset(d, 0, sizeof(*d));
    d->Dictionary = dict;
    d->ExpectedOutputSize = expected;
    d->MaximumOutputSize = maximum;
    InitializeRepeatQueues(d);
    return LZMS_TOKEN_OK;
}

LZMS_TOKEN_STATUS
LzmsTokenDecode(LZMS_TOKEN_DECODER *d, const LZMS_TOKEN *t,
                uint8_t *out, size_t capacity, size_t *used)
{
    LZMS_DICTIONARY *dict;
    LZMS_TOKEN_STATUS status;
    uint32_t length, distance = 0, rawDistance = 0;
    uint32_t i;
    uint8_t power = 0;
    uint8_t itemType;
    unsigned repeatSlot = 0;
    int delta, repeat;

    if (used) *used = 0;
    if (!d || !d->Dictionary || !t || !used || (capacity && !out))
        return LZMS_TOKEN_INVALID_ARGUMENT;
    dict = d->Dictionary;

    switch (t->Kind) {
    case LZMS_TOKEN_LITERAL: length = 1; delta = repeat = 0; itemType = 0; break;
    case LZMS_TOKEN_MATCH: length = t->Length; delta = repeat = 0; itemType = 1; break;
    case LZMS_TOKEN_REP_MATCH: length = t->Length; delta = 0; repeat = 1; itemType = 1; break;
    case LZMS_TOKEN_DELTA_MATCH: length = t->Length; delta = 1; repeat = 0; itemType = 2; break;
    case LZMS_TOKEN_REP_DELTA_MATCH: length = t->Length; delta = repeat = 1; itemType = 2; break;
    default: return LZMS_TOKEN_INVALID_ARGUMENT;
    }
    if (length > capacity)
        return LZMS_TOKEN_OUTPUT_FULL;
    status = CheckOutput(d, length);
    if (status != LZMS_TOKEN_OK)
        return status;

    if (t->Kind == LZMS_TOKEN_LITERAL) {
        if (LzmsDictionaryAppend(dict, &t->Literal, 1) != LZMS_DICTIONARY_OK)
            return LZMS_TOKEN_BAD_STATE;
        out[0] = t->Literal;
    } else {
        if (!length || (repeat && t->RepIndex >= LZMS_TOKEN_REP_COUNT))
            return LZMS_TOKEN_INVALID_ARGUMENT;

        if (delta) {
            if (repeat) {
                repeatSlot = t->RepIndex + (d->PreviousItemType >> 1);
                rawDistance = d->DeltaRepDistances[repeatSlot];
                power = d->DeltaRepPowers[repeatSlot];
            } else {
                rawDistance = t->Distance;
                power = t->DeltaPower;
            }
            if (power > 7 || rawDistance == 0 ||
                rawDistance > (UINT32_MAX >> power))
                return LZMS_TOKEN_BAD_DISTANCE;
            distance = rawDistance << power;
            if (distance > UINT32_MAX - (1u << power) ||
                (uint64_t)distance + (1u << power) >
                    LzmsDictionaryAvailable(dict))
                return LZMS_TOKEN_BAD_DISTANCE;
        } else {
            if (repeat) {
                repeatSlot = t->RepIndex + (d->PreviousItemType & 1u);
                distance = d->RepDistances[repeatSlot];
            } else {
                distance = t->Distance;
            }
            if (!distance || distance > LzmsDictionaryAvailable(dict))
                return LZMS_TOKEN_BAD_DISTANCE;
        }

        for (i = 0; i < length; ++i) {
            uint64_t pos = dict->TotalBytes;
            uint8_t value;
            if (delta) {
                uint32_t span = 1u << power;
                uint8_t b = LzmsDictionaryGetByte(dict, pos - distance);
                uint8_t c = LzmsDictionaryGetByte(dict, pos - span);
                uint8_t sourceDelta = LzmsDictionaryGetByte(
                    dict, pos - distance - span);
                value = (uint8_t)(b + c - sourceDelta);
            } else {
                value = LzmsDictionaryGetByte(dict, pos - distance);
            }
            if (LzmsDictionaryAppend(dict, &value, 1) != LZMS_DICTIONARY_OK)
                return LZMS_TOKEN_BAD_STATE;
            out[i] = value;
        }

        if (delta) {
            if (repeat)
                SelectDeltaRepeat(d, t->RepIndex, &rawDistance, &power);
            else
                RememberDeltaExplicit(d, rawDistance, power);
        } else if (repeat) {
            (void)SelectLzRepeat(d, t->RepIndex);
        } else {
            RememberLzExplicit(d, distance);
        }
    }

    d->Produced += length;
    d->PreviousTokenKind = t->Kind;
    d->PreviousItemType = itemType;
    d->HasPreviousToken = 1;
    *used = length;
    return LZMS_TOKEN_OK;
}

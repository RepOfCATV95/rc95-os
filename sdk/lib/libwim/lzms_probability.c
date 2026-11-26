/*
 * PROJECT:    RC95-OS LZMS codec
 * LICENSE:    GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:    Clean-room rolling-window binary probability models
 */
#include <libwim/lzms_probability.h>
void
LzmsProbabilityModelInitialize(LZMS_PROBABILITY_MODEL *Model)
{
    if (Model == 0) return;
    Model->RecentBits = LZMS_PROBABILITY_INITIAL_HISTORY;
    Model->ZeroCount = LZMS_PROBABILITY_INITIAL_ZERO_COUNT;
}
uint32_t
LzmsProbabilityModelGetZeroCount(const LZMS_PROBABILITY_MODEL *Model)
{
    uint32_t Count;
    if (Model == 0) return 1;
    Count = Model->ZeroCount;
    if (Count == 0) Count = 1;
    else if (Count >= LZMS_PROBABILITY_WINDOW_BITS)
        Count = LZMS_PROBABILITY_WINDOW_BITS - 1;
    return Count;
}
LZMS_PROBABILITY_STATUS
LzmsProbabilityModelUpdate(LZMS_PROBABILITY_MODEL *Model, uint32_t Bit)
{
    uint32_t OldestBit;
    if (Model == 0 || Bit > 1) return LZMS_PROBABILITY_INVALID_ARGUMENT;
    OldestBit = (uint32_t)(Model->RecentBits >> 63);
    if (OldestBit != Bit) {
        if (OldestBit != 0) ++Model->ZeroCount;
        else --Model->ZeroCount;
    }
    Model->RecentBits = (Model->RecentBits << 1) | (uint64_t)Bit;
    return LZMS_PROBABILITY_OK;
}
LZMS_PROBABILITY_STATUS
LzmsProbabilityAdvanceState(uint32_t *State, uint32_t StateCount, uint32_t Bit)
{
    if (State == 0 || StateCount == 0 ||
        (StateCount & (StateCount - 1)) != 0 ||
        Bit > 1 || *State >= StateCount)
        return LZMS_PROBABILITY_INVALID_ARGUMENT;
    *State = (*State << 1) & (StateCount - 1);
    *State |= Bit;
    return LZMS_PROBABILITY_OK;
}

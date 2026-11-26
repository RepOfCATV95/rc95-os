#ifndef _LIBWIM_LZMS_SLOTS_H_
#define _LIBWIM_LZMS_SLOTS_H_
#include <stdint.h>
#define LZMS_MAX_OFFSET_SLOTS 799u
#define LZMS_LENGTH_SLOTS 54u
extern const uint32_t LzmsOffsetSlotBase[800];
extern const uint8_t LzmsExtraOffsetBits[799];
extern const uint32_t LzmsLengthSlotBase[55];
extern const uint8_t LzmsExtraLengthBits[54];
uint16_t LzmsGetOffsetSlotCount(uint32_t OutputSize);
#endif

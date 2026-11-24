/*
 * PROJECT:         RC95-OS NTFS regression tests
 * LICENSE:         GPL-2.0-or-later
 * PURPOSE:         NTFS driver test registration
 */

#define STANDALONE
#include <apitest.h>

extern void func_VolumeInfo(void);

const struct test winetest_testlist[] =
{
    { "VolumeInfo", func_VolumeInfo },
    { 0, 0 }
};

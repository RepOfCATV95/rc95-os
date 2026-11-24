/*
 * PROJECT:         RC95-OS NTFS regression tests
 * LICENSE:         GPL-2.0-or-later
 * PURPOSE:         NTFS volume and file-I/O integration tests
 */

#include <apitest.h>

#include <windows.h>
#include <wchar.h>
#include <string.h>

static
void
TestNtfsFileRoundTrip(_In_ PCWSTR RootPath)
{
    static const UCHAR Expected[] =
    {
        0x52, 0x43, 0x39, 0x35, 0x2d, 0x4e, 0x54, 0x46,
        0x53, 0x2d, 0x52, 0x4f, 0x53, 0x54, 0x45, 0x53,
        0x54, 0x2d, 0x31, 0x00
    };
    UCHAR Actual[sizeof(Expected)];
    WCHAR TempName[MAX_PATH];
    HANDLE FileHandle;
    DWORD BytesTransferred;
    BOOL Result;

    if (!GetTempFileNameW(RootPath, L"Ntf", 0, TempName))
    {
        skip("Unable to create test file name on %ls (error %lu)\n",
             RootPath, GetLastError());
        return;
    }

    FileHandle = CreateFileW(TempName,
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             NULL,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_TEMPORARY,
                             NULL);
    ok(FileHandle != INVALID_HANDLE_VALUE,
       "CreateFile(%ls) failed, error %lu\n", TempName, GetLastError());
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(TempName);
        return;
    }

    BytesTransferred = 0;
    Result = WriteFile(FileHandle, Expected, sizeof(Expected),
                       &BytesTransferred, NULL);
    ok(Result, "WriteFile failed, error %lu\n", GetLastError());
    if (Result)
    {
        ok(BytesTransferred == sizeof(Expected),
           "WriteFile transferred %lu of %Iu bytes\n",
           BytesTransferred, sizeof(Expected));
        Result = (BytesTransferred == sizeof(Expected));
    }

    if (Result)
    {
        Result = FlushFileBuffers(FileHandle);
        ok(Result, "FlushFileBuffers failed, error %lu\n", GetLastError());
    }

    CloseHandle(FileHandle);
    if (!Result)
    {
        DeleteFileW(TempName);
        return;
    }

    FileHandle = CreateFileW(TempName,
                             GENERIC_READ,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    ok(FileHandle != INVALID_HANDLE_VALUE,
       "Reopening %ls failed, error %lu\n", TempName, GetLastError());
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(TempName);
        return;
    }

    ZeroMemory(Actual, sizeof(Actual));
    BytesTransferred = 0;
    Result = ReadFile(FileHandle, Actual, sizeof(Actual),
                      &BytesTransferred, NULL);
    ok(Result, "ReadFile failed, error %lu\n", GetLastError());
    if (Result)
    {
        ok(BytesTransferred == sizeof(Expected),
           "ReadFile transferred %lu of %Iu bytes\n",
           BytesTransferred, sizeof(Expected));
        ok(memcmp(Actual, Expected, sizeof(Expected)) == 0,
           "Read-back data differs from written data\n");
    }

    CloseHandle(FileHandle);
    ok(DeleteFileW(TempName), "DeleteFile(%ls) failed, error %lu\n",
       TempName, GetLastError());
}

static
void
TestNtfsAppendAndTruncate(_In_ PCWSTR RootPath)
{
    static const UCHAR First[] = { 'R', 'C', '9', '5', '-', 'A' };
    static const UCHAR Second[] = { '-', 'P', 'P', 'E', 'N', 'D' };
    static const UCHAR Expected[] = { 'R', 'C', '9', '5', '-', 'A',
                                      '-', 'P', 'P', 'E', 'N', 'D' };
    UCHAR Actual[sizeof(Expected)];
    WCHAR TempName[MAX_PATH];
    HANDLE FileHandle;
    DWORD BytesTransferred;
    BOOL Result;

    if (!GetTempFileNameW(RootPath, L"Nta", 0, TempName))
    {
        skip("Unable to create append test file name on %ls (error %lu)\n",
             RootPath, GetLastError());
        return;
    }

    FileHandle = CreateFileW(TempName,
                             GENERIC_READ | GENERIC_WRITE,
                             0,
                             NULL,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_TEMPORARY,
                             NULL);
    ok(FileHandle != INVALID_HANDLE_VALUE,
       "CreateFile(%ls) for append/truncate test failed, error %lu\n",
       TempName, GetLastError());
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        DeleteFileW(TempName);
        return;
    }

    BytesTransferred = 0;
    Result = WriteFile(FileHandle, First, sizeof(First),
                       &BytesTransferred, NULL);
    ok(Result, "Initial WriteFile failed, error %lu\n", GetLastError());
    if (Result)
    {
        ok(BytesTransferred == sizeof(First),
           "Initial WriteFile transferred %lu of %Iu bytes\n",
           BytesTransferred, sizeof(First));
        Result = (BytesTransferred == sizeof(First));
    }

    if (Result)
    {
        LARGE_INTEGER EndOfFile;
        EndOfFile.QuadPart = 0;

        Result = SetFilePointerEx(FileHandle, EndOfFile, NULL, FILE_END);
        ok(Result, "Seeking to EOF failed, error %lu\n", GetLastError());
    }

    if (Result)
    {
        BytesTransferred = 0;
        Result = WriteFile(FileHandle, Second, sizeof(Second),
                           &BytesTransferred, NULL);
        ok(Result, "Append WriteFile failed, error %lu\n", GetLastError());
        if (Result)
        {
            ok(BytesTransferred == sizeof(Second),
               "Append WriteFile transferred %lu of %Iu bytes\n",
               BytesTransferred, sizeof(Second));
            Result = (BytesTransferred == sizeof(Second));
        }
    }

    if (Result)
    {
        Result = FlushFileBuffers(FileHandle);
        ok(Result, "FlushFileBuffers after append failed, error %lu\n",
           GetLastError());
    }

    if (Result)
    {
        LARGE_INTEGER Start;
        Start.QuadPart = 0;
        Result = SetFilePointerEx(FileHandle, Start, NULL, FILE_BEGIN);
        ok(Result, "Seeking to beginning before truncate failed, error %lu\n",
           GetLastError());
    }

    if (Result)
    {
        ZeroMemory(Actual, sizeof(Actual));
        BytesTransferred = 0;
        Result = ReadFile(FileHandle, Actual, sizeof(Expected),
                          &BytesTransferred, NULL);
        ok(Result, "ReadFile before truncate failed, error %lu\n", GetLastError());
        if (Result)
        {
            ok(BytesTransferred == sizeof(Expected),
               "Appended stream returned %lu bytes, expected %Iu\n",
               BytesTransferred, sizeof(Expected));
            ok(memcmp(Actual, Expected, sizeof(Expected)) == 0,
               "Appended stream contents differ from expected data\n");
            Result = (BytesTransferred == sizeof(Expected) &&
                      memcmp(Actual, Expected, sizeof(Expected)) == 0);
        }
    }

    if (Result)
    {
        LARGE_INTEGER NewEnd;
        NewEnd.QuadPart = sizeof(First) + 2;

        Result = SetFilePointerEx(FileHandle, NewEnd, NULL, FILE_BEGIN);
        ok(Result, "Seeking to truncation point failed, error %lu\n",
           GetLastError());
    }

    if (Result)
    {
        Result = SetEndOfFile(FileHandle);
        ok(Result, "SetEndOfFile failed, error %lu\n", GetLastError());
    }

    if (Result)
    {
        Result = FlushFileBuffers(FileHandle);
        ok(Result, "FlushFileBuffers after truncation failed, error %lu\n",
           GetLastError());
    }

    if (Result)
    {
        LARGE_INTEGER Start;
        Start.QuadPart = 0;
        Result = SetFilePointerEx(FileHandle, Start, NULL, FILE_BEGIN);
        ok(Result, "Seeking to beginning after truncate failed, error %lu\n",
           GetLastError());
    }

    if (Result)
    {
        ZeroMemory(Actual, sizeof(Actual));
        BytesTransferred = 0;
        Result = ReadFile(FileHandle, Actual, sizeof(First) + 2,
                          &BytesTransferred, NULL);
        ok(Result, "ReadFile after truncate failed, error %lu\n", GetLastError());
        if (Result)
        {
            ok(BytesTransferred == sizeof(First) + 2,
               "Truncated stream returned %lu bytes, expected %Iu\n",
               BytesTransferred, sizeof(First) + 2);
            ok(memcmp(Actual, Expected, sizeof(First) + 2) == 0,
               "Append/truncate contents differ from expected prefix\n");
        }
    }

    CloseHandle(FileHandle);
    ok(DeleteFileW(TempName), "DeleteFile(%ls) after append/truncate failed, error %lu\n",
       TempName, GetLastError());
}
void
func_VolumeInfo(void)
{
    DWORD DriveMask;
    DWORD Drive;
    ULONG NtfsVolumes = 0;

    DriveMask = GetLogicalDrives();
    ok(DriveMask != 0, "GetLogicalDrives failed, error %lu\n", GetLastError());
    if (DriveMask == 0)
        return;

    for (Drive = 0; Drive < 26; ++Drive)
    {
        WCHAR RootPath[] = L"A:\\";
        WCHAR FileSystemName[MAX_PATH];
        DWORD MaximumComponentLength = 0;
        DWORD FileSystemFlags = 0;

        if ((DriveMask & (1u << Drive)) == 0)
            continue;

        RootPath[0] = (WCHAR)(L'A' + Drive);
        if (!GetVolumeInformationW(RootPath,
                                   NULL,
                                   0,
                                   NULL,
                                   &MaximumComponentLength,
                                   &FileSystemFlags,
                                   FileSystemName,
                                   sizeof(FileSystemName) / sizeof(FileSystemName[0])))
        {
            trace("Skipping inaccessible volume %ls (error %lu)\n",
                  RootPath, GetLastError());
            continue;
        }

        if (_wcsicmp(FileSystemName, L"NTFS") != 0)
            continue;

        ++NtfsVolumes;
        ok(MaximumComponentLength != 0,
           "%ls reports NTFS with zero maximum component length\n", RootPath);
        ok((FileSystemFlags & FILE_CASE_PRESERVED_NAMES) != 0,
           "%ls NTFS volume does not report case-preserved names (flags 0x%08lx)\n",
           RootPath, FileSystemFlags);

        TestNtfsFileRoundTrip(RootPath);
        TestNtfsAppendAndTruncate(RootPath);
    }

    if (NtfsVolumes == 0)
        skip("No accessible NTFS volume is mounted; integration checks were not run\n");
    else
        trace("Validated volume information and file I/O for %lu NTFS volume(s)\n",
              NtfsVolumes);
}

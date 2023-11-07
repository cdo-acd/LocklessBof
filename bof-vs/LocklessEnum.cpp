#include <Shlobj.h>
#include <Windows.h>
#include "base\helpers.h"
#include "base\ntdefs.h"
#include <shlwapi.h>
#include <ntstatus.h>
#include <time.h>

#ifdef _DEBUG

#pragma comment (lib, "shell32.lib")
#pragma comment (lib, "Shlwapi.lib")
#include "base\mock.h"
#undef DECLSPEC_IMPORT
#define DECLSPEC_IMPORT
#endif

#define CALLBACK_FILE 0x02
#define CALLBACK_FILE_WRITE 0x08
#define CALLBACK_FILE_CLOSE 0x09
#define CHUNK_SIZE 0xe1000
PVOID GetLibraryProcAddress(PSTR LibraryName, PSTR ProcName) {
    return GetProcAddress(GetModuleHandleA(LibraryName), ProcName);
}


extern "C" {
#include "beacon.h"
    BOOL upload_file(LPCSTR fileName, char fileData[], ULONG32 fileLength);
    //Ran into overload issues when using DFR Macro for wcsstr/time. Switched to manual resolution isntead of using Macro for these two methods.
#ifndef _DEBUG
    WINBASEAPI wchar_t* __cdecl MSVCRT$wcsstr(const wchar_t* _Str, const wchar_t* _SubStr);
#define wcsstr MSVCRT$wcsstr
    WINBASEAPI time_t __cdecl MSVCRT$time(time_t* time);
#define time MSVCRT$time
    //wcstombs_s(&convertedChars, fileNameChar, bufferSize + 1, fileName.Buffer, bufferSize);
    WINBASEAPI int __cdecl MSVCRT$wcstombs_s(size_t* preturnValue, char* mbstr, size_t sizeInBytes, const wchar_t* wcstr, size_t count);
#define wcstombs_s MSVCRT$wcstombs_s
#endif
    DFR(KERNEL32, GetLastError);
    DFR(SHELL32, SHGetFolderPathA);
    DFR(SHLWAPI, PathAppendA);
    DFR(KERNEL32, GetProcAddress);
    DFR(KERNEL32, GetModuleHandleA);
    DFR(KERNEL32, VirtualAlloc);
    DFR(KERNEL32, VirtualFree);
    DFR(KERNEL32, OpenProcess);
    DFR(KERNEL32, CloseHandle);
    DFR(KERNEL32, GetCurrentProcess);
    DFR(KERNEL32, HeapAlloc)
    DFR(KERNEL32, GetProcessHeap);
    DFR(KERNEL32, HeapReAlloc);
    DFR(MSVCRT, memset);
    DFR(KERNEL32, HeapFree);
    DFR(KERNEL32, GlobalAlloc);
    DFR(KERNEL32, ReadFile);
    DFR(KERNEL32, SetFilePointer);
    DFR(KERNEL32, GetFileSize);
    DFR(MSVCRT, wcstombs)
    DFR(MSVCRT, wcscmp)
    DFR(MSVCRT, wcslen)
    DFR(KERNEL32, GetFileType);
    #define GetLastError KERNEL32$GetLastError
    #define SHGetFolderPathA SHELL32$SHGetFolderPathA
    #define PathAppendA SHLWAPI$PathAppendA
    #define GetProcAddress KERNEL32$GetProcAddress
    #define GetModuleHandleA KERNEL32$GetModuleHandleA
    #define VirtualAlloc KERNEL32$VirtualAlloc
    #define VirtualFree KERNEL32$VirtualFree
    #define OpenProcess KERNEL32$OpenProcess
    #define CloseHandle KERNEL32$CloseHandle
    #define GetCurrentProcess KERNEL32$GetCurrentProcess
    #define HeapAlloc KERNEL32$HeapAlloc
    #define GetProcessHeap KERNEL32$GetProcessHeap
    #define HeapReAlloc KERNEL32$HeapReAlloc
    #define memset MSVCRT$memset
    #define HeapFree KERNEL32$HeapFree
    #define GlobalAlloc KERNEL32$GlobalAlloc
    #define ReadFile KERNEL32$ReadFile
    #define SetFilePointer KERNEL32$SetFilePointer
    #define GetFileSize KERNEL32$GetFileSize
    #define wcstombs MSVCRT$wcstombs
    #define wcscmp MSVCRT$wcscmp
    #define GetFileType KERNEL32$GetFileType
#define wcslen MSVCRT$wcslen

        void go(char* buf, int len) {

        DWORD pid = 0;
        wchar_t* filename = NULL;
        wchar_t* processName = NULL;
        datap parser;
        BeaconDataParse(&parser, buf, len);
        filename = (wchar_t*)BeaconDataExtract(&parser, NULL);
        size_t filenameLen = wcslen(filename);

        //Value will either be a wchar_t or int based off wither key value filename or handle_id was chosen
        processName = (wchar_t*)BeaconDataExtract(&parser, NULL);
        size_t processLen = wcslen(processName);
        BeaconPrintf(CALLBACK_OUTPUT, "Attempting to enumerate file handle to % .*S ", filenameLen, filename);


        DWORD dwErrorCode = ERROR_SUCCESS;
        PSYSTEM_HANDLE_INFORMATION handleInfo = NULL;
        PSYSTEM_HANDLE_TABLE_ENTRY_INFO curHandle = NULL;
        ULONG handleInfoSize = 1000;
        HANDLE processHandle = NULL;
        HANDLE dupHandle = NULL;
        PVOID objectNameInfo = NULL;
        POBJECT_TYPE_INFORMATION objectTypeInfo = NULL;

        //NTFunctions
        _NtQuerySystemInformation NtQuerySystemInformation;
        _NtDuplicateObject NtDuplicateObject = NULL;
        _NtQueryObject NtQueryObject = NULL;
        _RtlInitUnicodeString RtlInitUnicodeString = NULL;
        _NtClose NtClose = NULL;

        //Resolve NT API Function Addresses
        NtQuerySystemInformation = (_NtQuerySystemInformation)GetLibraryProcAddress("ntdll.dll", "NtQuerySystemInformation");
        NtDuplicateObject = (_NtDuplicateObject)GetLibraryProcAddress("ntdll.dll", "NtDuplicateObject");
        NtQueryObject = (_NtQueryObject)GetLibraryProcAddress("ntdll.dll", "NtQueryObject");
        NtClose = (_NtClose)GetLibraryProcAddress("ntdll.dll", "NtClose");
        RtlInitUnicodeString = (_RtlInitUnicodeString)GetLibraryProcAddress("ntdll.dll", "RtlInitUnicodeString");

        //Resolve NT Functions
        if ((!NtQuerySystemInformation) || (!NtDuplicateObject) || (!NtQueryObject) || (!NtClose) || (!RtlInitUnicodeString)) {
            BeaconPrintf(CALLBACK_ERROR, "Error: Failed to resolve NT API function addresses");
        }

        // Query the system handles. If the call fails because of a length mismatch, recreate a bigger buffer and try again.
        do
        {
            handleInfo = (PSYSTEM_HANDLE_INFORMATION)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, handleInfoSize);
            dwErrorCode = NtQuerySystemInformation(SystemHandleInformation, handleInfo, handleInfoSize, 0);
            if (dwErrorCode == STATUS_INFO_LENGTH_MISMATCH)
            {
                // The length of the buffer was not sufficient. Expand the buffer before retrying.
                HeapFree(GetProcessHeap(), 0, handleInfo);
                handleInfoSize *= 2;
            }
        } while (dwErrorCode == STATUS_INFO_LENGTH_MISMATCH);

        if (dwErrorCode != STATUS_SUCCESS) {
            HeapFree(GetProcessHeap(), 0, handleInfo);
            BeaconPrintf(CALLBACK_ERROR, "Error: Failed to enumerate system handles: error Code %lu", dwErrorCode);
            return;
        }
        //Iterate through System Handles
        int prevPID = 0;
        for (int i = 0; i < handleInfo->HandleCount; i++) {
            SYSTEM_HANDLE_TABLE_ENTRY_INFO objHandle;
            memset(&objHandle, 0, sizeof(SYSTEM_HANDLE_TABLE_ENTRY_INFO));
            objHandle = handleInfo->Handles[i];
            if (handleInfo->Handles[i].UniqueProcessId != prevPID) {
                prevPID = handleInfo->Handles[i].UniqueProcessId;
                if (processHandle) {
                    NtClose(processHandle);
                    processHandle = NULL;
                }
                processHandle = OpenProcess(PROCESS_DUP_HANDLE, FALSE, handleInfo->Handles[i].UniqueProcessId);
                if (NULL == processHandle) {
                    dwErrorCode = GetLastError();
                    BeaconPrintf(CALLBACK_ERROR, "Error: Failed to open process %i, error code %i", pid, dwErrorCode);
                    continue;
                }

            }
            else {
                if (NULL == processHandle) {
                    continue;
                }
            }

            //Check if handle type is of type file
            if (handleInfo->Handles[i].ObjectTypeIndex != HANDLE_TYPE_FILE) {
                continue;
            }


            UNICODE_STRING objectName;
            ULONG returnLength = 0;
            memset(&objectName, 0, sizeof(UNICODE_STRING));

            //Reset variables and handles
            if (dupHandle) {
                dwErrorCode = NtClose(dupHandle);
                dupHandle = NULL;
            }
            if (objectTypeInfo) {
                HeapFree(GetProcessHeap(), 0, objectTypeInfo);
                objectTypeInfo = NULL;
            }

            if (objectNameInfo) {
                HeapFree(GetProcessHeap(), 0, objectNameInfo);
                objectNameInfo = NULL;
            }

            //Check Access Mask of handle, can run into issues with 0x001a019f or 0x0012019f
            if (handleInfo->Handles[i].GrantedAccess == 0x001a019f || (handleInfo->Handles[i].HandleAttributes == 0x2 && handleInfo->Handles[i].GrantedAccess == 0x0012019f)) {
                continue;
            }

            DWORD currentID = handleInfo->Handles[i].UniqueProcessId;


            //Duplicate Handle (Need DUPLICATE_SAME_ACCESS to be able to read from the file if it is locked.
            //Inherit Handle must be false for handle cleanup (When set to true, was unable to close handle after use). 
            dwErrorCode = (DWORD)NtDuplicateObject(processHandle, (HANDLE)handleInfo->Handles[i].HandleValue, GetCurrentProcess(), &dupHandle, 0, false, DUPLICATE_SAME_ACCESS);

            //Check if handle was successfully duplicated
            if (dwErrorCode != STATUS_SUCCESS) {
                continue;
            }
            //Check if the handle exists on disk, otherwise the program will hang
            DWORD fileType = GetFileType(dupHandle);
            if (fileType != FILE_TYPE_DISK) {
                continue;
            }
            //Allocate memory for objectTypeInfo
            objectTypeInfo = (POBJECT_TYPE_INFORMATION)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x1000);
            //Check if memory was successfully allocated
            if (objectTypeInfo == NULL) {
                continue;

            }

            // Query the object type to get object type information
            dwErrorCode = (DWORD)NtQueryObject(dupHandle, ObjectTypeInformation, objectTypeInfo, 0x1000, NULL);
            //Check if the object type was successfully queries
            if (dwErrorCode != STATUS_SUCCESS) {
                continue;
            }

            //Allocate memory for object name info structure
            objectNameInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x1000);
            if (objectNameInfo == NULL) {
                continue;
            }

            //Retrieve object name info
            dwErrorCode = (DWORD)NtQueryObject(dupHandle, ObjectNameInformation, objectNameInfo, 0x1000, &returnLength);
            if (dwErrorCode != STATUS_SUCCESS) {

                // Reallocate the buffer and try again.
                objectNameInfo = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, objectNameInfo, returnLength);
                if (NULL == objectNameInfo) {
                    continue;
                }
                dwErrorCode = (DWORD)NtQueryObject(dupHandle, ObjectNameInformation, objectNameInfo, 0x1000, NULL);
                if (NULL == objectNameInfo) {
                    continue;
                }
            }

            // Cast our buffer into an UNICODE_STRING.
            objectName = *(PUNICODE_STRING)objectNameInfo;
            if (objectName.Length)
            {

                //Structures to get the fileName
                const wchar_t* filePathBuffer = objectName.Buffer;
                size_t length = objectName.Length / sizeof(wchar_t);

                // Initialize the file name as an empty string
                UNICODE_STRING fileName;
                fileName.Buffer = (PWSTR)(filePathBuffer + length);
                fileName.Length = 0;
                fileName.MaximumLength = 0;

                // Find the last occurrence of the path separator '\'
                for (int i = length - 1; i >= 0; i--) {
                    if (filePathBuffer[i] == '\\') {
                        // Set the file name to the portion of the string after the last '\'
                        fileName.Buffer = (PWSTR)(filePathBuffer + i + 1);
                        fileName.Length = (length - i - 1) * sizeof(wchar_t);
                        fileName.MaximumLength = fileName.Length;
                        break;
                    }
                }

                // Check if the provided file name exists within the unicodeString
                int result = 0;
                if (filename != NULL) {
                    UNICODE_STRING substring;
                    RtlInitUnicodeString(&substring, filename);
                    result = wcscmp(fileName.Buffer, substring.Buffer);
                    //BeaconPrintf(CALLBACK_OUTPUT, "FileName: % .*S", fileName.Length, fileName.Buffer);
                    result = wcscmp(fileName.Buffer, filename);
                    if (!result) {
                        BeaconPrintf(CALLBACK_OUTPUT, "Process ID: %ld, [% #d] % .*S\n", handleInfo->Handles[i].UniqueProcessId, handleInfo->Handles[i].HandleValue, objectName.Length / 2, objectName.Buffer);
                    }
                }

            }

        }
        if (false) {
            BeaconPrintf(CALLBACK_OUTPUT, "Error: Failed to find file handle within the specified process");
        }
    cleanup:
        if (handleInfo) {
            VirtualFree(handleInfo, 0, MEM_RELEASE);
            handleInfo = NULL;
        }
        if (processHandle) {
            CloseHandle(processHandle);
            processHandle = NULL;
        }
        if (objectTypeInfo) {
            HeapFree(GetProcessHeap(), 0, objectTypeInfo);
            objectTypeInfo = NULL;
        }

        if (objectNameInfo) {
            HeapFree(GetProcessHeap(), 0, objectNameInfo);
            objectNameInfo = NULL;
        }

        if (dupHandle) {
            CloseHandle(dupHandle);
            dupHandle = NULL;
        }
        return;
    }

}

// Define a main function for the bebug build
#if defined(_DEBUG) && !defined(_GTEST)

int main(int argc, char* argv[]) {
    // Run BOF's entrypoint
    // To pack arguments for the bof use e.g.: bof::runMocked<int, short, const char*>(go, 6502, 42, "foobar");
    //bof::runMocked<int, wchar_t*, wchar_t*>(go, 6696, L"filename", L"Cookies");

    bof::runMocked<wchar_t*,wchar_t*>(go, L"Cookies",L"/Process");
    return 0;
}

// Define unit tests
#elif defined(_GTEST)
#include <gtest\gtest.h>

TEST(BofTest, Test1) {
    std::vector<bof::output::OutputEntry> got =
        bof::runMocked<>(go);
    std::vector<bof::output::OutputEntry> expected = {
        {CALLBACK_OUTPUT, "System Directory: C:\\Windows\\system32"}
    };
    // It is possible to compare the OutputEntry vectors, like directly
    // ASSERT_EQ(expected, got);
    // However, in this case, we want to compare the output, ignoring the case.
    ASSERT_EQ(expected.size(), got.size());
    ASSERT_STRCASEEQ(expected[0].output.c_str(), got[0].output.c_str());
}
#endif
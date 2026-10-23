#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <vector>
#include <string>
#include <set>
#include <iostream>

#include "MutantsMonitor.h";

#pragma comment(lib, "ntdll.lib")

#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif
#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif

#ifndef ObjectNameInformation
#define ObjectNameInformation 1
#define ObjectTypeInformation 2
#endif

#define SystemExtendedHandleInformation 64

typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX {
    PVOID       Object;
    ULONG_PTR   UniqueProcessId;
    HANDLE      HandleValue;
    ULONG       GrantedAccess;
    USHORT      CreatorBackTraceIndex;
    USHORT      ObjectTypeIndex;
    ULONG       HandleAttributes;
    ULONG       Reserved;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX, * PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;

typedef struct _SYSTEM_HANDLE_INFORMATION_EX {
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
} SYSTEM_HANDLE_INFORMATION_EX, * PSYSTEM_HANDLE_INFORMATION_EX;

typedef struct _OBJECT_NAME_INFORMATION {
    UNICODE_STRING Name;
    WCHAR          NameBuffer[1];
} OBJECT_NAME_INFORMATION, * POBJECT_NAME_INFORMATION;

typedef struct _OBJECT_TYPE_INFORMATION {
    UNICODE_STRING TypeName;
    ULONG          TotalNumberOfObjects;
    ULONG          TotalNumberOfHandles;
    ULONG          TotalPagedPoolUsage;
    ULONG          TotalNonPagedPoolUsage;
    ULONG          TotalNamePoolUsage;
    ULONG          TotalHandleTableUsage;
    ULONG          HighWaterNumberOfObjects;
    ULONG          HighWaterNumberOfHandles;
    ULONG          HighWaterPagedPoolUsage;
    ULONG          HighWaterNonPagedPoolUsage;
    ULONG          HighWaterNamePoolUsage;
    ULONG          HighWaterHandleTableUsage;
    ULONG          InvalidAttributes;
    GENERIC_MAPPING GenericMapping;
    ULONG          ValidAccessMask;
    BOOLEAN        SecurityRequired;
    BOOLEAN        MaintainHandleCount;
    UCHAR          TypeIndex;
    CHAR           ReservedByte;
    ULONG          PoolType;
    ULONG          DefaultPagedPoolCharge;
    ULONG          DefaultNonPagedPoolCharge;
} OBJECT_TYPE_INFORMATION, * POBJECT_TYPE_INFORMATION;

typedef NTSTATUS(NTAPI* pNtQuerySystemInformation)(
    SYSTEM_INFORMATION_CLASS SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
    );

typedef NTSTATUS(NTAPI* pNtQueryObject)(
    HANDLE Handle,
    OBJECT_INFORMATION_CLASS ObjectInformationClass,
    PVOID ObjectInformation,
    ULONG ObjectInformationLength,
    PULONG ReturnLength
    );

static pNtQuerySystemInformation ptrNtQuerySystemInformation = nullptr;
static pNtQueryObject ptrNtQueryObject = nullptr;

static std::wstring GetObjectTypeName(HANDLE hObject) {
    ULONG returnLength = 0;
    NTSTATUS status = ptrNtQueryObject(
        hObject, (OBJECT_INFORMATION_CLASS)ObjectTypeInformation,
        nullptr, 0, &returnLength);

    if (status != STATUS_INFO_LENGTH_MISMATCH && status != STATUS_BUFFER_TOO_SMALL) {
        return L""; 
    }

    std::vector<BYTE> buffer(returnLength + 256);
    status = ptrNtQueryObject(
        hObject, (OBJECT_INFORMATION_CLASS)ObjectTypeInformation,
        buffer.data(), (ULONG)buffer.size(), &returnLength);

    if (!NT_SUCCESS(status)) return L"";

    auto* typeInfo = reinterpret_cast<POBJECT_TYPE_INFORMATION>(buffer.data());
    if (typeInfo->TypeName.Buffer && typeInfo->TypeName.Length > 0) {
        return std::wstring(typeInfo->TypeName.Buffer, typeInfo->TypeName.Length / sizeof(WCHAR));
    }
    return L"";
}

static std::wstring GetObjectName(HANDLE hObject) {
    ULONG returnLength = 0;
    NTSTATUS status = ptrNtQueryObject(
        hObject, (OBJECT_INFORMATION_CLASS)ObjectNameInformation,
        nullptr, 0, &returnLength);

    if (status != STATUS_INFO_LENGTH_MISMATCH && status != STATUS_BUFFER_TOO_SMALL) {
        return L"";
    }

    std::vector<BYTE> buffer(returnLength + 256);
    status = ptrNtQueryObject(
        hObject, (OBJECT_INFORMATION_CLASS)ObjectNameInformation,
        buffer.data(), (ULONG)buffer.size(), &returnLength);

    if (!NT_SUCCESS(status)) return L"";

    auto* nameInfo = reinterpret_cast<POBJECT_NAME_INFORMATION>(buffer.data());
    if (nameInfo->Name.Buffer && nameInfo->Name.Length > 0) {
        return std::wstring(nameInfo->Name.Buffer, nameInfo->Name.Length / sizeof(WCHAR));
    }
    return L"";
}

static std::wstring GetCleanObjectName(const std::wstring& source) {
    size_t pos = source.find_last_of(L'\\');
    if (pos != std::wstring::npos && pos + 1 < source.length()) {
        return source.substr(pos + 1);
    }
    return source;
}

bool InitializeNtFunctions() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;

    ptrNtQuerySystemInformation = (pNtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    ptrNtQueryObject = (pNtQueryObject)GetProcAddress(hNtdll, "NtQueryObject");

    return (ptrNtQuerySystemInformation != nullptr && ptrNtQueryObject != nullptr);
}

std::set<std::wstring> EnumerateNamedMutexes() {
    std::set<std::wstring> result;

    if (!ptrNtQuerySystemInformation || !ptrNtQueryObject) {
        return result;
    }

    ULONG bufferSize = 0x100000;
    std::vector<BYTE> buffer;
    NTSTATUS status;

    do {
        buffer.resize(bufferSize);
        status = ptrNtQuerySystemInformation(
            (SYSTEM_INFORMATION_CLASS)SystemExtendedHandleInformation,
            buffer.data(), bufferSize, &bufferSize);

        if (status == STATUS_INFO_LENGTH_MISMATCH || status == STATUS_BUFFER_TOO_SMALL) {
            bufferSize = bufferSize * 2 + 0x10000;
        }
        else if (!NT_SUCCESS(status)) {
            return result;
        }
    } while (status == STATUS_INFO_LENGTH_MISMATCH || status == STATUS_BUFFER_TOO_SMALL);

    auto* handleInfo = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION_EX>(buffer.data());

    for (ULONG_PTR i = 0; i < handleInfo->NumberOfHandles; ++i) {
        const auto& entry = handleInfo->Handles[i];

        if (entry.UniqueProcessId == GetCurrentProcessId()) continue;

        HANDLE hProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, (DWORD)entry.UniqueProcessId);
        if (!hProcess) continue;

        HANDLE hDup = nullptr;
        BOOL dupOk = DuplicateHandle(hProcess, entry.HandleValue, GetCurrentProcess(), &hDup, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CloseHandle(hProcess);

        if (!dupOk || !hDup) continue;

        if (GetObjectTypeName(hDup) == L"Mutant") {
            std::wstring fullName = GetObjectName(hDup);
            if (!fullName.empty()) {
                result.insert(GetCleanObjectName(fullName));
            }
        }
        CloseHandle(hDup);
    }

    return result;
}
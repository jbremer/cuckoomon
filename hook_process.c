/*
Cuckoo Sandbox - Automated Malware Analysis
Copyright (C) 2010-2012 Cuckoo Sandbox Developers

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <windows.h>
#include "hooking.h"
#include "ntapi.h"
#include "log.h"
#include "pipe.h"
#include "misc.h"

static IS_SUCCESS_NTSTATUS();
static const char *module_name = "process";

static void notify_pipe(DWORD process_id)
{
    char buf[32] = {}; int len = sizeof(buf);
    pipe_write_read(buf, &len, "PID:%d", process_id);
}

HOOKDEF(NTSTATUS, WINAPI, NtCreateProcess,
    __out       PHANDLE ProcessHandle,
    __in        ACCESS_MASK DesiredAccess,
    __in_opt    POBJECT_ATTRIBUTES ObjectAttributes,
    __in        HANDLE ParentProcess,
    __in        BOOLEAN InheritObjectTable,
    __in_opt    HANDLE SectionHandle,
    __in_opt    HANDLE DebugPort,
    __in_opt    HANDLE ExceptionPort
) {
    NTSTATUS ret = Old_NtCreateProcess(ProcessHandle, DesiredAccess,
        ObjectAttributes, ParentProcess, InheritObjectTable, SectionHandle,
        DebugPort, ExceptionPort);
    LOQ("PO", "ProcessHandle", ProcessHandle, "FileName", ObjectAttributes);
    if(NT_SUCCESS(ret)) {
        notify_pipe(GetPidFromProcessHandle(*ProcessHandle));
    }
    return ret;
}

HOOKDEF(NTSTATUS, WINAPI, NtCreateProcessEx,
    __out       PHANDLE ProcessHandle,
    __in        ACCESS_MASK DesiredAccess,
    __in_opt    POBJECT_ATTRIBUTES ObjectAttributes,
    __in        HANDLE ParentProcess,
    __in        ULONG Flags,
    __in_opt    HANDLE SectionHandle,
    __in_opt    HANDLE DebugPort,
    __in_opt    HANDLE ExceptionPort,
    __in        BOOLEAN InJob
) {
    NTSTATUS ret = Old_NtCreateProcessEx(ProcessHandle, DesiredAccess,
        ObjectAttributes, ParentProcess, Flags, SectionHandle, DebugPort,
        ExceptionPort, InJob);
    LOQ("PO", "ProcessHandle", ProcessHandle, "FileName", ObjectAttributes);
    if(NT_SUCCESS(ret)) {
        notify_pipe(GetPidFromProcessHandle(*ProcessHandle));
    }
    return ret;
}

HOOKDEF(BOOL, WINAPI, CreateProcessInternalW,
    __in_opt    LPVOID lpUnknown1,
    __in_opt    LPWSTR lpApplicationName,
    __inout_opt LPWSTR lpCommandLine,
    __in_opt    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    __in_opt    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    __in        BOOL bInheritHandles,
    __in        DWORD dwCreationFlags,
    __in_opt    LPVOID lpEnvironment,
    __in_opt    LPWSTR lpCurrentDirectory,
    __in        LPSTARTUPINFO lpStartupInfo,
    __out       LPPROCESS_INFORMATION lpProcessInformation,
    __in_opt    LPVOID lpUnknown2
) {
    IS_SUCCESS_BOOL();

    BOOL ret = Old_CreateProcessInternalW(lpUnknown1, lpApplicationName,
        lpCommandLine, lpProcessAttributes, lpThreadAttributes,
        bInheritHandles, dwCreationFlags | CREATE_SUSPENDED, lpEnvironment,
        lpCurrentDirectory, lpStartupInfo, lpProcessInformation, lpUnknown2);
    LOQ("uu3l2p", "ApplicationName", lpApplicationName,
        "CommandLine", lpCommandLine, "CreationFlags", dwCreationFlags,
        "ProcessId", lpProcessInformation->dwProcessId,
        "ThreadId", lpProcessInformation->dwThreadId,
        "ProcessHandle", lpProcessInformation->hProcess,
        "ThreadHandle", lpProcessInformation->hThread);
    if(ret != FALSE) {
        notify_pipe(lpProcessInformation->dwProcessId);

        // if the CREATE_SUSPENDED flag was not set, then we have to resume
        // the main thread ourself
        if((dwCreationFlags & CREATE_SUSPENDED) == 0) {
            ResumeThread(lpProcessInformation->hThread);
        }
    }
    return ret;
}

HOOKDEF(HANDLE, WINAPI, OpenProcess,
  __in  DWORD dwDesiredAccess,
  __in  BOOL bInheritHandle,
  __in  DWORD dwProcessId
) {
    IS_SUCCESS_HANDLE();

    HANDLE ret = Old_OpenProcess(dwDesiredAccess, bInheritHandle,
        dwProcessId);
    LOQ("ll", "DesiredAccess", dwDesiredAccess, "ProcessId", dwProcessId);
    return ret;
}

HOOKDEF(BOOL, WINAPI, TerminateProcess,
  __in  HANDLE hProcess,
  __in  UINT uExitCode
) {
    IS_SUCCESS_BOOL();

    BOOL ret = Old_TerminateProcess(hProcess, uExitCode);
    LOQ("pl", "ProcessHandle", hProcess, "ExitCode", uExitCode);
    return ret;
}

HOOKDEF(VOID, WINAPI, ExitProcess,
  __in  UINT uExitCode
) {
    IS_SUCCESS_VOID();

    int ret = 0;
    LOQ("l", "ExitCode", uExitCode);
    Old_ExitProcess(uExitCode);
}

HOOKDEF(BOOL, WINAPI, ShellExecuteExW,
  __inout  SHELLEXECUTEINFOW *pExecInfo
) {
    IS_SUCCESS_BOOL();

    BOOL ret = Old_ShellExecuteExW(pExecInfo);
    LOQ("2ul", "FilePath", pExecInfo->lpFile,
        "Parameters", pExecInfo->lpParameters, "Show", pExecInfo->nShow);
    return ret;
}

HOOKDEF(NTSTATUS, WINAPI, NtReadVirtualMemory,
    __in        HANDLE ProcessHandle,
    __in        LPCVOID BaseAddress,
    __out       LPVOID Buffer,
    __in        ULONG NumberOfBytesToRead,
    __out_opt   PULONG NumberOfBytesReaded
) {
    IS_SUCCESS_NTSTATUS();

    ENSURE_ULONG(NumberOfBytesReaded);

    BOOL ret = Old_NtReadVirtualMemory(ProcessHandle, BaseAddress, Buffer,
        NumberOfBytesToRead, NumberOfBytesReaded);
    LOQ("2pB", "ProcessHandle", ProcessHandle, "BaseAddress", BaseAddress,
        "Buffer", NumberOfBytesReaded, Buffer);
    return ret;
}

HOOKDEF(NTSTATUS, WINAPI, NtWriteVirtualMemory,
    __in        HANDLE ProcessHandle,
    __in        LPVOID BaseAddress,
    __in        LPCVOID Buffer,
    __in        ULONG NumberOfBytesToWrite,
    __out_opt   ULONG *NumberOfBytesWritten
) {
    IS_SUCCESS_NTSTATUS();

    ENSURE_ULONG(NumberOfBytesWritten);

    BOOL ret = Old_NtWriteVirtualMemory(ProcessHandle, BaseAddress, Buffer,
        NumberOfBytesToWrite, NumberOfBytesWritten);
    LOQ("2pB", "ProcessHandle", ProcessHandle, "BaseAddress", BaseAddress,
        "Buffer", NumberOfBytesWritten, Buffer);
    return ret;
}

HOOKDEF(LPVOID, WINAPI, VirtualAllocEx,
    __in      HANDLE hProcess,
    __in_opt  LPVOID lpAddress,
    __in      SIZE_T dwSize,
    __in      DWORD flAllocationType,
    __in      DWORD flProtect
) {
    IS_SUCCESS_HANDLE();

    LPVOID ret = Old_VirtualAllocEx(hProcess, lpAddress, dwSize,
        flAllocationType, flProtect);
    LOQ("pplll", "ProcessHandle", hProcess, "Address", lpAddress,
        "Size", dwSize, "AllocationType", flAllocationType,
        "Protection", flProtect);
    return ret;
}

HOOKDEF(BOOL, WINAPI, VirtualProtectEx,
    __in   HANDLE hProcess,
    __in   LPVOID lpAddress,
    __in   SIZE_T dwSize,
    __in   DWORD flNewProtect,
    __out  PDWORD lpflOldProtect
) {
    IS_SUCCESS_BOOL();

    BOOL ret = Old_VirtualProtectEx(hProcess, lpAddress, dwSize, flNewProtect,
        lpflOldProtect);
    LOQ("2p2l", "ProcessHandle", hProcess, "Address", lpAddress,
        "Size", dwSize, "Protection", flNewProtect);
    return ret;
}

HOOKDEF(BOOL, WINAPI, VirtualFreeEx,
    __in  HANDLE hProcess,
    __in  LPVOID lpAddress,
    __in  SIZE_T dwSize,
    __in  DWORD dwFreeType
) {
    IS_SUCCESS_BOOL();

    BOOL ret = Old_VirtualFreeEx(hProcess, lpAddress, dwSize, dwFreeType);
    LOQ("ppll", "ProcessHandle", hProcess, "Address", lpAddress,
        "Size", dwSize, "FreeType", dwFreeType);
    return ret;
}

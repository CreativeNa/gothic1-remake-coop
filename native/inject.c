/* inject.exe <pid> <full path to dll> : LoadLibraryA in the target process (test copy only). */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: inject <pid> <dll>\n"); return 2; }
    DWORD pid = (DWORD)atoi(argv[1]);
    const char *dll = argv[2];
    HANDLE p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!p) { fprintf(stderr, "OpenProcess failed %lu\n", GetLastError()); return 1; }
    SIZE_T n = strlen(dll) + 1;
    LPVOID mem = VirtualAllocEx(p, NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(p, mem, dll, n, NULL)) { fprintf(stderr, "alloc/write failed %lu\n", GetLastError()); return 1; }
    HANDLE t = CreateRemoteThread(p, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"), mem, 0, NULL);
    if (!t) { fprintf(stderr, "CreateRemoteThread failed %lu\n", GetLastError()); return 1; }
    WaitForSingleObject(t, 10000);
    DWORD code = 0; GetExitCodeThread(t, &code);
    printf("LoadLibrary returned 0x%lx (nonzero = loaded)\n", code);
    return code ? 0 : 1;
}

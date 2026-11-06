/* ftcall: one-shot caller of a Full Tilt exe's stdcall function (code, part), run under the same Wine prefix as ftpeek.
 *
 *   wine ftcall.exe DRAGON.EXE <func-hex> <code-hex> <partchain>
 *
 * <partchain> is the ftpeek chain syntax (hex numbers joined by ':', at most 4); the 32-bit value at the resolved
 * address is the part pointer (a tag's component pointer is at tag+4, so "43ac00" can be written "43ac04" for a tag
 * at 0x43ac00). The tool writes a 15-byte stub into the game process (VirtualAllocEx):
 *     push part ; push code ; call func ; ret 4   (the target pops its 8 bytes of arguments: both
 * dispatchers end in `ret 8`, checked with capstone)
 * and runs it on a new remote thread (CreateRemoteThread), waits for it and prints
 * "<func>\t<code>\t<part>\t<thread exit>\tok|fail". Used by UTe to make the original's own control dispatcher
 * (DRAGON 0x423060, PIRATES 0x423080: MissionControl pre-hook, then the part's control function) run for a chosen
 * part, the same call the native hook's `event <part> ControlCollision` makes. It does not coordinate with the game
 * thread: func 0 starts the thread at the final `ret 4` (a thread-start test). Call it while the game is idle (ball in the plunger lane).
 * Exit codes: 0 ok, 2 bad arguments, 3 process not found within 30 s, 4 OpenProcess failed, 5 a read/alloc/thread failed.
 * Build: i686-w64-mingw32-gcc -O2 -o re/work/evidence/reference/2026-11-01/tools/ftcall.exe re/test/scripts/ftcall.c
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD find_pid(const char *exe) {
    PROCESSENTRY32 pe;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;
    if (snap == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, exe) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static int hex(const char *s, unsigned long *v) {
    char *end;
    *v = strtoul(s, &end, 16);
    return (end == s || *end) ? -1 : 0;
}

int main(int argc, char **argv) {
    unsigned long func, code, chain[4], a, v = 0, part = 0, exitcode = 0xdeadbeef;
    int len = 0, k, wait_ms;
    char *p, *end, *spec;
    unsigned char stub[15];
    DWORD pid = 0;
    HANDLE h, th;
    SIZE_T got = 0;
    void *mem;

    if (argc != 5 || hex(argv[2], &func) || hex(argv[3], &code)) {
        fprintf(stderr, "usage: ftcall EXE func-hex code-hex partchain\n");
        return 2;
    }
    spec = _strdup(argv[4]);
    for (p = spec; *p;) {
        if (len >= 4) { fprintf(stderr, "chain too long\n"); return 2; }
        chain[len++] = strtoul(p, &end, 16);
        if (end == p) { fprintf(stderr, "bad chain\n"); return 2; }
        p = end;
        if (*p == ':') { p++; if (!*p) { fprintf(stderr, "bad chain\n"); return 2; } }
        else if (*p) { fprintf(stderr, "bad chain\n"); return 2; }
    }
    if (!len) { fprintf(stderr, "bad chain\n"); return 2; }
    for (wait_ms = 0; wait_ms < 30000 && !(pid = find_pid(argv[1])); wait_ms += 200) Sleep(200);
    if (!pid) { fprintf(stderr, "process %s not found\n", argv[1]); return 3; }
    h = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_CREATE_THREAD |
                    PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) { fprintf(stderr, "OpenProcess failed %lu\n", GetLastError()); return 4; }
    a = chain[0];
    for (k = 1; k < len; k++) {
        if (!ReadProcessMemory(h, (LPCVOID)a, &v, 4, &got) || got != 4) { fprintf(stderr, "chain read failed\n"); return 5; }
        a = v + chain[k];
    }
    if (!ReadProcessMemory(h, (LPCVOID)a, &part, 4, &got) || got != 4 || !part) {
        fprintf(stderr, "part pointer read failed or null\n");
        return 5;
    }
    mem = VirtualAllocEx(h, NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) { fprintf(stderr, "VirtualAllocEx failed %lu\n", GetLastError()); return 5; }
    stub[0] = 0x68; memcpy(stub + 1, &part, 4);
    stub[5] = 0x68; memcpy(stub + 6, &code, 4);
    stub[10] = 0xE8;
    {   /* rel32: the instruction after the call is at +15 */
        long rel = (long)func - ((long)mem + 15);
        memcpy(stub + 11, &rel, 4);
    }
    {
        unsigned char tail[3] = {0xC2, 0x04, 0x00};
        if (!WriteProcessMemory(h, mem, stub, 15, &got) || got != 15 ||
            !WriteProcessMemory(h, (unsigned char *)mem + 15, tail, 3, &got) || got != 3) {
            fprintf(stderr, "stub write failed %lu\n", GetLastError());
            return 5;
        }
    }
    th = CreateRemoteThread(h, NULL, 0, (LPTHREAD_START_ROUTINE)((unsigned char *)mem + (func ? 0 : 15)), NULL, 0, NULL);
    if (!th) { fprintf(stderr, "CreateRemoteThread failed %lu\n", GetLastError()); return 5; }
    if (WaitForSingleObject(th, 10000) != WAIT_OBJECT_0) { fprintf(stderr, "thread did not finish\n"); return 5; }
    GetExitCodeThread(th, &exitcode);
    printf("%lx\t%lx\t%lx\t%lu\tok\n", func, code, part, exitcode);
    fflush(stdout);
    CloseHandle(th);
    VirtualFreeEx(h, mem, 0, MEM_RELEASE);
    CloseHandle(h);
    return 0;
}

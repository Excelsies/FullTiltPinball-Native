/* ftpeek: read-only sampler of a Full Tilt exe's rule globals, run under the same Wine prefix.
 *
 *   wine ftpeek.exe DRAGON.EXE <period_ms> <max_samples> <spec>...
 *
 * A spec is a chain of hex numbers separated by ':': the first is an address, every further
 * number means "read the 32-bit value here, add this offset". "434d2c" = int at 0x434d2c;
 * "439b00:192" = int at (*(u32 *)0x439b00) + 0x192. Each line on stdout is
 * "<ms since start>\t<v1>\t<v2>..." and is printed only when a value changed. A failed read
 * prints "?" for that value. Exits when the target process exits or after max_samples.
 *
 * Why a Windows tool: the Wine game process is re-parented to the user's systemd, so with
 * kernel.yama.ptrace_scope = 1 a Linux reader may not open /proc/<pid>/mem; ReadProcessMemory
 * inside the prefix goes through wineserver, which may.
 * Build: i686-w64-mingw32-gcc -O2 -o re/work/evidence/reference/2026-11-01/tools/ftpeek.exe re/test/scripts/ftpeek.c
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SPECS 64
#define MAX_CHAIN 4

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

int main(int argc, char **argv) {
    unsigned long chain[MAX_SPECS][MAX_CHAIN];
    int len[MAX_SPECS], nspec, i, k, wait_ms;
    long last[MAX_SPECS], cur[MAX_SPECS];
    int okl[MAX_SPECS], okc[MAX_SPECS], first = 1;
    long period, max_samples, n;
    DWORD pid = 0, t0, code;
    HANDLE h;

    if (argc < 5) { fprintf(stderr, "usage: ftpeek EXE period_ms max_samples spec...\n"); return 2; }
    period = strtol(argv[2], NULL, 10);
    max_samples = strtol(argv[3], NULL, 10);
    nspec = argc - 4;
    if (nspec > MAX_SPECS || period < 1 || max_samples < 1) { fprintf(stderr, "bad args\n"); return 2; }
    for (i = 0; i < nspec; i++) {
        char *p = argv[4 + i];
        len[i] = 0;
        while (*p && len[i] < MAX_CHAIN) {
            chain[i][len[i]++] = strtoul(p, &p, 16);
            if (*p == ':') p++;
        }
        if (*p) { fprintf(stderr, "spec %s too long\n", argv[4 + i]); return 2; }
    }
    for (wait_ms = 0; wait_ms < 30000 && !(pid = find_pid(argv[1])); wait_ms += 200) Sleep(200);
    if (!pid) { fprintf(stderr, "process %s not found\n", argv[1]); return 3; }
    h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!h) { fprintf(stderr, "OpenProcess failed %lu\n", GetLastError()); return 4; }
    fprintf(stderr, "attached pid %lu\n", pid);
    t0 = GetTickCount();
    for (n = 0; n < max_samples; n++) {
        int changed = first;
        if (GetExitCodeProcess(h, &code) && code != STILL_ACTIVE) break;
        for (i = 0; i < nspec; i++) {
            unsigned long addr = chain[i][0], v = 0;
            SIZE_T got = 0;
            okc[i] = 1;
            for (k = 0; k < len[i]; k++) {
                if (k) addr = v + chain[i][k];
                if (!ReadProcessMemory(h, (LPCVOID)addr, &v, 4, &got) || got != 4) { okc[i] = 0; break; }
            }
            cur[i] = (long)v;
            if (first || okc[i] != okl[i] || cur[i] != last[i]) changed = 1;
        }
        if (changed) {
            printf("%lu", (unsigned long)(GetTickCount() - t0));
            for (i = 0; i < nspec; i++) {
                if (okc[i]) printf("\t%ld", cur[i]); else printf("\t?");
                last[i] = cur[i]; okl[i] = okc[i];
            }
            printf("\n");
            fflush(stdout);
            first = 0;
        }
        Sleep((DWORD)period);
    }
    CloseHandle(h);
    return 0;
}

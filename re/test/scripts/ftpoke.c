/* ftpoke: one-shot writer of a Full Tilt exe's 32-bit globals, run under the same Wine prefix as ftpeek.
 *
 *   wine ftpoke.exe DRAGON.EXE <spec>=<hexvalue>...
 *
 * <spec> is the ftpeek chain syntax: hex numbers separated by ':'; the first is an address, every
 * further number means "read the 32-bit value here, add this offset". The last address of the chain
 * is the one written: "43bc74=3039" writes 0x3039 at 0x43bc74; "439b00:192=0" writes 0 at
 * (*(u32 *)0x439b00) + 0x192. Each write is read back and reported on stdout as
 * "<spec>\t<old>\t<new>\tok|fail" (old/new as signed decimals, as ftpeek prints them). "ok" means
 * the write succeeded and the read-back equals the value written; a game thread may legitimately
 * change a live global between the write and the read-back, which then shows as "fail".
 * Seeding DRAGON.EXE's holdrand (43bc74) therefore reports "ok" only during about the first 2-3 s
 * after launch, before the game loop calls rand(); later the write still succeeds but the value is
 * advanced before the read-back, so the line shows the advanced value with "fail" and exit 5.
 * The value is 1-8 hex digits (optional 0x prefix); anything else exits 2 before waiting.
 *
 * Exit codes (ftpeek's plus 5): 0 all written, 2 bad arguments, 3 process not found within 30 s,
 * 4 OpenProcess failed, 5 a chain read or a write failed, or a read-back differed.
 * Why a Windows tool: see ftpeek.c (ptrace_scope); WriteProcessMemory goes through wineserver.
 * Build: i686-w64-mingw32-gcc -O2 -o re/work/evidence/reference/<date>/tools/ftpoke.exe re/test/scripts/ftpoke.c
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

/* The value must be 1-8 hex digits, optionally after "0x": no sign, no blanks, no overflow. */
static int parse_value(const char *s, unsigned long *value) {
    int n = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (*value = 0; *s; s++, n++) {
        int c = (unsigned char)*s, d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        if (n >= 8) return -1;
        *value = (*value << 4) | (unsigned long)d;
    }
    return n ? 0 : -1;
}

/* Parses "chain=value" in place. Returns 0 on success. */
static int parse_spec(char *arg, unsigned long *chain, int *len, unsigned long *value) {
    char *eq = strchr(arg, '='), *p, *end;
    if (!eq || eq == arg || !eq[1]) return -1;
    *eq = 0;
    if (parse_value(eq + 1, value)) return -1;
    *len = 0;
    for (p = arg; *p;) {
        if (*len >= MAX_CHAIN) return -1;
        chain[(*len)++] = strtoul(p, &end, 16);
        if (end == p) return -1;
        p = end;
        if (*p == ':') { p++; if (!*p) return -1; }
        else if (*p) return -1;
    }
    return *len ? 0 : -1;
}

/* Follows the chain to its final address (all but the last link are read). */
static int resolve(HANDLE h, const unsigned long *chain, int len, unsigned long *addr) {
    unsigned long a = chain[0], v = 0;
    SIZE_T got = 0;
    int k;
    for (k = 1; k < len; k++) {
        if (!ReadProcessMemory(h, (LPCVOID)a, &v, 4, &got) || got != 4) return -1;
        a = v + chain[k];
    }
    *addr = a;
    return 0;
}

int main(int argc, char **argv) {
    unsigned long chain[MAX_SPECS][MAX_CHAIN], value[MAX_SPECS];
    char *name[MAX_SPECS];
    int len[MAX_SPECS], nspec, i, wait_ms, rc = 0;
    DWORD pid = 0;
    HANDLE h;

    if (argc < 3) { fprintf(stderr, "usage: ftpoke EXE spec=hexvalue...\n"); return 2; }
    nspec = argc - 2;
    if (nspec > MAX_SPECS) { fprintf(stderr, "too many specs\n"); return 2; }
    for (i = 0; i < nspec; i++) {
        name[i] = _strdup(argv[2 + i]);
        if (!name[i]) { fprintf(stderr, "out of memory\n"); return 2; }
        if (parse_spec(argv[2 + i], chain[i], &len[i], &value[i])) {
            fprintf(stderr, "bad spec %s (want chain=hexvalue)\n", name[i]);
            return 2;
        }
        *strchr(name[i], '=') = 0; /* parse_spec accepted it, so '=' exists: keep the chain part */
    }
    for (wait_ms = 0; wait_ms < 30000 && !(pid = find_pid(argv[1])); wait_ms += 200) Sleep(200);
    if (!pid) { fprintf(stderr, "process %s not found\n", argv[1]); return 3; }
    h = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) { fprintf(stderr, "OpenProcess failed %lu\n", GetLastError()); return 4; }
    fprintf(stderr, "attached pid %lu\n", pid);
    for (i = 0; i < nspec; i++) {
        unsigned long addr = 0, old = 0, cur = 0, val = value[i];
        SIZE_T got = 0;
        int ok = 0;
        if (resolve(h, chain[i], len[i], &addr)) {
            fprintf(stderr, "%s: chain read failed %lu\n", name[i], GetLastError());
        } else if (!ReadProcessMemory(h, (LPCVOID)addr, &old, 4, &got) || got != 4) {
            fprintf(stderr, "%s: read failed %lu\n", name[i], GetLastError());
        } else if (!WriteProcessMemory(h, (LPVOID)addr, &val, 4, &got) || got != 4) {
            fprintf(stderr, "%s: write failed %lu\n", name[i], GetLastError());
        } else if (!ReadProcessMemory(h, (LPCVOID)addr, &cur, 4, &got) || got != 4) {
            fprintf(stderr, "%s: read-back failed %lu\n", name[i], GetLastError());
        } else {
            ok = (cur == val);
        }
        printf("%s\t%ld\t%ld\t%s\n", name[i], (long)old, (long)cur, ok ? "ok" : "fail");
        fflush(stdout);
        if (!ok) rc = 5;
    }
    CloseHandle(h);
    return rc;
}

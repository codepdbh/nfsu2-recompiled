/*
 * main.c - Windows host for the recompiled SPEED2.EXE (pcrecomp native32).
 *
 *   NFSU2-Recompiled.exe [--game-root DIR] [--trace-native] [--trace-callbacks]
 *                        [pcrecomp trace options, see recomp_trace_help]
 *
 * Boot:
 *   1. reserve the guest image range (0x400000..) before anything else lands there
 *   2. find GAME_ROOT (the folder with SPEED2.EXE) and make it the current dir
 *   3. map SPEED2.EXE at its base (read-only use of the original file), bind
 *      its imports to real Windows (plus src/shims), mask 3DNow! out of CPUID
 *   4. run the lifted entry point
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
#include "recomp_trace.h"
#include "native32.h"
#include "nfs_runtime.h"
#include "nfs_widescreen.h"
#include "nfs_log.h"

char g_nfs_game_root[MAX_PATH];
char g_nfs_exe_path[MAX_PATH];

extern native32_shim_t g_nfs_shims[];
extern int g_nfs_nshims;

#define GUEST_SPAN 0x00532000u   /* SizeOfImage of the tested SPEED2.EXE */

static int file_exists(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Walk up from the host exe looking for "<dir>/Need for Speed Underground 2/SPEED2.EXE"
 * or "<dir>/SPEED2.EXE". */
static int find_game_root(char *out) {
    char p[MAX_PATH];
    GetModuleFileNameA(NULL, p, MAX_PATH);
    for (int up = 0; up < 8; up++) {
        char *s = strrchr(p, '\\');
        if (!s) break;
        *s = 0;
        char c[MAX_PATH];
        snprintf(c, sizeof c, "%s\\SPEED2.EXE", p);
        if (file_exists(c)) { strcpy(out, p); return 1; }
        snprintf(c, sizeof c, "%s\\Need for Speed Underground 2\\SPEED2.EXE", p);
        if (file_exists(c)) { snprintf(out, MAX_PATH, "%s\\Need for Speed Underground 2", p); return 1; }
    }
    return 0;
}

#define GAME_THREAD_STACK (256u << 20)

/* Progress heartbeat: which lifted function owns the machine, and how many
 * indirect/native calls have happened. Racy reads, diagnostics only. Tells a
 * spin (calls frozen, function constant) from progress at a glance. */
static DWORD WINAPI watchdog(void *p) {
    (void)p;
    uint32_t last_ic = 0, last_nat = 0;
    for (;;) {
        Sleep(5000);
        uint32_t ic = g_icall_count, nat = g_native_ring_idx;
        const char *imp = g_cur_import;
        NFS_LOG(CPU, "heartbeat: sub_%08X  icalls +%u  native +%u  frames %u  last import %s",
                g_cur_func, ic - last_ic, nat - last_nat, nfs_d3d9_frames(), imp ? imp : "-");
        last_ic = ic; last_nat = nat;
        nfs_log_flush();
    }
}

/* NFSU2_NATIVE=1: the oracle. The ORIGINAL x86 code runs natively in this
 * host (same image mapping, same import shims, windowed mode through the
 * vtable hooks), so its behaviour can be compared with the recompiled code
 * without the fullscreen original. Lifted code is not used at all. */
static int g_native;

static void make_guest_code_executable(void) {
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(uintptr_t)(NFS_GUEST_BASE +
        ((IMAGE_DOS_HEADER *)(uintptr_t)NFS_GUEST_BASE)->e_lfanew);
    IMAGE_SECTION_HEADER *sh = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sh++) {
        DWORD old;
        if (sh->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            VirtualProtect((void *)(uintptr_t)(NFS_GUEST_BASE + sh->VirtualAddress), sh->Misc.VirtualSize,
                           PAGE_EXECUTE_READWRITE, &old);
    }
}

static DWORD WINAPI game_thread(void *p) {
    if (g_native) {
        ((void (*)(void))p)();           /* WinMainCRTStartup; ends in ExitProcess */
        return 0;
    }
    native32_call_guest((uint32_t)(uintptr_t)p, 0, NULL);
    return g_eax;
}

/* Is [base, base+span) one reservation of ours (made by the launcher)? */
static int guest_range_reserved(void) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)(uintptr_t)NFS_GUEST_BASE, &mbi, sizeof mbi)) return 0;
    return mbi.State == MEM_RESERVE && (uint32_t)(uintptr_t)mbi.AllocationBase == NFS_GUEST_BASE &&
           mbi.RegionSize >= GUEST_SPAN;
}

/* The launcher. By the time main() runs, the Windows loader has mapped NLS
 * tables, locale files and heaps all over 0x400000-0x930000, where SPEED2.EXE
 * has to live (it has no relocations it can be trusted to apply to). So the
 * host starts ITSELF suspended, reserves the guest range in the child before
 * the child's loader runs, and resumes it. The child finds the reservation and
 * commits the image into it. */
static int launch_child(void) {
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    SetEnvironmentVariableA("NFSU2_RECOMP_CHILD", "1");
    STARTUPINFOA si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(self, GetCommandLineA(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "[NFSU2:BOOT] launcher: CreateProcess failed (%lu)\n", GetLastError());
        return 2;
    }
    void *r = VirtualAllocEx(pi.hProcess, (void *)(uintptr_t)NFS_GUEST_BASE, GUEST_SPAN, MEM_RESERVE, PAGE_NOACCESS);
    if (!r) {
        fprintf(stderr, "[NFSU2:BOOT] launcher: cannot reserve 0x%08X+0x%X in the child (%lu)\n",
                NFS_GUEST_BASE, GUEST_SPAN, GetLastError());
        TerminateProcess(pi.hProcess, 2);
        return 2;
    }
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 0;
    GetExitCodeProcess(pi.hProcess, &rc);
    fprintf(stderr, "[NFSU2:BOOT] launcher: game process exited with 0x%08lX\n", rc);
    return (int)rc;
}

int main(int argc, char **argv) {
    /* 1. The guest range must be reserved before the loader fills it. */
    if (!getenv("NFSU2_RECOMP_CHILD")) return launch_child();
    void *hold = guest_range_reserved() ? (void *)(uintptr_t)NFS_GUEST_BASE : NULL;

    char logdir[MAX_PATH] = "logs";
    const char *root_arg = getenv("NFSU2_ROOT");
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game-root") && i + 1 < argc) root_arg = argv[++i];
        else if (!strcmp(argv[i], "--log-dir") && i + 1 < argc) strncpy(logdir, argv[++i], MAX_PATH - 1);
        else if (!strcmp(argv[i], "--trace-native")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--fullscreen")) { extern int g_nfs_windowed; g_nfs_windowed = 0; }
        else if (!strcmp(argv[i], "--windowed")) { extern int g_nfs_windowed; g_nfs_windowed = 1; }
        else if (!strcmp(argv[i], "--trace-callbacks")) native32_trace_callbacks = 1;
        else {
            int used = recomp_trace_arg(argc, argv, i);
            if (used > 0) i += used - 1;
        }
    }
    /* Log dir is resolved against the host's starting directory, before chdir. */
    char logabs[MAX_PATH];
    CreateDirectoryA(logdir, NULL);
    GetFullPathNameA(logdir, MAX_PATH, logabs, NULL);
    nfs_log_init(logabs);
    NFS_LOG(BOOT, "NFSU2-Recompiled starting; %d lifted functions, %d overrides",
            recomp_dispatch_count, nfs_override_count());
    if (!hold) {
        NFS_LOG(BOOT, "could not reserve guest range 0x%08X+0x%X; occupants:", NFS_GUEST_BASE, GUEST_SPAN);
        for (uint32_t a = NFS_GUEST_BASE; a < NFS_GUEST_BASE + GUEST_SPAN;) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery((void *)(uintptr_t)a, &mbi, sizeof mbi)) break;
            char mod[MAX_PATH] = "";
            if (mbi.Type == MEM_IMAGE) GetModuleFileNameA((HMODULE)mbi.AllocationBase, mod, MAX_PATH);
            NFS_LOG(BOOT, "  %08X+%08X state=%lX type=%lX alloc=%p %s", a, (uint32_t)mbi.RegionSize,
                    mbi.State, mbi.Type, mbi.AllocationBase, mod);
            a = (uint32_t)(uintptr_t)mbi.BaseAddress + (uint32_t)mbi.RegionSize;
        }
        return 2;
    }

    /* 2. GAME_ROOT */
    if (root_arg) strncpy(g_nfs_game_root, root_arg, MAX_PATH - 1);
    else if (!find_game_root(g_nfs_game_root)) {
        NFS_LOG(BOOT, "SPEED2.EXE not found: pass --game-root or set NFSU2_ROOT");
        return 2;
    }
    snprintf(g_nfs_exe_path, MAX_PATH, "%s\\SPEED2.EXE", g_nfs_game_root);
    if (!file_exists(g_nfs_exe_path)) {
        NFS_LOG(BOOT, "no SPEED2.EXE in %s", g_nfs_game_root);
        return 2;
    }
    SetCurrentDirectoryA(g_nfs_game_root);
    NFS_LOG(BOOT, "GAME_ROOT = %s", g_nfs_game_root);
    nfs_widescreen_init(g_nfs_game_root);
    NFS_LOG(BOOT, "widescreen: %d HUD offsets from scripts/NFSUnderground2.WidescreenFix.dat", nfs_widescreen_hud_entries());

    /* 3. Machine, image, imports */
    native32_init();
    nfs_install_crash_handler();
    nfs_d3d9_trace_init();
    /* image_loader commits the image into the reservation the launcher made */
    uint32_t span = native32_map(g_nfs_exe_path, NFS_GUEST_BASE);
    if (!span) { NFS_LOG(BOOT, "mapping SPEED2.EXE at 0x%08X failed", NFS_GUEST_BASE); return 2; }
    NFS_LOG(BOOT, "mapped SPEED2.EXE at 0x%08X, span 0x%X", NFS_GUEST_BASE, span);
    int missing = native32_bind(NFS_GUEST_BASE, g_nfs_shims, g_nfs_nshims);
    NFS_LOG(IMPORT, "imports bound, %d unresolved", missing);

    /* CPUID: no 3DNow!/3DNow!+ (extended leaf EDX bits 31/30). The lifter does
     * not model 3DNow!, and CPU-dispatched code must take the SSE/x87 paths. */
    g_cpuid_edx_ext &= ~0xC0000000u;

    /* 4. Run */
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(uintptr_t)(NFS_GUEST_BASE +
        ((IMAGE_DOS_HEADER *)(uintptr_t)NFS_GUEST_BASE)->e_lfanew);
    uint32_t entry = NFS_GUEST_BASE + nt->OptionalHeader.AddressOfEntryPoint;
    g_native = getenv("NFSU2_NATIVE") && *getenv("NFSU2_NATIVE") == '1';
    { extern void nfs_native_range_init(void); nfs_native_range_init(); }
    if (g_native) {
        make_guest_code_executable();
        NFS_LOG(BOOT, "NFSU2_NATIVE=1: running the ORIGINAL x86 code natively (oracle mode)");
    }
    NFS_LOG(BOOT, "entering original entry point 0x%08X", entry);
    nfs_log_flush();
    /* The game runs on its own host thread: every guest call is a host C call,
     * so it needs a far deeper stack than the 1 MB main thread, and a big
     * main-thread stack would be placed by the loader right where the guest
     * image has to go. */
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    extern void nfs_profiler_start(HANDLE);
    HANDLE th = CreateThread(NULL, GAME_THREAD_STACK, game_thread, (void *)(uintptr_t)entry,
                             STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    nfs_profiler_start(th);
    WaitForSingleObject(th, INFINITE);
    DWORD rc = 0;
    GetExitCodeThread(th, &rc);
    NFS_LOG(BOOT, "entry point returned (eax=%08lX)", rc);
    nfs_log_flush();
    return (int)rc;
}

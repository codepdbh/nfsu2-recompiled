/*
 * d3d9_trace.c - which part of Direct3D 9 NFSU2 uses, measured at runtime.
 *
 * The game calls the real d3d9.dll through COM vtables. native32's post-call
 * hook sees every bridged call; when Direct3DCreate9 returns an IDirect3D9 and
 * CreateDevice returns an IDirect3DDevice9, their vtable slots get names, and
 * from then on every method call is counted and its interesting arguments
 * (states, formats, FVFs, primitive types) are recorded.
 *
 *   D3D_TRACE=0        off
 *   D3D_TRACE=summary  counts + one line per 60 frames (default)
 *   D3D_TRACE=full     plus one log line per D3D call (slow)
 *
 * The report (logs/d3d9_usage_<stamp>.txt) is the specification for the
 * future Vulkan renderer, so it lists every value seen, not just counts.
 */
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native32.h"
#include "nfs_log.h"
#include "nfs_runtime.h"
#include "nfs_widescreen.h"

static const char *k_d3d9[17] = {
    "QueryInterface", "AddRef", "Release", "RegisterSoftwareDevice", "GetAdapterCount",
    "GetAdapterIdentifier", "GetAdapterModeCount", "EnumAdapterModes", "GetAdapterDisplayMode",
    "CheckDeviceType", "CheckDeviceFormat", "CheckDeviceMultiSampleType", "CheckDepthStencilMatch",
    "CheckDeviceFormatConversion", "GetDeviceCaps", "GetAdapterMonitor", "CreateDevice" };

#define NDEV 119
static const char *k_dev[NDEV] = {
    "QueryInterface", "AddRef", "Release", "TestCooperativeLevel", "GetAvailableTextureMem",
    "EvictManagedResources", "GetDirect3D", "GetDeviceCaps", "GetDisplayMode", "GetCreationParameters",
    "SetCursorProperties", "SetCursorPosition", "ShowCursor", "CreateAdditionalSwapChain", "GetSwapChain",
    "GetNumberOfSwapChains", "Reset", "Present", "GetBackBuffer", "GetRasterStatus", "SetDialogBoxMode",
    "SetGammaRamp", "GetGammaRamp", "CreateTexture", "CreateVolumeTexture", "CreateCubeTexture",
    "CreateVertexBuffer", "CreateIndexBuffer", "CreateRenderTarget", "CreateDepthStencilSurface",
    "UpdateSurface", "UpdateTexture", "GetRenderTargetData", "GetFrontBufferData", "StretchRect",
    "ColorFill", "CreateOffscreenPlainSurface", "SetRenderTarget", "GetRenderTarget",
    "SetDepthStencilSurface", "GetDepthStencilSurface", "BeginScene", "EndScene", "Clear", "SetTransform",
    "GetTransform", "MultiplyTransform", "SetViewport", "GetViewport", "SetMaterial", "GetMaterial",
    "SetLight", "GetLight", "LightEnable", "GetLightEnable", "SetClipPlane", "GetClipPlane",
    "SetRenderState", "GetRenderState", "CreateStateBlock", "BeginStateBlock", "EndStateBlock",
    "SetClipStatus", "GetClipStatus", "GetTexture", "SetTexture", "GetTextureStageState",
    "SetTextureStageState", "GetSamplerState", "SetSamplerState", "ValidateDevice", "SetPaletteEntries",
    "GetPaletteEntries", "SetCurrentTexturePalette", "GetCurrentTexturePalette", "SetScissorRect",
    "GetScissorRect", "SetSoftwareVertexProcessing", "GetSoftwareVertexProcessing", "SetNPatchMode",
    "GetNPatchMode", "DrawPrimitive", "DrawIndexedPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP",
    "ProcessVertices", "CreateVertexDeclaration", "SetVertexDeclaration", "GetVertexDeclaration", "SetFVF",
    "GetFVF", "CreateVertexShader", "SetVertexShader", "GetVertexShader", "SetVertexShaderConstantF",
    "GetVertexShaderConstantF", "SetVertexShaderConstantI", "GetVertexShaderConstantI",
    "SetVertexShaderConstantB", "GetVertexShaderConstantB", "SetStreamSource", "GetStreamSource",
    "SetStreamSourceFreq", "GetStreamSourceFreq", "SetIndices", "GetIndices", "CreatePixelShader",
    "SetPixelShader", "GetPixelShader", "SetPixelShaderConstantF", "GetPixelShaderConstantF",
    "SetPixelShaderConstantI", "GetPixelShaderConstantI", "SetPixelShaderConstantB",
    "GetPixelShaderConstantB", "DrawRectPatch", "DrawTriPatch", "DeletePatch", "CreateQuery" };

enum { D_PRESENT = 17, D_CREATETEX = 23, D_CREATECUBE = 25, D_CREATEVB = 26, D_CREATEIB = 27,
       D_CREATERT = 28, D_CREATEDS = 29, D_CLEAR = 43, D_SETRS = 57, D_SETTEX = 65, D_SETTSS = 67,
       D_SETSS = 69, D_DRAWP = 81, D_DRAWIP = 82, D_DRAWPUP = 83, D_DRAWIPUP = 84, D_CREATEDECL = 86,
       D_SETFVF = 89, D_CREATEVS = 91, D_CREATEPS = 106, D_RESET = 16 };

static int g_mode = 1;                 /* 0 off, 1 summary, 2 full */
static uint32_t g_d3d_vt[17], g_dev_vt[NDEV];
static uint64_t g_count[NDEV], g_frame_count[NDEV];
static uint32_t g_frames;
/* sets of values seen: small open tables */
typedef struct { uint32_t v[256]; uint32_t n[256]; int len; } vset;
static vset s_rs, s_tss, s_ss, s_texfmt, s_cubefmt, s_rtfmt, s_dsfmt, s_fvf, s_prim, s_vbusage, s_ibfmt;
static uint32_t s_tex_max_w, s_tex_max_h, s_vs_created, s_ps_created, s_decl_created;
static char g_pp[512];

static void vs_add(vset *s, uint32_t v) {
    for (int i = 0; i < s->len; i++) if (s->v[i] == v) { s->n[i]++; return; }
    if (s->len < 256) { s->v[s->len] = v; s->n[s->len++] = 1; }
}
static int dev_index(uint32_t fn) {
    for (int i = 0; i < NDEV; i++) if (g_dev_vt[i] == fn) return i;
    return -1;
}

static void dump_set(FILE *f, const char *title, vset *s, int hex) {
    fprintf(f, "%s (%d distinct):", title, s->len);
    for (int i = 0; i < s->len; i++) fprintf(f, hex ? " 0x%X(x%u)" : " %u(x%u)", s->v[i], s->n[i]);
    fputc('\n', f);
}

void nfs_d3d9_write_report(void) {
    if (!g_mode || !g_dev_vt[0]) return;
    char path[700];
    snprintf(path, sizeof path, "%s/d3d9_usage_%s.txt", nfs_log_dir(), nfs_log_stamp());
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "NFSU2 Direct3D 9 usage (measured), %u frames presented\n\n", g_frames);
    fprintf(f, "CreateDevice: %s\n\n", g_pp);
    fprintf(f, "Methods called (total):\n");
    for (int i = 0; i < NDEV; i++) if (g_count[i]) fprintf(f, "  %-28s %llu\n", k_dev[i], g_count[i]);
    fputc('\n', f);
    dump_set(f, "SetRenderState D3DRENDERSTATETYPE", &s_rs, 0);
    dump_set(f, "SetTextureStageState D3DTEXTURESTAGESTATETYPE", &s_tss, 0);
    dump_set(f, "SetSamplerState D3DSAMPLERSTATETYPE", &s_ss, 0);
    dump_set(f, "CreateTexture D3DFORMAT", &s_texfmt, 0);
    dump_set(f, "CreateCubeTexture D3DFORMAT", &s_cubefmt, 0);
    dump_set(f, "CreateRenderTarget D3DFORMAT", &s_rtfmt, 0);
    dump_set(f, "CreateDepthStencilSurface D3DFORMAT", &s_dsfmt, 0);
    dump_set(f, "CreateVertexBuffer usage", &s_vbusage, 1);
    dump_set(f, "CreateIndexBuffer D3DFORMAT", &s_ibfmt, 0);
    dump_set(f, "SetFVF", &s_fvf, 1);
    dump_set(f, "Draw* D3DPRIMITIVETYPE", &s_prim, 0);
    fprintf(f, "largest texture: %ux%u\nvertex shaders created: %u, pixel shaders created: %u, vertex declarations: %u\n",
            s_tex_max_w, s_tex_max_h, s_vs_created, s_ps_created, s_decl_created);
    fclose(f);
}

int nfs_dinput_post(uint32_t fn, const uint32_t *a, uint32_t ret);

/* Name any unnamed native target "module+0xOFF" once, so traces and crash
 * reports say which DLL a COM/GetProcAddress call went to. */
static void name_unknown(uint32_t fn) {
    if (native32_name(fn)) return;
    HMODULE m = NULL; char path[MAX_PATH] = "?", off[24];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)fn, &m)) GetModuleFileNameA(m, path, MAX_PATH);
    const char *b = strrchr(path, '\\'); b = b ? b + 1 : path;
    snprintf(off, sizeof off, "+0x%X", (unsigned)(fn - (uint32_t)(uintptr_t)m));
    native32_add_name(fn, b, off);
}

static void post(uint32_t fn, const uint32_t *a, uint32_t ret) {
    name_unknown(fn);
    if (nfs_dinput_post(fn, a, ret)) return;
    if (g_d3d_vt[0]) for (int k = 3; k < 17; k++)
        if (fn == g_d3d_vt[k] && (ret & 0x80000000u) && k != 16) {
            static uint32_t s_f3[17];
            if (s_f3[k]++ < 16) NFS_LOG(D3D9, "FAILED IDirect3D9::%s(%08X %08X %08X %08X %08X) -> %08X",
                                        k_d3d9[k], a[1], a[2], a[3], a[4], a[5], ret);
        }
    if (g_d3d_vt[16] && fn == g_d3d_vt[16]) {               /* IDirect3D9::CreateDevice */
        const uint32_t *pp = (const uint32_t *)(uintptr_t)a[5];
        snprintf(g_pp, sizeof g_pp,
                 "adapter %u type %u hwnd %08X behavior 0x%X | backbuffer %ux%u fmt %u count %u, "
                 "msaa %u/%u, swap %u, windowed %u, autodepth %u fmt %u, flags 0x%X, refresh %u, interval 0x%X -> hr %08X",
                 a[1], a[2], a[3], a[4], pp[0], pp[1], pp[2], pp[3], pp[4], pp[5], pp[6], pp[8], pp[9], pp[10],
                 pp[11], pp[12], pp[13], ret);
        NFS_LOG(D3D9, "CreateDevice: %s", g_pp);
        if (ret == 0) {
            uint32_t dev = *(uint32_t *)(uintptr_t)a[6];
            const uint32_t *vt = *(const uint32_t **)(uintptr_t)dev;
            for (int i = 0; i < NDEV; i++) { g_dev_vt[i] = vt[i]; native32_add_name(vt[i], "IDirect3DDevice9", k_dev[i]); }
            NFS_LOG(D3D9, "IDirect3DDevice9 %08X", dev);
        }
        return;
    }
    if (!g_dev_vt[0] || !g_mode) return;
    int i = dev_index(fn);
    if (i < 0) return;
    g_count[i]++; g_frame_count[i]++;
    /* every failing HRESULT, the first 16 per method (AddRef/Release return counts) */
    static uint32_t s_fail[NDEV];
    if (i > 2 && (ret & 0x80000000u) && s_fail[i]++ < 16)
        NFS_LOG(D3D9, "FAILED %s(%08X %08X %08X %08X %08X) -> %08X", k_dev[i], a[1], a[2], a[3], a[4], a[5], ret);
    switch (i) {
    case D_SETRS: vs_add(&s_rs, a[1]); break;
    case D_SETTSS: vs_add(&s_tss, a[2]); break;
    case D_SETSS: vs_add(&s_ss, a[2]); break;
    case D_CREATETEX: vs_add(&s_texfmt, a[5]);
        if (a[1] * a[2] > s_tex_max_w * s_tex_max_h) { s_tex_max_w = a[1]; s_tex_max_h = a[2]; } break;
    case D_CREATECUBE: vs_add(&s_cubefmt, a[4]); break;
    case D_CREATERT: vs_add(&s_rtfmt, a[3]); break;
    case D_CREATEDS: vs_add(&s_dsfmt, a[3]); break;
    case D_CREATEVB: vs_add(&s_vbusage, a[2]); break;
    case D_CREATEIB: vs_add(&s_ibfmt, a[3]); break;
    case D_SETFVF: vs_add(&s_fvf, a[1]); break;
    case D_DRAWP: case D_DRAWIP: case D_DRAWPUP: case D_DRAWIPUP: vs_add(&s_prim, a[1]); break;
    case D_CREATEVS: s_vs_created++; break;
    case D_CREATEPS: s_ps_created++; break;
    case D_CREATEDECL: s_decl_created++; break;
    case D_RESET: NFS_LOG(D3D9, "Reset -> %08X", ret); break;
    }
    /* Shader constants that are NaN/Inf: a CPU-side math error shows up here
     * first (a car lit by NaN renders black). First 32 occurrences. */
    if (i == 94 /* SetVertexShaderConstantF */ || i == 109 /* SetPixelShaderConstantF */) {
        static uint32_t s_bad;
        const uint32_t *v = (const uint32_t *)(uintptr_t)a[2];
        for (uint32_t k = 0; v && k < a[3] * 4 && s_bad < 32; k++)
            if ((v[k] & 0x7F800000u) == 0x7F800000u) {
                s_bad++;
                NFS_LOG(D3D9, "%s: c%u.%c = %08X (NaN/Inf) frame %u", k_dev[i], a[1] + k / 4, "xyzw"[k % 4], v[k], g_frames);
                break;
            }
    }
    if (g_mode == 2) NFS_LOG(D3D9, "%s(%08X %08X %08X %08X) -> %08X", k_dev[i], a[1], a[2], a[3], a[4], ret);
    if (i == D_PRESENT) {
        g_frames++;
        if (g_frames == 1 || g_frames % 60 == 0) {
            uint64_t draws = g_frame_count[D_DRAWP] + g_frame_count[D_DRAWIP] +
                             g_frame_count[D_DRAWPUP] + g_frame_count[D_DRAWIPUP], calls = 0;
            for (int k = 0; k < NDEV; k++) calls += g_frame_count[k];
            NFS_LOG(D3D9, "frame %u: Present -> %08X; last frame %llu calls, %llu draws, %llu SetTexture, %llu SetRenderState",
                    g_frames, ret, calls, draws, g_frame_count[D_SETTEX], g_frame_count[D_SETRS]);
            nfs_d3d9_write_report();
        }
        memset(g_frame_count, 0, sizeof g_frame_count);
    }
}

/* ---- windowed mode ----
 *
 * NFSU2 only creates fullscreen devices. With g_nfs_windowed set, the
 * presentation parameters of IDirect3D9::CreateDevice and
 * IDirect3DDevice9::Reset are rewritten before the real call (Windowed = 1,
 * no fullscreen refresh rate, back-buffer format from the desktop), and the
 * game's borderless window becomes a normal window whose client area is the
 * back buffer, centred on the desktop. The game itself is not told. */
int g_nfs_windowed = 1;

/* NFSU2_BACKGROUND=1 (testing): keep the game running while its window is
 * not active. The game pauses on WM_ACTIVATEAPP(FALSE); a host-side subclass
 * of its window turns that into TRUE before the guest WndProc sees it. */
static WNDPROC g_game_wndproc;
static LRESULT CALLBACK bg_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ACTIVATEAPP && !w) w = TRUE;
    if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE) w = WA_ACTIVE;
    return CallWindowProcA(g_game_wndproc, h, m, w, l);
}
static void maybe_background(HWND hwnd) {
    const char *e = getenv("NFSU2_BACKGROUND");
    if (!e || *e != '1' || g_game_wndproc || !hwnd) return;
    g_game_wndproc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)bg_wndproc);
    NFS_LOG(D3D9, "NFSU2_BACKGROUND: window %p keeps running when inactive (wndproc %p)", hwnd, g_game_wndproc);
}

static void make_windowed(uint32_t *pp, uint32_t hwnd_hint) {
    pp[8] = 1;            /* Windowed */
    pp[12] = 0;           /* FullScreen_RefreshRateInHz */
    pp[2] = 0;            /* BackBufferFormat: D3DFMT_UNKNOWN = desktop format */
    HWND hwnd = (HWND)(uintptr_t)(pp[7] ? pp[7] : hwnd_hint);
    if (!hwnd) return;
    maybe_background(hwnd);
    RECT r = { 0, 0, (LONG)(pp[0] ? pp[0] : 640), (LONG)(pp[1] ? pp[1] : 480) };
    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    SetWindowLongA(hwnd, GWL_STYLE, style | WS_VISIBLE);
    SetWindowLongA(hwnd, GWL_EXSTYLE, 0);
    SetWindowPos(hwnd, HWND_NOTOPMOST, x < 0 ? 0 : x, y < 0 ? 0 : y, w, h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
    NFS_LOG(D3D9, "windowed: %ux%u client, window %dx%d at %d,%d", pp[0], pp[1], w, h, x, y);
}

/* ---- vtable hooks (work for lifted AND native guest code) ----
 * The windowed-mode rewrite has to happen whoever calls CreateDevice/Reset:
 * lifted code goes through the native32 bridge, but original x86 code run
 * natively (NFSU2_NATIVE=1, the oracle) calls d3d9.dll directly. Patching the
 * slots of the (process-local) vtables catches both. */
typedef HRESULT (__stdcall *create_device_t)(void *, UINT, UINT, HWND, DWORD, void *, void **);
typedef HRESULT (__stdcall *reset_t)(void *, void *);
static create_device_t g_orig_create;
static reset_t g_orig_reset;
static void patch_slot(uint32_t *slot, void *fn, void **orig) {
    DWORD old;
    if (*slot == (uint32_t)(uintptr_t)fn) return;
    VirtualProtect(slot, 4, PAGE_EXECUTE_READWRITE, &old);
    *orig = (void *)(uintptr_t)*slot;
    *slot = (uint32_t)(uintptr_t)fn;
    VirtualProtect(slot, 4, old, &old);
}
static HRESULT __stdcall hk_reset(void *dev, void *pp) {
    if (pp) nfs_widescreen_set_resolution(((uint32_t *)pp)[0], ((uint32_t *)pp)[1]);
    if (g_nfs_windowed && pp) make_windowed((uint32_t *)pp, 0);
    return g_orig_reset(dev, pp);
}
static HRESULT __stdcall hk_create(void *d3d, UINT ad, UINT type, HWND focus, DWORD flags, void *pp, void **out) {
    if (pp) nfs_widescreen_set_resolution(((uint32_t *)pp)[0], ((uint32_t *)pp)[1]);
    if (g_nfs_windowed && pp) make_windowed((uint32_t *)pp, (uint32_t)(uintptr_t)focus);
    HRESULT hr = g_orig_create(d3d, ad, type, focus, flags, pp, out);
    if (hr == 0 && out && *out)
        patch_slot(&(*(uint32_t **)*out)[D_RESET], (void *)hk_reset, (void **)&g_orig_reset);
    return hr;
}
/* Called by the Direct3DCreate9 shim with the new IDirect3D9. */
void nfs_d3d9_hook_object(uint32_t d3d9) {
    uint32_t *vt = *(uint32_t **)(uintptr_t)d3d9;
    patch_slot(&vt[16], (void *)hk_create, (void **)&g_orig_create);
    for (int i = 0; i < 17; i++) { g_d3d_vt[i] = vt[i]; native32_add_name(vt[i], "IDirect3D9", k_d3d9[i]); }
    NFS_LOG(D3D9, "IDirect3D9 %08X: CreateDevice hooked (windowed=%d)", d3d9, g_nfs_windowed);
}

static void pre(uint32_t fn, uint32_t *a) {
    (void)fn; (void)a;   /* nothing yet: windowed mode lives in the vtable hooks */
}

void nfs_d3d9_trace_init(void) {
    const char *m = getenv("D3D_TRACE");
    if (m) g_mode = !strcmp(m, "full") ? 2 : (!strcmp(m, "0") || !strcmp(m, "off")) ? 0 : 1;
    native32_post_hook = post;
    native32_pre_hook = pre;
    NFS_LOG(D3D9, "display: %s", g_nfs_windowed ? "windowed (use --fullscreen for fullscreen)" : "fullscreen");
    if (!g_mode) return;
    native32_post_hook = post;
    NFS_LOG(D3D9, "D3D9 trace: %s", g_mode == 2 ? "full" : "summary");
}

uint32_t nfs_d3d9_frames(void) { return g_frames; }

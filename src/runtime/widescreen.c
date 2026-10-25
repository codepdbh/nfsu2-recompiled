/*
 * widescreen.c - HUD, FOV, mirror, radar and FMV geometry for wide displays.
 *
 * A port of the HUD/FOV parts of ThirteenAG's NFSUnderground2.WidescreenFix
 * (WidescreenFixesPack, MIT): Frontend.ixx (FixHUD, cutscene borders, FMV
 * mode) and Rendering.ixx (FixFOV). That mod patches x86 bytes in memory; the
 * game here is lifted C, so each patch is re-expressed as one of:
 *   - a write to a float the code reads from .rdata/.data (still guest memory);
 *   - a host hook (Lifter hook_sites -> RECOMP_HOOK -> recomp_hook) right after
 *     an instruction that stored an immediate, rewriting what it stored, or
 *     where the mod's inline hook adjusted registers/memory.
 * The hook VAs are listed in config/hooks.txt and were located by the mod's
 * own byte patterns in the pinned SPEED2.EXE (scripts/widescreen_sites.py).
 *
 * HUD element offsets come from the mod's NFSUnderground2.WidescreenFix.dat,
 * read from <game>/scripts/ when present (the player supplies the mod).
 */
#include "recomp_types.h"
#include "nfs_widescreen.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct { float pos_x, pos_y, offset_x, offset_y; } hud_entry;
static hud_entry s_hud[256];
static unsigned s_hud_count;
static int s_enabled;
static float s_aspect = 4.0f / 3.0f;
static float s_hud_pos_x = 320.0f;      /* the mod's fHudPosX */
static float s_hud_offset = 0.0f;       /* fWidescreenHudOffset */
static float s_width = 640.0f, s_height = 480.0f;
static const int s_disable_cutscene_borders = 1;
/* Extra vertical move of the minimap (mobile layout: top-left instead of
 * bottom-left, clear of the thumbs). 0 keeps the mod's position. */
static float s_minimap_dy = 0.0f;
/* Game frame time for FPSLimit (0 = the game's own 1/60 s). */
static float s_frame_time = 0.0f;
/* FOV multipliers chosen per view at 0x5C7F64 and applied at its three
 * reads of the 1.0/0.5/1.0 constants (the mod repoints those operands). */
static double s_fov_scale[3] = {1.0, 1.0, 1.0};

static float rdf(uint32_t va) { uint32_t v = MEM32(va); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t va, float f) { uint32_t v; memcpy(&v, &f, 4); MEM32(va) = v; }

static void load_dat(const char* path) {
    FILE* f = fopen(path, "r");
    char line[256];
    if (!f) return;
    while (fgets(line, sizeof line, f) && s_hud_count < sizeof s_hud / sizeof s_hud[0]) {
        hud_entry e;
        if (!line[0] || line[0] == '#') continue;
        if (sscanf(line, "%*s %f %f %f %f", &e.pos_x, &e.pos_y, &e.offset_x, &e.offset_y) == 4)
            s_hud[s_hud_count++] = e;
    }
    fclose(f);
}

/* MiniMap_Pos_1..4 and MiniMap_Filter_Pos in the mod's .dat (names are not
 * loaded; the anchors are what the game draws them at). */
static int is_minimap(const hud_entry* e) {
    int x = (int)e->pos_x, y = (int)e->pos_y;
    return (x == -223 && (y == 70 || y == 71 || y == 142 || y == 143)) || (x == -143 && y == 196);
}

/* The mod's WidescreenHud(): move a known HUD element by its .dat offset. */
static void widescreen_hud(float* x, float* y) {
    const float base_offset = (480.0f * (16.0f / 9.0f) - 640.0f) / 2.0f;
    int ix = (int)floorf(*x), iy = (int)floorf(*y);
    for (unsigned i = 0; i < s_hud_count; i++) {
        const hud_entry* e = &s_hud[i];
        if ((int)e->pos_x != ix || (int)e->pos_y != iy) continue;
        if (e->offset_x >= 0.0f) *x += s_hud_offset + (e->offset_x - base_offset);
        else *x -= s_hud_offset - (e->offset_x + base_offset);
        *y += e->offset_y;
        if (is_minimap(e)) *y += s_minimap_dy;
        return;
    }
}

static void adjust_mem_pair(uint32_t vx, uint32_t vy) {
    float x = rdf(vx), y = rdf(vy);
    widescreen_hud(&x, &y);
    wrf(vx, x); wrf(vy, y);
}

/* Hook sites, in config/hooks*.txt order. The lifted code comes from one
 * SPEED2.EXE build, chosen at lift and compile time with NFSU2_EXE_BUILD:
 * the documented build (default, README "Tested Game Build"), f4bb0d99
 * (R36S port, PR #683) or 90d73842 (this fork's bundled speed2.exe, see
 * config/hooks-90d73842.txt) -- each at its own addresses. */
enum { HUD_A0, HUD_74A, HUD_84, HUD_94, HUD_74B, HUD_1A0, HUD_GLOBAL, MIRROR_R1, MIRROR_L1, MIRROR_L2, MIRROR_R2, BORDER_38, BORDER_48, BORDER_2C, BORDER_3C, RADAR_W30, RADAR_W40, RADAR_H44, RADAR_H54, HUD_HOOK, BLIPS_HOOK, HUD_HOOK2, STOP_SIGN, FMV_QUAD, FOV_SCALE, FOV_CONST1, FOV_CONST2, FOV_CONST3, FRAME_TIME, SITE_COUNT };
static const uint32_t k_sites[SITE_COUNT] = {
#if defined(NFSU2_EXE_F4BB0D99)
    0x0051B05Fu, 0x0053655Cu, 0x0053672Du, 0x0053695Du, 0x00536CA5u, 0x0050B099u, 0x0048B5D4u, 0x005CBDCDu, 0x005CBDDDu, 0x005CBDEDu, 0x005CBDFDu, 0x005CBBC9u, 0x005CBBD9u, 0x005CBB5Du, 0x005CBB75u, 0x005C6F3Eu, 0x005C6F4Eu, 0x005C6F56u, 0x005C6F66u, 0x0051AE28u, 0x0051D2E5u, 0x004C6589u, 0x00504D5Fu, 0x005366C2u, 0x005C7C34u, 0x005C7CBEu, 0x005C7CD0u, 0x005C7CF5u, 0x005D2962u,
#elif defined(NFSU2_EXE_90D73842)
    0x0051B3CFu, 0x005368DCu, 0x00536AADu, 0x00536CDDu, 0x00537025u, 0x0050B4F9u, 0x0048B644u, 0x005CC10Du, 0x005CC11Du, 0x005CC12Du, 0x005CC13Du, 0x005CBF09u, 0x005CBF19u, 0x005CBE9Du, 0x005CBEB5u, 0x005C727Eu, 0x005C728Eu, 0x005C7296u, 0x005C72A6u, 0x0051B198u, 0x0051D665u, 0x004C6759u, 0x0050516Fu, 0x00536A42u, 0x005C7F74u, 0x005C7FFEu, 0x005C8010u, 0x005C8035u, 0x005D2C22u,
#else
    0x0051B3CFu, 0x005368CCu, 0x00536A9Du, 0x00536CCDu, 0x00537015u, 0x0050B4F9u, 0x0048B644u, 0x005CC0FDu, 0x005CC10Du, 0x005CC11Du, 0x005CC12Du, 0x005CBEF9u, 0x005CBF09u, 0x005CBE8Du, 0x005CBEA5u, 0x005C726Eu, 0x005C727Eu, 0x005C7286u, 0x005C7296u, 0x0051B198u, 0x0051D655u, 0x004C6759u, 0x0050516Fu, 0x00536A32u, 0x005C7F64u, 0x005C7FEEu, 0x005C8000u, 0x005C8025u, 0x005D2C12u,
#endif
};
#if defined(NFSU2_EXE_F4BB0D99)
#define HUD_SCALE_X 0x0079AC0Cu
#else
#define HUD_SCALE_X 0x0079AC10u
#endif
void nfs_widescreen_init(const char* game_root) {
    char path[1024];
    snprintf(path, sizeof path, "%s/scripts/NFSUnderground2.WidescreenFix.dat", game_root);
    load_dat(path);
    /* Ports may ship the mod's table themselves (MIT): NFSU2_WIDESCREEN_DAT=<path>. */
    if (!s_hud_count && getenv("NFSU2_WIDESCREEN_DAT")) load_dat(getenv("NFSU2_WIDESCREEN_DAT"));
    s_enabled = 1;
}

void nfs_widescreen_set_resolution(unsigned width, unsigned height) {
    float hud_scale_x;
    if (!s_enabled || !width || !height) return;
    s_width = (float)width; s_height = (float)height;
    s_aspect = s_width / s_height;
    hud_scale_x = (1.0f / s_width * (s_height / 480.0f)) * 2.0f;
    s_hud_pos_x = 640.0f / (640.0f * hud_scale_x);
    /* HudAspectRatioConstraint = Auto: -CalculateWidescreenOffset(W, H, 640, 480). */
    s_hud_offset = -((640.0f / 2.0f) - (480.0f / 2.0f) * s_aspect);
    wrf(HUD_SCALE_X, hud_scale_x);              /* fld [fHudScaleX] */
    wrf(0x00797D50u, s_hud_pos_x);              /* fsub [fHudPosX] */
    wrf(0x00797D58u, s_hud_pos_x * 2.0f);       /* fmul [fHudPosX_x2] */
    /* Dyno result graph origin (float[2]). */
    wrf(0x007FA3C0u, 79.0f + ((480.0f * s_aspect) - 640.0f) / 2.0f);
    wrf(0x007FA3C4u, 60.0f);
}

int nfs_widescreen_hud_entries(void) { return (int)s_hud_count; }

void nfs_widescreen_set_minimap_offset(float dy) { s_minimap_dy = dy; }

/* FPSLimit (the mod's Misc.ixx): the game paces its main loop with a fixed
 * frame time of 1/60 s. SetFrameRate (0x576CB0) stores 1/fps in 0x865558 and
 * the loop at 0x57EB0A waits for it; the video-mode, smokeable and step
 * constants carry the same 1/60. They are data, so the lifted code reads
 * whatever is written here. */

void nfs_widescreen_set_frame_rate(unsigned fps) {
    s_frame_time = fps >= 30 ? 1.0f / (float)fps : 0.0f;
}

void nfs_widescreen_apply_frame_rate(void) {
    if (s_frame_time <= 0.0f) return;
    if (rdf(0x00865558u) != s_frame_time) {
        wrf(0x00865558u, s_frame_time);   /* main loop pacing (SetFrameRate) */
        wrf(0x007FB710u, s_frame_time);
        wrf(0x007875BCu, s_frame_time);   /* video mode frame time */
        wrf(0x007FC858u, s_frame_time);   /* smokeable frame time */
    }
}

static void set_esp_float(uint32_t offset, float value) { wrf(g_esp + offset, value); }

void recomp_hook(uint32_t va) {
    int site = 0;
    while (site < SITE_COUNT && k_sites[site] != va) ++site;
    if (!s_enabled && site != FRAME_TIME) return;
    { static int trace = -1; static uint32_t seen[SITE_COUNT]; static int count;
      if (trace < 0) trace = getenv("NFSU2_WS_TRACE") != NULL;   /* first hit of each site */
      if (trace) { int k; for (k = 0; k < count && seen[k] != va; ++k) ;
          if (k == count && count < SITE_COUNT) { seen[count++] = va; printf("[WS] hook %08x esp=%08x\n", va, g_esp); fflush(stdout); } } }
    switch (site) {
    /* HUD anchors: `mov [esp+N], 320.0` -> fHudPosX (pattern offsets 4/7). */
    case HUD_A0: set_esp_float(0xA0, s_hud_pos_x); break;
    case HUD_74A: set_esp_float(0x74, s_hud_pos_x); break;
    case HUD_84: set_esp_float(0x84, s_hud_pos_x); break;
    case HUD_94: set_esp_float(0x94, s_hud_pos_x); break;
    case HUD_74B: set_esp_float(0x74, s_hud_pos_x); break;
    case HUD_1A0: set_esp_float(0x1A0, s_hud_pos_x); break;
    case HUD_GLOBAL: wrf(0x0082D2D8u, s_hud_pos_x); break;
    /* Rear-view mirror quad: 440/200 shifted with the HUD centre. */
    case MIRROR_R1: set_esp_float(0x30, (s_hud_pos_x - 320.0f) + 440.0f); break;
    case MIRROR_L1: set_esp_float(0x40, (s_hud_pos_x - 320.0f) + 200.0f); break;
    case MIRROR_L2: set_esp_float(0x50, (s_hud_pos_x - 320.0f) + 200.0f); break;
    case MIRROR_R2: set_esp_float(0x60, (s_hud_pos_x - 320.0f) + 440.0f); break;
    /* Cutscene letterbox quads (DisableCutsceneBorders = 1). */
    case BORDER_38: set_esp_float(0x38, s_disable_cutscene_borders ? 0.0f : s_width); break;
    case BORDER_48: set_esp_float(0x48, s_disable_cutscene_borders ? 0.0f : s_width); break;
    case BORDER_2C: set_esp_float(0x2C, s_disable_cutscene_borders ? 0.0f : s_width); break;
    case BORDER_3C: set_esp_float(0x3C, s_disable_cutscene_borders ? 0.0f : s_width); break;
    /* Radar mask covers the whole render target. */
    case RADAR_W30: set_esp_float(0x30, s_width * 2.0f); break;
    case RADAR_W40: set_esp_float(0x40, s_width * 2.0f); break;
    case RADAR_H44: set_esp_float(0x44, s_height * 2.0f); break;
    case RADAR_H54: set_esp_float(0x54, s_height * 2.0f); break;
    /* HudHook: the element position just stored from ecx/edx. */
    case HUD_HOOK: adjust_mem_pair(g_esp + 0x60, g_esp + 0x64); break;
    /* BlipsHook: radar blips, before `mov [edx], ecx`. */
    case BLIPS_HOOK: adjust_mem_pair(g_ebx + 0x1C, g_ebx + 0x20); g_ecx = MEM32(g_ebx + 0x1C); break;
    /* HudHook2: ecx = adjusted X of [esi+1C]/[esi+20]. */
    case HUD_HOOK2: {
        float x, y; uint32_t v = g_ecx; memcpy(&x, &v, 4); y = rdf(g_esi + 0x20);
        widescreen_hud(&x, &y); memcpy(&v, &x, 4); g_ecx = v;
        break;
    }
    /* StopSignHook: [esi] was ST0 + [edx]; make it ST0 + adjusted [edx]. */
    case STOP_SIGN: {
        float x = rdf(g_edx), y = rdf(g_edx + 4), original = x;
        widescreen_hud(&x, &y);
        wrf(g_esi, rdf(g_esi) + (x - original));
        break;
    }
    /* FMVWidescreenMode = 1 (cropped 16:9): the four pushed quad extents. */
    case FMV_QUAD: {
        const float v = 0.5f / ((4.0f / 3.0f) / (16.0f / 9.0f));
        set_esp_float(0x0, -v); set_esp_float(0x4, -v); set_esp_float(0x8, v); set_esp_float(0xC, v);
        break;
    }
    /* FixFOV: ST0 was viewport height/width; the mod loads a per-view scale
     * and picks the constants of the three later reads by view id (ecx). */
    case FOV_SCALE: {
        uint32_t view = g_ecx;
        double hor = 1.0 / (s_aspect / (4.0 / 3.0));
        if (view == 1 || view == 4) { s_fov_scale[0] = hor; s_fov_scale[1] = 0.43511; s_fov_scale[2] = 1.22; }
        else { s_fov_scale[0] = 1.0; s_fov_scale[1] = 0.5; s_fov_scale[2] = 1.0; }
        g_st[0] = view == 3 ? 0.45 : 1.0;   /* rear-view mirror */
        break;
    }
    /* `fdivr [1.0]`, `fmul [0.5]`, `fdivr [1.0]`: rescale by new/original. */
    case FOV_CONST1: g_st[0] *= s_fov_scale[0] / rdf(0x00784250u); break;
    case FOV_CONST2: g_st[0] *= s_fov_scale[1] / rdf(0x00784260u); break;
    case FOV_CONST3: g_st[0] *= s_fov_scale[2] / rdf(0x00784250u); break;
    /* FPSLimit: eDisplayFrame pushes 1/60 s as the particle step. */
    case FRAME_TIME: if (s_frame_time > 0.0f) set_esp_float(0x0, s_frame_time); break;
    default: break;
    }
}

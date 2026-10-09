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

void nfs_widescreen_init(const char* game_root) {
    char path[1024];
    snprintf(path, sizeof path, "%s/scripts/NFSUnderground2.WidescreenFix.dat", game_root);
    load_dat(path);
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
    wrf(0x0079AC10u, hud_scale_x);              /* fld [fHudScaleX] */
    wrf(0x00797D50u, s_hud_pos_x);              /* fsub [fHudPosX] */
    wrf(0x00797D58u, s_hud_pos_x * 2.0f);       /* fmul [fHudPosX_x2] */
    /* Dyno result graph origin (float[2]). */
    wrf(0x007FA3C0u, 79.0f + ((480.0f * s_aspect) - 640.0f) / 2.0f);
    wrf(0x007FA3C4u, 60.0f);
}

int nfs_widescreen_hud_entries(void) { return (int)s_hud_count; }

void nfs_widescreen_set_minimap_offset(float dy) { s_minimap_dy = dy; }

static void set_esp_float(uint32_t offset, float value) { wrf(g_esp + offset, value); }

void recomp_hook(uint32_t va) {
    if (!s_enabled) return;
    switch (va) {
    /* HUD anchors: `mov [esp+N], 320.0` -> fHudPosX (pattern offsets 4/7). */
    case 0x0051B3CFu: set_esp_float(0xA0, s_hud_pos_x); break;
    case 0x005368CCu: set_esp_float(0x74, s_hud_pos_x); break;
    case 0x00536A9Du: set_esp_float(0x84, s_hud_pos_x); break;
    case 0x00536CCDu: set_esp_float(0x94, s_hud_pos_x); break;
    case 0x00537015u: set_esp_float(0x74, s_hud_pos_x); break;
    case 0x0050B4F9u: set_esp_float(0x1A0, s_hud_pos_x); break;
    case 0x0048B644u: wrf(0x0082D2D8u, s_hud_pos_x); break;
    /* Rear-view mirror quad: 440/200 shifted with the HUD centre. */
    case 0x005CC0FDu: set_esp_float(0x30, (s_hud_pos_x - 320.0f) + 440.0f); break;
    case 0x005CC10Du: set_esp_float(0x40, (s_hud_pos_x - 320.0f) + 200.0f); break;
    case 0x005CC11Du: set_esp_float(0x50, (s_hud_pos_x - 320.0f) + 200.0f); break;
    case 0x005CC12Du: set_esp_float(0x60, (s_hud_pos_x - 320.0f) + 440.0f); break;
    /* Cutscene letterbox quads (DisableCutsceneBorders = 1). */
    case 0x005CBEF9u: set_esp_float(0x38, s_disable_cutscene_borders ? 0.0f : s_width); break;
    case 0x005CBF09u: set_esp_float(0x48, s_disable_cutscene_borders ? 0.0f : s_width); break;
    case 0x005CBE8Du: set_esp_float(0x2C, s_disable_cutscene_borders ? 0.0f : s_width); break;
    case 0x005CBEA5u: set_esp_float(0x3C, s_disable_cutscene_borders ? 0.0f : s_width); break;
    /* Radar mask covers the whole render target. */
    case 0x005C726Eu: set_esp_float(0x30, s_width * 2.0f); break;
    case 0x005C727Eu: set_esp_float(0x40, s_width * 2.0f); break;
    case 0x005C7286u: set_esp_float(0x44, s_height * 2.0f); break;
    case 0x005C7296u: set_esp_float(0x54, s_height * 2.0f); break;
    /* HudHook: the element position just stored from ecx/edx. */
    case 0x0051B198u: adjust_mem_pair(g_esp + 0x60, g_esp + 0x64); break;
    /* BlipsHook: radar blips, before `mov [edx], ecx`. */
    case 0x0051D655u: adjust_mem_pair(g_ebx + 0x1C, g_ebx + 0x20); g_ecx = MEM32(g_ebx + 0x1C); break;
    /* HudHook2: ecx = adjusted X of [esi+1C]/[esi+20]. */
    case 0x004C6759u: {
        float x, y; uint32_t v = g_ecx; memcpy(&x, &v, 4); y = rdf(g_esi + 0x20);
        widescreen_hud(&x, &y); memcpy(&v, &x, 4); g_ecx = v;
        break;
    }
    /* StopSignHook: [esi] was ST0 + [edx]; make it ST0 + adjusted [edx]. */
    case 0x0050516Fu: {
        float x = rdf(g_edx), y = rdf(g_edx + 4), original = x;
        widescreen_hud(&x, &y);
        wrf(g_esi, rdf(g_esi) + (x - original));
        break;
    }
    /* FMVWidescreenMode = 1 (cropped 16:9): the four pushed quad extents. */
    case 0x00536A32u: {
        const float v = 0.5f / ((4.0f / 3.0f) / (16.0f / 9.0f));
        set_esp_float(0x0, -v); set_esp_float(0x4, -v); set_esp_float(0x8, v); set_esp_float(0xC, v);
        break;
    }
    /* FixFOV: ST0 was viewport height/width; the mod loads a per-view scale
     * and picks the constants of the three later reads by view id (ecx). */
    case 0x005C7F64u: {
        uint32_t view = g_ecx;
        double hor = 1.0 / (s_aspect / (4.0 / 3.0));
        if (view == 1 || view == 4) { s_fov_scale[0] = hor; s_fov_scale[1] = 0.43511; s_fov_scale[2] = 1.22; }
        else { s_fov_scale[0] = 1.0; s_fov_scale[1] = 0.5; s_fov_scale[2] = 1.0; }
        g_st[0] = view == 3 ? 0.45 : 1.0;   /* rear-view mirror */
        break;
    }
    /* `fdivr [1.0]`, `fmul [0.5]`, `fdivr [1.0]`: rescale by new/original. */
    case 0x005C7FEEu: g_st[0] *= s_fov_scale[0] / rdf(0x00784250u); break;
    case 0x005C8000u: g_st[0] *= s_fov_scale[1] / rdf(0x00784260u); break;
    case 0x005C8025u: g_st[0] *= s_fov_scale[2] / rdf(0x00784250u); break;
    default: break;
    }
}

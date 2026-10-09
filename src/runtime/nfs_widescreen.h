/* nfs_widescreen.h - wide-display HUD/FOV geometry (see widescreen.c). */
#ifndef NFS_WIDESCREEN_H
#define NFS_WIDESCREEN_H
#ifdef __cplusplus
extern "C" {
#endif

/* Enable the fix; reads <game_root>/scripts/NFSUnderground2.WidescreenFix.dat. */
void nfs_widescreen_init(const char* game_root);
/* The game's render size; call before its HUD draws and on every change. */
void nfs_widescreen_set_resolution(unsigned width, unsigned height);
/* HUD offset entries loaded from the .dat (0: elements keep 4:3 anchors). */
int nfs_widescreen_hud_entries(void);
/* Vertical offset (480-line HUD units, negative = up) added to the minimap. */
void nfs_widescreen_set_minimap_offset(float dy);
/* Game frame pacing (the game caps itself at 60): fps >= 30 replaces its 1/60 s
 * frame time; 0 keeps the original. apply() re-asserts it once per frame,
 * since the game re-sets 1/60 on mode changes. */
void nfs_widescreen_set_frame_rate(unsigned fps);
void nfs_widescreen_apply_frame_rate(void);

#ifdef __cplusplus
}
#endif
#endif

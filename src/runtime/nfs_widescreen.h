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

#ifdef __cplusplus
}
#endif
#endif

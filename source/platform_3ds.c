/* Echte 3DS-versie (libctru). */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"

u32 __stacksize__ = 2 * 1024 * 1024; /* grote stack voor JavaScript */

#define SAVEDIR "sdmc:/3ds/crownfall"
static int romfs_ok = 0;

void plat_init(void) {
  gfxInitDefault();
  gfxSetDoubleBuffering(GFX_TOP, false);
  gfxSetDoubleBuffering(GFX_BOTTOM, false);
  gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
  gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
  osSetSpeedupEnable(true); /* New 3DS: snelle modus */
  romfs_ok = (romfsInit() == 0);
  mkdir("sdmc:/3ds", 0777); mkdir(SAVEDIR, 0777);
}
void plat_shutdown(void) { if (romfs_ok) romfsExit(); gfxExit(); }
extern int g_quit_flag;
int plat_running(void) { return aptMainLoop() && !g_quit_flag; }
void plat_quit(void) { g_quit_flag = 1; }
int g_quit_flag = 0;

void plat_poll(Input *in) {
  hidScanInput();
  u32 d = hidKeysDown(), h = hidKeysHeld(), u = hidKeysUp(); touchPosition t; hidTouchRead(&t);
  in->down = d & 0xFFF; in->held = h & 0xFFF; in->up = u & 0xFFF;
  in->touching = (h & KEY_TOUCH) ? 1 : 0; in->tx = t.px; in->ty = t.py;
}
static void blit(gfxScreen_t sc, Surf *s) {
  u16 fw, fh; u8 *fb = gfxGetFramebuffer(sc, GFX_LEFT, &fw, &fh); /* fw=240 (hoogte), fh=breedte */
  int W = s->w, H = s->h; if (W > fh) W = fh; if (H > fw) H = fw;
  for (int x = 0; x < W; x++) {
    u8 *col = fb + x * fw * 3;
    for (int y = 0; y < H; y++) {
      uint32_t p = s->px[y * s->w + x]; u8 *o = col + (fw - 1 - y) * 3;
      o[0] = (p >> 16) & 255; o[1] = (p >> 8) & 255; o[2] = p & 255;
    }
  }
}
void plat_present(Surf *top, Surf *bot, int tc, int bc) {
  if (tc) blit(GFX_TOP, top);
  if (bc) blit(GFX_BOTTOM, bot);
  gfxFlushBuffers(); gfxSwapBuffers();
}
void plat_wait_frame(void) { gspWaitForVBlank(); }
double plat_now_ms(void) { return (double)osGetTime(); }

static uint8_t *readfile(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *d = (uint8_t *)malloc(n + 1); if (!d) { fclose(f); return 0; } size_t r = fread(d, 1, n, f); fclose(f); d[r] = 0; if (len) *len = r; return d;
}
uint8_t *plat_read_asset(const char *name, size_t *len) { char p[300]; snprintf(p, sizeof p, "romfs:/%s", name); return readfile(p, len); }
uint8_t *plat_read_save(const char *name, size_t *len) { char p[300]; snprintf(p, sizeof p, SAVEDIR "/%s", name); return readfile(p, len); }
int plat_write_save(const char *name, const void *data, size_t len) {
  char p[300], q[310]; snprintf(p, sizeof p, SAVEDIR "/%s", name); snprintf(q, sizeof q, "%s.tmp", p);
  FILE *f = fopen(q, "wb"); if (!f) return 0; size_t w = fwrite(data, 1, len, f); fclose(f); if (w != len) return 0;
  remove(p); return rename(q, p) == 0;
}
void plat_remove_save(const char *name) { char p[300]; snprintf(p, sizeof p, SAVEDIR "/%s", name); remove(p); }
void plat_log(const char *m) {
  FILE *f = fopen(SAVEDIR "/log.txt", "a"); if (f) { fprintf(f, "%s\n", m); fclose(f); }
}
void plat_fatal(const char *m) {
  plat_log(m);
  consoleInit(GFX_TOP, NULL); printf("\x1b[1;1HCrownfall: fout\n\n%s\n\nDruk START om af te sluiten.\n", m);
  while (aptMainLoop()) { hidScanInput(); if (hidKeysDown() & KEY_START) break; gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank(); }
}
int main(void) { plat_init(); int r = game_run(); plat_shutdown(); return r; }

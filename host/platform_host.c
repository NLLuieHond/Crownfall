/* Testversie voor de computer: draait de game zonder scherm, met een script voor aanrakingen en toetsen.
   Schermafbeeldingen worden als PPM-bestanden weggeschreven. Hiermee test ik alles voordat het naar de 3DS gaat. */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct { int frame; char cmd[16]; char arg[64]; int x, y; } Ev;
static Ev *evs; static int nev, evi; static int frame, maxframes = 600, quitting;
static int touching, tx, ty; static uint32_t heldkeys;
static const char *romfs = "romfs", *savedir = "save", *outdir = "out";
static Surf *ptop, *pbot; static char pendingShots[8][64]; static int npend;

void plat_init(void) {
  if (getenv("CROWN_ROMFS")) romfs = getenv("CROWN_ROMFS");
  if (getenv("CROWN_SAVE")) savedir = getenv("CROWN_SAVE");
  if (getenv("CROWN_OUT")) outdir = getenv("CROWN_OUT");
  if (getenv("CROWN_FRAMES")) maxframes = atoi(getenv("CROWN_FRAMES"));
  mkdir(savedir, 0777); mkdir(outdir, 0777);
  const char *sp = getenv("CROWN_SCRIPT"); if (!sp) return;
  FILE *f = fopen(sp, "r"); if (!f) return; char line[256];
  while (fgets(line, sizeof line, f)) {
    Ev e; memset(&e, 0, sizeof e); char a1[64] = "", a2[64] = "", a3[64] = "";
    int n = sscanf(line, "%d %15s %63s %63s %63s", &e.frame, e.cmd, a1, a2, a3); if (n < 2) continue;
    strcpy(e.arg, a1); e.x = atoi(a1); e.y = atoi(a2); evs = realloc(evs, (nev + 1) * sizeof(Ev)); evs[nev++] = e;
  }
  fclose(f);
}
void plat_shutdown(void) {}
int plat_running(void) { return frame < maxframes && !quitting; }
void plat_quit(void) { quitting = 1; }
static uint32_t keybit(const char *n) {
  struct { const char *n; uint32_t b; } t[] = { {"A",K_A},{"B",K_B},{"X",K_X},{"Y",K_Y},{"L",K_L},{"R",K_R},{"START",K_START},{"SELECT",K_SELECT},{"UP",K_UP},{"DOWN",K_DOWN},{"LEFT",K_LEFT},{"RIGHT",K_RIGHT} };
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(t[i].n, n)) return t[i].b; return 0;
}
void plat_poll(Input *in) {
  uint32_t prev = heldkeys, pressNow = 0; int wasTouch = touching;
  while (evi < nev && evs[evi].frame <= frame) {
    Ev *e = &evs[evi++];
    if (!strcmp(e->cmd, "down")) { touching = 1; tx = e->x; ty = e->y; }
    else if (!strcmp(e->cmd, "move")) { tx = e->x; ty = e->y; }
    else if (!strcmp(e->cmd, "up")) touching = 0;
    else if (!strcmp(e->cmd, "hold")) heldkeys |= keybit(e->arg);
    else if (!strcmp(e->cmd, "release")) heldkeys &= ~keybit(e->arg);
    else if (!strcmp(e->cmd, "key")) pressNow |= keybit(e->arg);
    else if (!strcmp(e->cmd, "shot")) { if (npend < 8) strncpy(pendingShots[npend++], e->arg, 63); }
    else if (!strcmp(e->cmd, "quit")) quitting = 1;
  }
  uint32_t held = heldkeys | pressNow;
  in->down = (held & ~prev) | pressNow; in->held = held; in->up = (prev & ~held) & ~pressNow; in->tx = tx; in->ty = ty; in->touching = touching; (void)wasTouch;
  heldkeys &= ~0u; /* 'key' is eenmalig: niet in heldkeys opgenomen */
}
static void write_ppm(const char *name, Surf *s) {
  char path[300]; snprintf(path, sizeof path, "%s/%s.ppm", outdir, name); FILE *f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
  for (int i = 0; i < s->w * s->h; i++) { uint32_t p = s->px[i]; unsigned char c[3] = { p & 255, (p >> 8) & 255, (p >> 16) & 255 }; fwrite(c, 1, 3, f); }
  fclose(f);
}
void plat_present(Surf *top, Surf *bot, int tc, int bc) { ptop = top; pbot = bot; (void)tc; (void)bc; }
void plat_wait_frame(void) {
  for (int i = 0; i < npend; i++) { char n[100]; if (ptop) { snprintf(n, sizeof n, "%s_top", pendingShots[i]); write_ppm(n, ptop); } if (pbot) { snprintf(n, sizeof n, "%s_bot", pendingShots[i]); write_ppm(n, pbot); } }
  npend = 0; frame++;
}
double plat_now_ms(void) { return 1000.0 + frame * (1000.0 / 60.0); }
static uint8_t *readfile(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *d = (uint8_t *)malloc(n + 1); size_t r = fread(d, 1, n, f); fclose(f); d[r] = 0; if (len) *len = r; return d;
}
uint8_t *plat_read_asset(const char *name, size_t *len) { char p[400]; snprintf(p, sizeof p, "%s/%s", romfs, name); return readfile(p, len); }
uint8_t *plat_read_save(const char *name, size_t *len) { char p[400]; snprintf(p, sizeof p, "%s/%s", savedir, name); return readfile(p, len); }
int plat_write_save(const char *name, const void *data, size_t len) { char p[400]; snprintf(p, sizeof p, "%s/%s", savedir, name); FILE *f = fopen(p, "wb"); if (!f) return 0; fwrite(data, 1, len, f); fclose(f); return 1; }
void plat_remove_save(const char *name) { char p[400]; snprintf(p, sizeof p, "%s/%s", savedir, name); remove(p); }
void plat_log(const char *m) { fprintf(stderr, "[log] %s\n", m); }
void plat_fatal(const char *m) { fprintf(stderr, "[FATAAL] %s\n", m); }
int main(void) { plat_init(); int r = game_run(); plat_shutdown(); return r; }

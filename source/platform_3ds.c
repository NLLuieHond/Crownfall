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

/* ===== Muziek: IMA-ADPCM (romfs/music.wav) afspelen via ndsp ===== */
uint8_t *plat_read_asset(const char *name, size_t *len);
#define MBUFS 16
static uint8_t *mus_data = 0; static size_t mus_len = 0, mus_start = 0, mus_off = 0;
static int mus_rate = 32000, mus_bs = 2048, mus_spb = 0, mus_n = 0, mus_blk = 0, mus_ok = 0, mus_on = 1, mus_next = 0;
static ndspWaveBuf mus_wb[MBUFS]; static s16 *mus_pcm = 0;
static Thread mus_th = 0; static volatile int mus_run = 0;
/* Twee nummers: 0 = spelmuziek (music.wav), 1 = gevechtsmuziek (battle.wav). De plek van het nummer dat pauzeert blijft bewaard. */
typedef struct { uint8_t *data; size_t len, start, off; int rate, bs, spb, n, blk; } Trk;
static Trk mus_sv[2]; static int mus_cur = 0; static volatile int mus_want = 0;
static void trk_save(int i) { Trk *t = &mus_sv[i]; t->data = mus_data; t->len = mus_len; t->start = mus_start; t->off = mus_off; t->rate = mus_rate; t->bs = mus_bs; t->spb = mus_spb; t->n = mus_n; t->blk = mus_blk; }
static void trk_load(int i) { Trk *t = &mus_sv[i]; mus_data = t->data; mus_len = t->len; mus_start = t->start; mus_off = t->off; mus_rate = t->rate; mus_bs = t->bs; mus_spb = t->spb; mus_n = t->n; mus_blk = t->blk; }
static const int ima_step[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int ima_idx[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static int ima_dec(int nib, int *pred, int *idx) {
  int step = ima_step[*idx], diff = step >> 3;
  if (nib & 1) diff += step >> 2; if (nib & 2) diff += step >> 1; if (nib & 4) diff += step; if (nib & 8) diff = -diff;
  int p = *pred + diff; if (p > 32767) p = 32767; if (p < -32768) p = -32768; *pred = p;
  int i = *idx + ima_idx[nib]; if (i < 0) i = 0; if (i > 88) i = 88; *idx = i; return p;
}
static void ima_block(const uint8_t *b, s16 *out) { /* stereo, 1 blok -> mus_spb frames */
  int pred[2], idx[2];
  for (int c = 0; c < 2; c++) { pred[c] = (s16)(b[c * 4] | (b[c * 4 + 1] << 8)); idx[c] = b[c * 4 + 2]; if (idx[c] > 88) idx[c] = 88; out[c] = (s16)pred[c]; }
  const uint8_t *p = b + 8; s16 *o = out + 2; int groups = (mus_bs - 8) / 8;
  for (int g = 0; g < groups; g++)
    for (int c = 0; c < 2; c++)
      for (int i = 0; i < 4; i++) {
        uint8_t by = *p++;
        o[(g * 8 + i * 2) * 2 + c] = (s16)ima_dec(by & 15, &pred[c], &idx[c]);
        o[(g * 8 + i * 2 + 1) * 2 + c] = (s16)ima_dec(by >> 4, &pred[c], &idx[c]);
      }
}
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static int mus_parse(void) {
  if (mus_len < 44 || memcmp(mus_data, "RIFF", 4) || memcmp(mus_data + 8, "WAVE", 4)) return 0;
  size_t o = 12; int ch = 0; mus_start = 0; size_t dsz = 0;
  while (o + 8 <= mus_len) {
    uint32_t sz = rd32(mus_data + o + 4);
    if (!memcmp(mus_data + o, "fmt ", 4)) { ch = rd16(mus_data + o + 10); mus_rate = rd32(mus_data + o + 12); mus_bs = rd16(mus_data + o + 20); }
    else if (!memcmp(mus_data + o, "data", 4)) { mus_start = o + 8; dsz = sz; break; }
    o += 8 + sz + (sz & 1);
  }
  if (ch != 2 || !mus_start || mus_bs < 64 || mus_bs > 8192 || (mus_bs & 7)) return 0;
  if (mus_start + dsz > mus_len) dsz = mus_len - mus_start;
  mus_n = (int)(dsz / mus_bs); mus_spb = mus_bs - 7; /* frames per blok */
  return mus_n > 0;
}
static void mus_switch(void) {
  int want = mus_want;
  if (want == mus_cur || !mus_sv[want].data) return;
  int q = 0; for (int i = 0; i < MBUFS; i++) if (mus_wb[i].status == NDSP_WBUF_QUEUED || mus_wb[i].status == NDSP_WBUF_PLAYING) q++;
  mus_blk -= q; while (mus_blk < 0) mus_blk += mus_n; mus_off = mus_start + (size_t)mus_blk * mus_bs; /* terug naar wat echt gespeeld was */
  trk_save(mus_cur); trk_load(want); mus_cur = want;
  if (want == 1) { mus_blk = 0; mus_off = mus_start; } /* gevechtsmuziek begint steeds opnieuw */
  ndspChnWaveBufClear(0); memset(mus_wb, 0, sizeof mus_wb); mus_next = 0;
}
static void mus_fill(void) {
  mus_switch();
  while (1) {
    ndspWaveBuf *w = &mus_wb[mus_next];
    if (w->status != NDSP_WBUF_FREE && w->status != NDSP_WBUF_DONE) break;
    s16 *pcm = mus_pcm + (size_t)mus_next * mus_spb * 2;
    ima_block(mus_data + mus_off, pcm);
    mus_blk++; mus_off += mus_bs; if (mus_blk >= mus_n) { mus_blk = 0; mus_off = mus_start; }
    DSP_FlushDataCache(pcm, (u32)mus_spb * 4);
    w->data_vaddr = pcm; w->nsamples = mus_spb; ndspChnWaveBufAdd(0, w);
    mus_next = (mus_next + 1) % MBUFS;
  }
}
static void mus_thread(void *arg) {
  while (mus_run) { mus_fill(); svcSleepThread(20000000LL); } /* elke 20 ms bijvullen, ook als het spel even vastzit */
}
/* ===== Geluidseffecten: romfs/sfx.bin (16-bit mono) op ndsp-kanalen 1..4 ===== */
#define SCH 4
static float g_mus = 0.55f, g_sfx = 0.9f; /* volume van muziek en effecten (instelbaar in het spel) */
static s16 *sfx_pcm = 0; static uint32_t sfx_n = 0, sfx_rate = 22050, *sfx_tab = 0; static int sfx_ok = 0, sfx_next = 0;
static ndspWaveBuf sfx_wb[SCH];
static void sfx_init(void) {
  if (!mus_ok) return; /* geen DSP: dan ook geen effecten */
  size_t len = 0; uint8_t *d = plat_read_asset("sfx.bin", &len); if (!d) return;
  if (len < 12 || memcmp(d, "SFX1", 4)) { free(d); return; }
  uint32_t rate = rd32(d + 4), n = rd32(d + 8);
  if (n == 0 || n > 64 || len < 12 + (size_t)n * 8) { free(d); return; }
  size_t dataoff = 12 + (size_t)n * 8, bytes = len - dataoff;
  sfx_pcm = (s16 *)linearAlloc(bytes + 64); sfx_tab = (uint32_t *)malloc((size_t)n * 8);
  if (!sfx_pcm || !sfx_tab) { if (sfx_pcm) linearFree(sfx_pcm); if (sfx_tab) free(sfx_tab); sfx_pcm = 0; sfx_tab = 0; free(d); return; }
  memcpy(sfx_pcm, d + dataoff, bytes); DSP_FlushDataCache(sfx_pcm, (u32)bytes);
  for (uint32_t i = 0; i < n; i++) {
    sfx_tab[i * 2] = rd32(d + 12 + i * 8); sfx_tab[i * 2 + 1] = rd32(d + 12 + i * 8 + 4);
    if (((size_t)sfx_tab[i * 2] + sfx_tab[i * 2 + 1]) * 2 > bytes) sfx_tab[i * 2 + 1] = 0;
  }
  free(d); sfx_n = n; sfx_rate = rate ? rate : 22050;
  float mix[12];
  for (int c = 0; c < SCH; c++) {
    ndspChnReset(1 + c); ndspChnSetInterp(1 + c, NDSP_INTERP_LINEAR); ndspChnSetRate(1 + c, (float)sfx_rate); ndspChnSetFormat(1 + c, NDSP_FORMAT_MONO_PCM16);
    memset(mix, 0, sizeof mix); mix[0] = mix[1] = g_sfx; ndspChnSetMix(1 + c, mix);
  }
  memset(sfx_wb, 0, sizeof sfx_wb); sfx_ok = 1;
}
static void sfx_exit(void) {
  if (!sfx_ok) return;
  sfx_ok = 0;
  for (int c = 0; c < SCH; c++) ndspChnWaveBufClear(1 + c);
}
void plat_sfx(int id) {
  if (!sfx_ok || id < 0 || (uint32_t)id >= sfx_n || !sfx_tab[id * 2 + 1]) return;
  int c = sfx_next; sfx_next = (sfx_next + 1) % SCH;
  ndspChnWaveBufClear(1 + c);
  ndspWaveBuf *w = &sfx_wb[c]; memset(w, 0, sizeof *w);
  w->data_vaddr = sfx_pcm + sfx_tab[id * 2]; w->nsamples = sfx_tab[id * 2 + 1]; w->looping = false;
  ndspChnWaveBufAdd(1 + c, w);
}
static void mus_init(void) {
  mus_data = plat_read_asset("music.wav", &mus_len); if (!mus_data || !mus_parse()) return;
  if (R_FAILED(ndspInit())) return;  /* geen DSP-firmware: dan geen muziek */
  mus_pcm = (s16 *)linearAlloc((size_t)MBUFS * mus_spb * 4); if (!mus_pcm) { ndspExit(); return; }
  ndspSetOutputMode(NDSP_OUTPUT_STEREO); ndspChnReset(0); ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
  ndspChnSetRate(0, (float)mus_rate); ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
  float mix[12]; memset(mix, 0, sizeof mix); mix[0] = mix[1] = g_mus; ndspChnSetMix(0, mix);
  memset(mus_wb, 0, sizeof mus_wb); mus_off = mus_start; mus_blk = 0; mus_next = 0; mus_ok = 1;
  memset(mus_sv, 0, sizeof mus_sv); trk_save(0); mus_cur = 0; mus_want = 0;
  { /* gevechtsmuziek (optioneel): moet hetzelfde blokformaat hebben als music.wav */
    int bs0 = mus_bs, rate0 = mus_rate;
    mus_data = plat_read_asset("battle.wav", &mus_len);
    if (mus_data && mus_parse() && mus_bs == bs0 && mus_rate == rate0) { mus_off = mus_start; mus_blk = 0; trk_save(1); }
    else if (mus_data) free(mus_data);
    trk_load(0);
  }
  mus_fill();
  svcSetThreadPriority(CUR_THREAD_HANDLE, 0x30); /* hoofddraad vaste prioriteit; de muziekdraad (0x28) mag dan altijd voorrang nemen */
  mus_run = 1; mus_th = threadCreate(mus_thread, 0, 32 * 1024, 0x28, -1, false); if (!mus_th) mus_run = 0;
}
void plat_music(int id) { mus_want = id ? 1 : 0; }
void plat_volume(int kind, int level) { /* kind 0 = muziek, 1 = effecten; level 0 (uit) .. 3 (hard) */
  static const float MV[4] = { 0.f, 0.25f, 0.55f, 0.85f }, SV[4] = { 0.f, 0.4f, 0.9f, 1.0f };
  float mix[12]; memset(mix, 0, sizeof mix); if (level < 0) level = 0; if (level > 3) level = 3;
  if (kind == 0) { g_mus = MV[level]; if (mus_ok) { mix[0] = mix[1] = g_mus; ndspChnSetMix(0, mix); } }
  else { g_sfx = SV[level]; if (sfx_ok) { mix[0] = mix[1] = g_sfx; for (int c = 0; c < SCH; c++) ndspChnSetMix(1 + c, mix); } }
}
static void mus_update(void) { if (mus_ok && !mus_run) mus_fill(); } /* alleen zonder aparte draad */
static void mus_toggle(void) { if (!mus_ok) return; mus_on = !mus_on; ndspChnSetPaused(0, !mus_on); }
static void mus_exit(void) { if (!mus_ok) return; sfx_exit(); if (mus_th) { mus_run = 0; threadJoin(mus_th, U64_MAX); threadFree(mus_th); mus_th = 0; } ndspChnWaveBufClear(0); ndspExit(); linearFree(mus_pcm); mus_ok = 0; }


void plat_init(void) {
  gfxInitDefault();
  gfxSetDoubleBuffering(GFX_TOP, false);
  gfxSetDoubleBuffering(GFX_BOTTOM, false);
  gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
  gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
  osSetSpeedupEnable(true); /* New 3DS: snelle modus */
  romfs_ok = (romfsInit() == 0);
  mkdir("sdmc:/3ds", 0777); mkdir(SAVEDIR, 0777);
  mus_init();
  sfx_init();
}
void plat_shutdown(void) { mus_exit(); if (romfs_ok) romfsExit(); gfxExit(); }
extern int g_quit_flag;
int plat_running(void) { return aptMainLoop() && !g_quit_flag; }
void plat_quit(void) { g_quit_flag = 1; }
int g_quit_flag = 0;

void plat_poll(Input *in) {
  hidScanInput();
  u32 d = hidKeysDown(), h = hidKeysHeld(), u = hidKeysUp(); touchPosition t; hidTouchRead(&t);
  if (d & KEY_SELECT) mus_toggle(); /* SELECT: muziek aan/uit */
  in->down = d & 0xFFF; in->held = h & 0xFFF; in->up = u & 0xFFF;
  in->touching = (h & KEY_TOUCH) ? 1 : 0; in->tx = t.px; in->ty = t.py;
}
static void blit(gfxScreen_t sc, Surf *s) {
  u16 fw, fh; u8 *fb = gfxGetFramebuffer(sc, GFX_LEFT, &fw, &fh); /* fw=240 (hoogte), fh=breedte */
  int W = s->w, H = s->h; if (W > fh) W = fh; if (H > fw) H = fw;
  /* in blokjes van 8 rijen: de lees- en schrijfgeheugen blijven zo in de cache */
  for (int yb = 0; yb < H; yb += 8) {
    int ye = yb + 8 < H ? yb + 8 : H;
    for (int x = 0; x < W; x++) {
      u8 *o = fb + x * fw * 3 + (fw - 1 - yb) * 3;
      const uint32_t *src = s->px + (size_t)yb * s->w + x;
      for (int y = yb; y < ye; y++, o -= 3, src += s->w) {
        uint32_t p = *src; o[0] = (p >> 16) & 255; o[1] = (p >> 8) & 255; o[2] = p & 255;
      }
    }
  }
}
void plat_present(Surf *top, Surf *bot, int tc, int bc) {
  if (tc) blit(GFX_TOP, top);
  if (bc) blit(GFX_BOTTOM, bot);
  gfxFlushBuffers(); gfxSwapBuffers();
}
void plat_wait_frame(void) { mus_update(); gspWaitForVBlank(); }
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

#include "canvas2d.h"
#include "stb_truetype.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define PI_F 3.14159265358979f

/* ------------------------------------------------------------------ oppervlak */
Surf *surf_new(int w, int h) {
  Surf *s = (Surf *)calloc(1, sizeof(Surf));
  if (w < 1) w = 1; if (h < 1) h = 1;
  s->w = w; s->h = h; s->px = (uint32_t *)calloc((size_t)w * h, 4); s->gen = 1;
  return s;
}
static void surf_free_mips(Surf *s) { for (int i = 0; i < 12; i++) { if (s->mip[i]) { free(s->mip[i]->px); free(s->mip[i]); s->mip[i] = 0; } } }
void surf_free(Surf *s) { if (!s) return; surf_free_mips(s); free(s->px); free(s); }
void surf_resize(Surf *s, int w, int h) {
  if (w < 1) w = 1; if (h < 1) h = 1;
  surf_free_mips(s); free(s->px); s->w = w; s->h = h; s->px = (uint32_t *)calloc((size_t)w * h, 4); s->gen++;
}
Surf *surf_from_rgba(const uint8_t *rgba, int w, int h) {
  Surf *s = surf_new(w, h);
  for (int i = 0; i < w * h; i++) {
    unsigned r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
    r = (r * a + 127) / 255; g = (g * a + 127) / 255; b = (b * a + 127) / 255;
    s->px[i] = r | (g << 8) | (b << 16) | (a << 24);
  }
  return s;
}
Surf *surf_mip(Surf *s, int level) {
  if (level <= 0) return s;
  if (level > 11) level = 11;
  Surf *prev = surf_mip(s, level - 1);
  if (prev->w <= 1 && prev->h <= 1) return prev;
  Surf *m = s->mip[level];
  if (m && s->mipgen[level] == s->gen) return m;
  int nw = prev->w > 1 ? prev->w / 2 : 1, nh = prev->h > 1 ? prev->h / 2 : 1;
  if (!m) { m = surf_new(nw, nh); s->mip[level] = m; } else if (m->w != nw || m->h != nh) { surf_resize(m, nw, nh); }
  for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) {
    int x0 = x * 2, y0 = y * 2, x1 = x0 + 1 < prev->w ? x0 + 1 : x0, y1 = y0 + 1 < prev->h ? y0 + 1 : y0;
    uint32_t p[4] = { prev->px[y0 * prev->w + x0], prev->px[y0 * prev->w + x1], prev->px[y1 * prev->w + x0], prev->px[y1 * prev->w + x1] };
    unsigned c[4] = { 0, 0, 0, 0 };
    for (int k = 0; k < 4; k++) { c[0] += p[k] & 255; c[1] += (p[k] >> 8) & 255; c[2] += (p[k] >> 16) & 255; c[3] += p[k] >> 24; }
    m->px[y * nw + x] = ((c[0] + 2) >> 2) | (((c[1] + 2) >> 2) << 8) | (((c[2] + 2) >> 2) << 16) | (((c[3] + 2) >> 2) << 24);
  }
  s->mipgen[level] = s->gen;
  return m;
}

/* ------------------------------------------------------------------ kleuren */
static int hexv(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
static uint8_t clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)(v + .5f); }
int css_color(const char *s, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
  while (*s == ' ') s++;
  *r = *g = *b = 0; *a = 255;
  if (*s == '#') {
    int n = (int)strlen(s + 1); int v[8] = { 0 }; if (n > 8) return 0;
    for (int i = 0; i < n && i < 8; i++) { v[i] = hexv(s[1 + i]); if (v[i] < 0) return 0; }
    if (n == 3 || n == 4) { *r = v[0] * 17; *g = v[1] * 17; *b = v[2] * 17; if (n == 4) *a = v[3] * 17; return 1; }
    if (n == 6 || n == 8) { *r = v[0] * 16 + v[1]; *g = v[2] * 16 + v[3]; *b = v[4] * 16 + v[5]; if (n == 8) *a = v[6] * 16 + v[7]; return 1; }
    return 0;
  }
  if (!strncmp(s, "rgb", 3)) {
    const char *p = strchr(s, '('); if (!p) return 0; p++;
    float f[4] = { 0, 0, 0, 1 }; int k = 0;
    while (*p && *p != ')' && k < 4) { char *e; f[k++] = strtof(p, &e); if (e == p) break; p = e; while (*p == ',' || *p == ' ' || *p == '/') p++; }
    *r = clamp8(f[0]); *g = clamp8(f[1]); *b = clamp8(f[2]); *a = clamp8(f[3] * 255.f); return 1;
  }
  if (!strcmp(s, "white")) { *r = *g = *b = 255; return 1; }
  if (!strcmp(s, "black")) return 1;
  if (!strcmp(s, "red")) { *r = 255; return 1; }
  if (!strcmp(s, "transparent")) { *a = 0; return 1; }
  return 0;
}
void grad_stop(Grad *g, float off, const char *css) {
  if (g->n >= 16) return;
  Stop st; st.off = off; if (!css_color(css, &st.r, &st.g, &st.b, &st.a)) { st.r = st.g = st.b = 0; st.a = 255; }
  int i = g->n; while (i > 0 && g->st[i - 1].off > off) { g->st[i] = g->st[i - 1]; i--; }
  g->st[i] = st; g->n++; g->lutok = 0;
}
static void grad_prepare(Grad *g) {
  if (g->lutok) return;
  for (int i = 0; i < 256; i++) {
    float t = i / 255.f, r = 0, gg = 0, b = 0, a = 0;
    if (g->n > 0) {
      if (t <= g->st[0].off) { r = g->st[0].r; gg = g->st[0].g; b = g->st[0].b; a = g->st[0].a; }
      else if (t >= g->st[g->n - 1].off) { Stop *s = &g->st[g->n - 1]; r = s->r; gg = s->g; b = s->b; a = s->a; }
      else for (int k = 0; k + 1 < g->n; k++) {
        Stop *s0 = &g->st[k], *s1 = &g->st[k + 1];
        if (t >= s0->off && t <= s1->off) { float u = s1->off > s0->off ? (t - s0->off) / (s1->off - s0->off) : 0; r = s0->r + (s1->r - s0->r) * u; gg = s0->g + (s1->g - s0->g) * u; b = s0->b + (s1->b - s0->b) * u; a = s0->a + (s1->a - s0->a) * u; break; }
      }
    }
    float al = a / 255.f;
    g->lut[i] = (uint32_t)clamp8(r * al) | ((uint32_t)clamp8(gg * al) << 8) | ((uint32_t)clamp8(b * al) << 16) | ((uint32_t)clamp8(a) << 24);
  }
  g->lutok = 1;
}

/* ------------------------------------------------------------------ matrix */
static Mat mat_id(void) { Mat m = { 1, 0, 0, 1, 0, 0 }; return m; }
static Mat mat_mul(Mat m, Mat n) {
  Mat r; r.a = m.a * n.a + m.c * n.b; r.b = m.b * n.a + m.d * n.b; r.c = m.a * n.c + m.c * n.d; r.d = m.b * n.c + m.d * n.d;
  r.e = m.a * n.e + m.c * n.f + m.e; r.f = m.b * n.e + m.d * n.f + m.f; return r;
}
static int mat_inv(Mat m, Mat *o) {
  float det = m.a * m.d - m.b * m.c; if (fabsf(det) < 1e-12f) return 0;
  float id = 1.f / det; o->a = m.d * id; o->b = -m.b * id; o->c = -m.c * id; o->d = m.a * id;
  o->e = -(o->a * m.e + o->c * m.f); o->f = -(o->b * m.e + o->d * m.f); return 1;
}
static float mat_scale(Mat m) { float d = fabsf(m.a * m.d - m.b * m.c); return sqrtf(d); }

/* ------------------------------------------------------------------ context */
static void paint_solid(Paint *p, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { memset(p, 0, sizeof *p); p->r = r; p->g = g; p->b = b; p->a = a; }
Ctx *ctx_new(Surf *s) {
  Ctx *c = (Ctx *)calloc(1, sizeof(Ctx)); c->surf = s; c->m = mat_id(); c->alpha = 1; c->op = 0;
  paint_solid(&c->fill, 0, 0, 0, 255); paint_solid(&c->stroke, 0, 0, 0, 255);
  c->lineWidth = 1; c->fsize = 10; c->smooth = 1; c->tbase = 0;
  return c;
}
void ctx_free(Ctx *c) { if (!c) return; free(c->path.x); free(c->path.y); free(c->path.sub); free(c->path.cl); free(c); }
void ctx_save(Ctx *c) {
  if (c->sp >= 32) return;
  typeof(c->stack[0]) *s = &c->stack[c->sp++];
  s->m = c->m; s->alpha = c->alpha; s->op = c->op; s->fill = c->fill; s->stroke = c->stroke; s->lw = c->lineWidth; memcpy(s->dash, c->dash, sizeof c->dash); s->nd = c->ndash;
  s->face = c->face; s->fsize = c->fsize; s->italic = c->italic; s->ta = c->talign; s->tb = c->tbase; s->smooth = c->smooth;
}
void ctx_restore(Ctx *c) {
  if (c->sp <= 0) return;
  typeof(c->stack[0]) *s = &c->stack[--c->sp];
  c->m = s->m; c->alpha = s->alpha; c->op = s->op; c->fill = s->fill; c->stroke = s->stroke; c->lineWidth = s->lw; memcpy(c->dash, s->dash, sizeof c->dash); c->ndash = s->nd;
  c->face = s->face; c->fsize = s->fsize; c->italic = s->italic; c->talign = s->ta; c->tbase = s->tb; c->smooth = s->smooth;
}
void ctx_translate(Ctx *c, float x, float y) { Mat t = { 1, 0, 0, 1, x, y }; c->m = mat_mul(c->m, t); }
void ctx_scale(Ctx *c, float x, float y) { Mat t = { x, 0, 0, y, 0, 0 }; c->m = mat_mul(c->m, t); }
void ctx_set_dash(Ctx *c, const float *d, int n) { if (n > 8) n = 8; c->ndash = 0; float sum = 0; for (int i = 0; i < n; i++) { c->dash[i] = d[i]; sum += d[i]; } if (n > 0 && sum > 0) c->ndash = (n & 1) ? 0 : n; if (n == 1 && d[0] > 0) { c->dash[1] = d[0]; c->ndash = 2; } }
void ctx_set_font(Ctx *c, const char *css) {
  char buf[256]; strncpy(buf, css, 255); buf[255] = 0; int italic = 0, weight = 400; float size = 10;
  char *save = 0; for (char *t = strtok_r(buf, " ,", &save); t; t = strtok_r(0, " ,", &save)) {
    if (!strcmp(t, "italic") || !strcmp(t, "oblique")) italic = 1;
    else if (!strcmp(t, "bold")) weight = 700;
    else if (isdigit((unsigned char)t[0])) { char *e; float v = strtof(t, &e); if (!strncmp(e, "px", 2)) { size = v; break; } else weight = (int)v; }
  }
  c->italic = italic; c->fsize = size; c->face = italic ? 2 : (weight >= 650 ? 1 : 0);
}

/* ------------------------------------------------------------------ pad */
static void path_pt(Path *p, float x, float y) {
  if (p->n >= p->cap) { p->cap = p->cap ? p->cap * 2 : 64; p->x = (float *)realloc(p->x, p->cap * 4); p->y = (float *)realloc(p->y, p->cap * 4); }
  p->x[p->n] = x; p->y[p->n] = y; p->n++;
}
static void path_newsub(Path *p) {
  if (p->ns >= p->scap) { p->scap = p->scap ? p->scap * 2 : 16; p->sub = (int *)realloc(p->sub, p->scap * sizeof(int)); p->cl = (char *)realloc(p->cl, p->scap); }
  p->sub[p->ns] = p->n; p->cl[p->ns] = 0; p->ns++;
}
void ctx_begin(Ctx *c) { c->path.n = 0; c->path.ns = 0; c->path.afterClose = 0; }
static void dev(Ctx *c, float x, float y, float *ox, float *oy) { *ox = c->m.a * x + c->m.c * y + c->m.e; *oy = c->m.b * x + c->m.d * y + c->m.f; }
void ctx_move(Ctx *c, float x, float y) {
  Path *p = &c->path; float dx, dy; dev(c, x, y, &dx, &dy);
  if (p->ns > 0 && p->sub[p->ns - 1] == p->n - 1 && !p->cl[p->ns - 1]) { p->x[p->n - 1] = dx; p->y[p->n - 1] = dy; return; }
  path_newsub(p); path_pt(p, dx, dy); p->afterClose = 0;
}
void ctx_line(Ctx *c, float x, float y) {
  Path *p = &c->path; float dx, dy; dev(c, x, y, &dx, &dy);
  if (p->ns == 0) { path_newsub(p); path_pt(p, dx, dy); return; }
  if (p->afterClose) { float sx = p->x[p->sub[p->ns - 1]], sy = p->y[p->sub[p->ns - 1]]; path_newsub(p); path_pt(p, sx, sy); p->afterClose = 0; }
  path_pt(p, dx, dy);
}
void ctx_close(Ctx *c) { Path *p = &c->path; if (p->ns) { p->cl[p->ns - 1] = 1; p->afterClose = 1; } }
void ctx_ellipse(Ctx *c, float x, float y, float rx, float ry, float rot, float a0, float a1, int ccw) {
  float two = 2 * PI_F, sweep;
  if (!ccw) { float d = a1 - a0; if (d >= two) sweep = two; else { if (d < 0) { d = fmodf(d, two); if (d < 0) d += two; } sweep = d; } }
  else { float d = a0 - a1; if (d >= two) sweep = -two; else { if (d < 0) { d = fmodf(d, two); if (d < 0) d += two; } sweep = -d; } }
  float rd = fmaxf(rx, ry) * mat_scale(c->m), step = rd > 0.3f ? 2 * acosf(1 - 0.1f / rd) : 1.f; if (step < 0.02f) step = 0.02f;
  int n = (int)ceilf(fabsf(sweep) / step); if (n < 4) n = 4; if (n > 240) n = 240;
  float cr = cosf(rot), sr = sinf(rot);
  for (int i = 0; i <= n; i++) {
    float an = a0 + sweep * i / n, ex = rx * cosf(an), ey = ry * sinf(an);
    float ux = x + ex * cr - ey * sr, uy = y + ex * sr + ey * cr;
    if (i == 0 && c->path.ns == 0) ctx_move(c, ux, uy); else ctx_line(c, ux, uy);
  }
}
void ctx_arc(Ctx *c, float x, float y, float r, float a0, float a1, int ccw) { ctx_ellipse(c, x, y, r, r, 0, a0, a1, ccw); }

/* ------------------------------------------------------------------ pixel verwerking */
static inline unsigned div255(unsigned x) { return ((x + 128) * 257) >> 16; }
static inline uint32_t blend(uint32_t d, unsigned sr, unsigned sg, unsigned sb, unsigned sa, int op) {
  if (op == 0) {
    if (sa > 255) sa = 255; if (sr > 255) sr = 255; if (sg > 255) sg = 255; if (sb > 255) sb = 255;
    const uint32_t M = 0x00ff00ffu; unsigned inv = 255 - sa;
    uint32_t rb = (d & M) * inv + 0x00800080u; rb = ((rb + ((rb >> 8) & M)) >> 8) & M;
    uint32_t ag = ((d >> 8) & M) * inv + 0x00800080u; ag = ((ag + ((ag >> 8) & M)) >> 8) & M;
    rb += sr | (sb << 16); ag += sg | (sa << 16);
    rb |= ((rb >> 8) & 0x00010001u) * 255u; ag |= ((ag >> 8) & 0x00010001u) * 255u;
    return (rb & M) | ((ag & M) << 8);
  }
  unsigned dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255, da = d >> 24;
  if (op == 0) {
  } else if (op == 1) {
    unsigned inv = 255 - sa; dr = div255(sr * da) + div255(dr * inv); dg = div255(sg * da) + div255(dg * inv); db = div255(sb * da) + div255(db * inv);
  } else {
    dr = div255(dr * sa); dg = div255(dg * sa); db = div255(db * sa); da = div255(da * sa);
  }
  if (dr > 255) dr = 255; if (dg > 255) dg = 255; if (db > 255) db = 255; if (da > 255) da = 255;
  return dr | (dg << 8) | (db << 16) | (da << 24);
}
static inline uint32_t lerp8(uint32_t a, uint32_t b, unsigned t) {
  uint32_t rb = (((a & 0x00ff00ffu) * (256 - t) + (b & 0x00ff00ffu) * t) >> 8) & 0x00ff00ffu;
  uint32_t ag = (((a >> 8) & 0x00ff00ffu) * (256 - t) + ((b >> 8) & 0x00ff00ffu) * t) & 0xff00ff00u;
  return rb | ag;
}
static inline uint32_t sample_nearest(Surf *s, int x, int y) { return s->px[y * s->w + x]; }

/* schrijf een rij met dekking cov[] (0..1) vanaf (x0,y), n pixels */
static void paint_row(Ctx *c, const Paint *p, int y, int x0, int n, const float *cov) {
  Surf *s = c->surf; if (y < 0 || y >= s->h) return;
  uint32_t *row = s->px + (size_t)y * s->w; float al = c->alpha; int op = c->op;
  if (p->kind == 0) {
    float pa = p->a / 255.f * al; float pr = p->r * pa, pg = p->g * pa, pb = p->b * pa, pA = 255.f * pa;
    for (int i = 0; i < n; i++) {
      float cv = cov[i]; int x = x0 + i; if (cv <= 0.002f || x < 0 || x >= s->w) continue;
      if (cv >= 0.999f && pA >= 254.5f && op == 0) { row[x] = (uint32_t)clamp8(pr) | ((uint32_t)clamp8(pg) << 8) | ((uint32_t)clamp8(pb) << 16) | 0xff000000u; continue; }
      row[x] = blend(row[x], (unsigned)(pr * cv + .5f), (unsigned)(pg * cv + .5f), (unsigned)(pb * cv + .5f), (unsigned)(pA * cv + .5f), op);
    }
    return;
  }
  Mat inv; if (!mat_inv(c->m, &inv)) return;
  if (p->kind == 1 && p->grad) {
    Grad *g = p->grad; grad_prepare(g);
    for (int i = 0; i < n; i++) {
      float cv = cov[i]; int x = x0 + i; if (cv <= 0.002f || x < 0 || x >= s->w) continue;
      float px = x + .5f, py = y + .5f, ux = inv.a * px + inv.c * py + inv.e, uy = inv.b * px + inv.d * py + inv.f, t = 0; int ok = 1;
      if (!g->radial) {
        float dx = g->x1 - g->x0, dy = g->y1 - g->y0, dd = dx * dx + dy * dy; t = dd > 0 ? ((ux - g->x0) * dx + (uy - g->y0) * dy) / dd : 0;
      } else {
        float cdx = g->x1 - g->x0, cdy = g->y1 - g->y0, dr = g->r1 - g->r0, pdx = ux - g->x0, pdy = uy - g->y0;
        float A = cdx * cdx + cdy * cdy - dr * dr, B = pdx * cdx + pdy * cdy + g->r0 * dr, C = pdx * pdx + pdy * pdy - g->r0 * g->r0; ok = 0;
        if (fabsf(A) < 1e-6f) { if (fabsf(B) > 1e-9f) { float sv = C / (2 * B); if (g->r0 + sv * dr >= 0) { t = sv; ok = 1; } } }
        else { float disc = B * B - A * C; if (disc >= 0) { float sq = sqrtf(disc), s1 = (B + sq) / A, s2 = (B - sq) / A, hi = s1 > s2 ? s1 : s2, lo = s1 > s2 ? s2 : s1; if (g->r0 + hi * dr >= 0) { t = hi; ok = 1; } else if (g->r0 + lo * dr >= 0) { t = lo; ok = 1; } } }
      }
      if (!ok) continue;
      if (t < 0) t = 0; if (t > 1) t = 1;
      uint32_t lc = g->lut[(int)(t * 255.f + .5f)]; float k = cv * al;
      unsigned sr = (unsigned)((lc & 255) * k + .5f), sg = (unsigned)(((lc >> 8) & 255) * k + .5f), sb = (unsigned)(((lc >> 16) & 255) * k + .5f), sa = (unsigned)((lc >> 24) * k + .5f);
      row[x] = blend(row[x], sr, sg, sb, sa, op);
    }
    return;
  }
  if (p->kind == 2 && p->pat) {
    Surf *t = p->pat;
    if (inv.b == 0 && inv.c == 0) {
      float uy = inv.d * (y + .5f) + inv.f, itw = 1.f / t->w, ith = 1.f / t->h; float fy = floorf(uy);
      int ty = (int)(fy - floorf(fy * ith) * t->h); if (ty >= t->h) ty -= t->h; if (ty < 0) ty += t->h;
      const uint32_t *trow = t->px + (size_t)ty * t->w; int solid = al >= .999f && op == 0;
      for (int i = 0; i < n; i++) {
        float cv = cov[i]; int x = x0 + i; if (cv <= 0.002f || x < 0 || x >= s->w) continue;
        float fl = floorf(inv.a * (x + .5f) + inv.e); int tx = (int)(fl - floorf(fl * itw) * t->w); if (tx >= t->w) tx -= t->w; if (tx < 0) tx += t->w;
        uint32_t lc = trow[tx];
        if (solid && cv >= .999f && (lc >> 24) == 255) { row[x] = lc; continue; }
        float k = cv * al;
        row[x] = blend(row[x], (unsigned)((lc & 255) * k + .5f), (unsigned)(((lc >> 8) & 255) * k + .5f), (unsigned)(((lc >> 16) & 255) * k + .5f), (unsigned)((lc >> 24) * k + .5f), op);
      }
      return;
    }
    for (int i = 0; i < n; i++) {
      float cv = cov[i]; int x = x0 + i; if (cv <= 0.002f || x < 0 || x >= s->w) continue;
      float px = x + .5f, py = y + .5f, ux = inv.a * px + inv.c * py + inv.e, uy = inv.b * px + inv.d * py + inv.f;
      int tx = (int)floorf(ux) % t->w, ty = (int)floorf(uy) % t->h; if (tx < 0) tx += t->w; if (ty < 0) ty += t->h;
      uint32_t lc = sample_nearest(t, tx, ty); float k = cv * al;
      row[x] = blend(row[x], (unsigned)((lc & 255) * k + .5f), (unsigned)(((lc >> 8) & 255) * k + .5f), (unsigned)(((lc >> 16) & 255) * k + .5f), (unsigned)((lc >> 24) * k + .5f), op);
    }
  }
}

/* ------------------------------------------------------------------ polygonen vullen (accumulatiebuffer) */
static float *g_acc = 0; static size_t g_acc_cap = 0; static float *g_cov = 0; static int g_cov_cap = 0;
static void acc_seg(float *a, int stride, int bh, float x0, float y0, float x1, float y1, float dir) {
  if (y0 == y1) return;
  float dxdy = (x1 - x0) / (y1 - y0), x = x0;
  int ys = (int)floorf(y0); if (ys < 0) ys = 0; int ye = (int)ceilf(y1); if (ye > bh) ye = bh;
  for (int y = ys; y < ye; y++) {
    float yt = y > y0 ? (float)y : y0, yb = (y + 1) < y1 ? (float)(y + 1) : y1, dy = yb - yt; if (dy <= 0) continue;
    float xn = x + dxdy * dy, d = dy * dir; float *ln = a + (size_t)y * stride;
    float xa = x < xn ? x : xn, xb = x < xn ? xn : x; float xaf = floorf(xa); int xai = (int)xaf; float xbc = ceilf(xb); int xbi = (int)xbc;
    if (xbi <= xai + 1) { float xmf = 0.5f * (x + xn) - xaf; ln[xai] += d - d * xmf; ln[xai + 1] += d * xmf; }
    else {
      float s = 1.f / (xb - xa), x0f = xa - xaf, a0 = 0.5f * s * (1.f - x0f) * (1.f - x0f), x1f = xb - xbc + 1.f, am = 0.5f * s * x1f * x1f;
      ln[xai] += d * a0;
      if (xbi == xai + 2) ln[xai + 1] += d * (1.f - a0 - am);
      else { float a1 = s * (1.5f - x0f); ln[xai + 1] += d * (a1 - a0); for (int xi = xai + 2; xi < xbi - 1; xi++) ln[xi] += d * s; float a2 = a1 + (xbi - xai - 3) * s; ln[xbi - 1] += d * (1.f - a2 - am); }
      ln[xbi] += d * am;
    }
    x = xn;
  }
}
static void acc_clipx(float *a, int stride, int bw, int bh, float x0, float y0, float x1, float y1, float dir) {
  if (x0 <= 0 && x1 <= 0) { acc_seg(a, stride, bh, 0, y0, 0, y1, dir); return; }
  if (x0 >= bw && x1 >= bw) { acc_seg(a, stride, bh, (float)bw, y0, (float)bw, y1, dir); return; }
  if (x0 >= 0 && x0 <= bw && x1 >= 0 && x1 <= bw) { acc_seg(a, stride, bh, x0, y0, x1, y1, dir); return; }
  if ((x0 < 0) != (x1 < 0) && (x0 < 0 || x1 < 0)) {
    float t = (0 - x0) / (x1 - x0), ym = y0 + t * (y1 - y0);
    if (x0 < 0) { acc_clipx(a, stride, bw, bh, 0, y0, 0, ym, dir); acc_clipx(a, stride, bw, bh, 0, ym, x1, y1, dir); }
    else { acc_clipx(a, stride, bw, bh, x0, y0, 0, ym, dir); acc_clipx(a, stride, bw, bh, 0, ym, 0, y1, dir); }
    return;
  }
  float t = (bw - x0) / (x1 - x0), ym = y0 + t * (y1 - y0);
  if (x0 < bw) { acc_clipx(a, stride, bw, bh, x0, y0, (float)bw, ym, dir); acc_clipx(a, stride, bw, bh, (float)bw, ym, (float)bw, y1, dir); }
  else { acc_clipx(a, stride, bw, bh, (float)bw, y0, (float)bw, ym, dir); acc_clipx(a, stride, bw, bh, (float)bw, ym, x1, y1, dir); }
}
static void acc_edge(float *a, int stride, int bw, int bh, float x0, float y0, float x1, float y1) {
  if (y0 == y1) return; float dir = 1;
  if (y0 > y1) { float t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
  if (y1 <= 0 || y0 >= bh) return;
  float dxdy = (x1 - x0) / (y1 - y0);
  if (y0 < 0) { x0 -= y0 * dxdy; y0 = 0; }
  if (y1 > bh) { x1 -= (y1 - bh) * dxdy; y1 = (float)bh; }
  acc_clipx(a, stride, bw, bh, x0, y0, x1, y1, dir);
}
static void fill_path(Ctx *c, Path *p, const Paint *paint) {
  if (p->n < 3) return;
  float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
  for (int i = 0; i < p->n; i++) { if (p->x[i] < minx) minx = p->x[i]; if (p->x[i] > maxx) maxx = p->x[i]; if (p->y[i] < miny) miny = p->y[i]; if (p->y[i] > maxy) maxy = p->y[i]; }
  if (!(minx == minx) || !(maxx == maxx)) return;
  int bx0 = (int)floorf(minx), by0 = (int)floorf(miny), bx1 = (int)ceilf(maxx), by1 = (int)ceilf(maxy);
  if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; if (bx1 > c->surf->w) bx1 = c->surf->w; if (by1 > c->surf->h) by1 = c->surf->h;
  int bw = bx1 - bx0, bh = by1 - by0; if (bw <= 0 || bh <= 0) return;
  int stride = bw + 2; size_t need = (size_t)stride * bh;
  if (need > g_acc_cap) { g_acc = (float *)realloc(g_acc, need * 4); g_acc_cap = need; }
  memset(g_acc, 0, need * 4);
  for (int s = 0; s < p->ns; s++) {
    int a = p->sub[s], b = (s + 1 < p->ns) ? p->sub[s + 1] : p->n; if (b - a < 2) continue;
    for (int i = a; i < b; i++) { int j = (i + 1 < b) ? i + 1 : a; acc_edge(g_acc, stride, bw, bh, p->x[i] - bx0, p->y[i] - by0, p->x[j] - bx0, p->y[j] - by0); }
  }
  if (bw > g_cov_cap) { g_cov = (float *)realloc(g_cov, bw * 4); g_cov_cap = bw; }
  for (int y = 0; y < bh; y++) {
    float acc = 0, *ln = g_acc + (size_t)y * stride; int any = 0;
    for (int x = 0; x < bw; x++) { acc += ln[x]; float v = fabsf(acc); if (v > 1) v = 1; g_cov[x] = v; if (v > 0.002f) any = 1; }
    if (any) paint_row(c, paint, by0 + y, bx0, bw, g_cov);
  }
  c->surf->gen++;
}
void ctx_fill(Ctx *c) { fill_path(c, &c->path, &c->fill); }

/* ------------------------------------------------------------------ lijnen */
static void quad(Path *o, float ax, float ay, float bx, float by, float hw) {
  float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy); if (l < 1e-6f) return; dx /= l; dy /= l;
  float nx = -dy * hw, ny = dx * hw;
  float px[4] = { ax + nx, bx + nx, bx - nx, ax - nx }, py[4] = { ay + ny, by + ny, by - ny, ay - ny };
  /* altijd dezelfde draairichting */
  float area = 0; for (int i = 0; i < 4; i++) { int j = (i + 1) & 3; area += px[i] * py[j] - px[j] * py[i]; }
  path_newsub(o);
  if (area >= 0) for (int i = 0; i < 4; i++) path_pt(o, px[i], py[i]); else for (int i = 3; i >= 0; i--) path_pt(o, px[i], py[i]);
}
static void disc(Path *o, float cx, float cy, float r) {
  path_newsub(o); int n = r > 4 ? 16 : 10; for (int i = 0; i < n; i++) { float an = 2 * PI_F * i / n; path_pt(o, cx + r * cosf(an), cy + r * sinf(an)); }
}
void ctx_stroke(Ctx *c) {
  Path *p = &c->path; Path o; memset(&o, 0, sizeof o);
  float sc = mat_scale(c->m), hw = 0.5f * c->lineWidth * sc; if (hw < 0.35f) hw = 0.35f;
  float dsum = 0; float dsh[8]; int nd = c->ndash; for (int i = 0; i < nd; i++) { dsh[i] = c->dash[i] * sc; dsum += dsh[i]; } if (dsum <= 0) nd = 0;
  for (int s = 0; s < p->ns; s++) {
    int a = p->sub[s], b = (s + 1 < p->ns) ? p->sub[s + 1] : p->n; int cnt = b - a; if (cnt < 2) continue;
    int segs = p->cl[s] ? cnt : cnt - 1; int di = 0; float drem = nd ? dsh[0] : 0; int on = 1;
    for (int i = 0; i < segs; i++) {
      int j = a + (i + 1) % cnt, k = a + i; float ax = p->x[k], ay = p->y[k], bx = p->x[j], by = p->y[j];
      if (!nd) { quad(&o, ax, ay, bx, by, hw); if (hw >= 0.9f) disc(&o, ax, ay, hw); continue; }
      float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy), pos = 0; if (l < 1e-6f) continue;
      while (pos < l) {
        float step = drem < l - pos ? drem : l - pos;
        if (on && step > 0) quad(&o, ax + dx * pos / l, ay + dy * pos / l, ax + dx * (pos + step) / l, ay + dy * (pos + step) / l, hw);
        pos += step; drem -= step;
        if (drem <= 1e-4f) { di = (di + 1) % nd; drem = dsh[di] > 0 ? dsh[di] : 1e-3f; on = !(di & 1) ? 1 : 0; }
      }
    }
    if (hw >= 0.9f && !nd) disc(&o, p->x[b - 1], p->y[b - 1], hw);
  }
  fill_path(c, &o, &c->stroke); free(o.x); free(o.y); free(o.sub); free(o.cl);
}

/* ------------------------------------------------------------------ rechthoeken */
void ctx_fill_rect(Ctx *c, float x, float y, float w, float h) {
  if (c->m.b == 0 && c->m.c == 0) {
    float x0 = c->m.a * x + c->m.e, x1 = c->m.a * (x + w) + c->m.e, y0 = c->m.d * y + c->m.f, y1 = c->m.d * (y + h) + c->m.f, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; } if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    int ix0 = (int)floorf(x0), ix1 = (int)ceilf(x1), iy0 = (int)floorf(y0), iy1 = (int)ceilf(y1);
    if (ix0 < 0) ix0 = 0; if (iy0 < 0) iy0 = 0; if (ix1 > c->surf->w) ix1 = c->surf->w; if (iy1 > c->surf->h) iy1 = c->surf->h;
    int n = ix1 - ix0; if (n <= 0 || iy1 <= iy0) return;
    if (n > g_cov_cap) { g_cov = (float *)realloc(g_cov, n * 4); g_cov_cap = n; }
    for (int yy = iy0; yy < iy1; yy++) {
      float cy = fminf((float)(yy + 1), y1) - fmaxf((float)yy, y0); if (cy <= 0) continue;
      if (c->fill.kind == 0 && c->op == 0 && cy >= 0.999f) {
        /* snelle weg voor effen kleur: binnenste deel in een keer, randen met dekking */
        int xa = (int)ceilf(x0), xb = (int)floorf(x1); if (xa < ix0) xa = ix0; if (xb > ix1) xb = ix1;
        if (xa < xb) {
          float pa = c->fill.a / 255.f * c->alpha; unsigned sa = (unsigned)(255.f * pa + .5f);
          uint32_t sp = (uint32_t)(c->fill.r * pa + .5f) | ((uint32_t)(c->fill.g * pa + .5f) << 8) | ((uint32_t)(c->fill.b * pa + .5f) << 16) | ((uint32_t)sa << 24);
          uint32_t *row = c->surf->px + (size_t)yy * c->surf->w;
          if (sa >= 255) { for (int xx = xa; xx < xb; xx++) row[xx] = sp; }
          else if (sa > 0) {
            uint32_t inv8 = 255 - sa, prb = sp & 0x00ff00ffu, pag = sp & 0xff00ff00u;
            for (int xx = xa; xx < xb; xx++) {
              uint32_t d = row[xx];
              uint32_t rb2 = (d & 0x00ff00ffu) * inv8 + 0x00800080u; rb2 = ((rb2 + ((rb2 >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
              uint32_t ag2 = ((d >> 8) & 0x00ff00ffu) * inv8 + 0x00800080u; ag2 = (ag2 + ((ag2 >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
              row[xx] = (prb + rb2) | (pag + ag2);
            }
          }
        }
        for (int xx = ix0; xx < xa && xx < ix1; xx++) { float cx = fminf((float)(xx + 1), x1) - fmaxf((float)xx, x0); g_cov[0] = cx > 0 ? cx * cy : 0; paint_row(c, &c->fill, yy, xx, 1, g_cov); }
        for (int xx = (xb > xa ? xb : xa); xx < ix1; xx++) { float cx = fminf((float)(xx + 1), x1) - fmaxf((float)xx, x0); g_cov[0] = cx > 0 ? cx * cy : 0; paint_row(c, &c->fill, yy, xx, 1, g_cov); }
        continue;
      }
      for (int xx = ix0; xx < ix1; xx++) { float cx = fminf((float)(xx + 1), x1) - fmaxf((float)xx, x0); g_cov[xx - ix0] = cx > 0 ? cx * cy : 0; }
      paint_row(c, &c->fill, yy, ix0, n, g_cov);
    }
    c->surf->gen++; return;
  }
  Path q; memset(&q, 0, sizeof q); float px[4] = { x, x + w, x + w, x }, py[4] = { y, y, y + h, y + h }; path_newsub(&q);
  for (int i = 0; i < 4; i++) { float dx, dy; dev(c, px[i], py[i], &dx, &dy); path_pt(&q, dx, dy); }
  fill_path(c, &q, &c->fill); free(q.x); free(q.y); free(q.sub); free(q.cl);
}
void ctx_stroke_rect(Ctx *c, float x, float y, float w, float h) {
  Path save = c->path; memset(&c->path, 0, sizeof c->path);
  ctx_move(c, x, y); ctx_line(c, x + w, y); ctx_line(c, x + w, y + h); ctx_line(c, x, y + h); ctx_close(c); ctx_stroke(c);
  free(c->path.x); free(c->path.y); free(c->path.sub); free(c->path.cl); c->path = save;
}

/* ------------------------------------------------------------------ afbeeldingen */
void ctx_draw_image(Ctx *c, Surf *img, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh) {
  if (!img || sw <= 0 || sh <= 0 || dw == 0 || dh == 0) return;
  Mat M = c->m, inv; if (!mat_inv(M, &inv)) return;
  float cx[4] = { dx, dx + dw, dx + dw, dx }, cy[4] = { dy, dy, dy + dh, dy + dh }, minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
  for (int i = 0; i < 4; i++) { float X, Y; dev(c, cx[i], cy[i], &X, &Y); if (X < minx) minx = X; if (X > maxx) maxx = X; if (Y < miny) miny = Y; if (Y > maxy) maxy = Y; }
  int x0 = (int)floorf(minx), y0 = (int)floorf(miny), x1 = (int)ceilf(maxx), y1 = (int)ceilf(maxy);
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > c->surf->w) x1 = c->surf->w; if (y1 > c->surf->h) y1 = c->surf->h;
  if (x1 <= x0 || y1 <= y0) return;
  float sxd = sqrtf(M.a * M.a + M.b * M.b), syd = sqrtf(M.c * M.c + M.d * M.d);
  float kx = sw / (fabsf(dw) * (sxd > 1e-6f ? sxd : 1e-6f)), ky = sh / (fabsf(dh) * (syd > 1e-6f ? syd : 1e-6f)), k = kx > ky ? kx : ky;
  int L = 0; if (c->smooth) while (k >= 2.0f && L < 10) { k *= .5f; L++; }
  Surf *m = surf_mip(img, L); if (m == img) L = 0;
  float inv2 = 1.f / (float)(1 << L), mx0 = sx * inv2, my0 = sy * inv2, mx1 = (sx + sw) * inv2, my1 = (sy + sh) * inv2;
  int ixmin = (int)floorf(mx0), iymin = (int)floorf(my0), ixmax = (int)ceilf(mx1) - 1, iymax = (int)ceilf(my1) - 1;
  if (ixmin < 0) ixmin = 0; if (iymin < 0) iymin = 0; if (ixmax > m->w - 1) ixmax = m->w - 1; if (iymax > m->h - 1) iymax = m->h - 1;
  if (ixmax < ixmin || iymax < iymin) return;
  float al = c->alpha; int op = c->op, smooth = c->smooth;
  float rx0 = dw > 0 ? dx : dx + dw, rx1 = dw > 0 ? dx + dw : dx, ry0 = dh > 0 ? dy : dy + dh, ry1 = dh > 0 ? dy + dh : dy;
  if (M.b == 0 && M.c == 0) {
    /* snelle weg: alleen verschuiven en schalen. Alle posities per kolom en per rij vooraf uitrekenen, daarna alleen geheeltallige rekenkunde. */
    int nw = x1 - x0, nh = y1 - y0;
    int *colA = (int *)malloc(sizeof(int) * (size_t)nw * 3), *rowA = (int *)malloc(sizeof(int) * (size_t)nh * 3);
    if (!colA || !rowA) { free(colA); free(rowA); return; }
    int *cxa = colA, *cxb = colA + nw, *cfx = colA + 2 * nw, *rya = rowA, *ryb = rowA + nh, *rfy = rowA + 2 * nh;
    for (int i = 0; i < nw; i++) {
      float ux = inv.a * (x0 + i + .5f) + inv.e; cfx[i] = 0; cxa[i] = -1;
      if (ux < rx0 || ux >= rx1) continue;
      float tx = (sx + (ux - dx) / dw * sw) * inv2; int ix, fx = 0;
      if (smooth) { tx -= .5f; ix = (int)floorf(tx); fx = (int)((tx - ix) * 256.f); if (fx > 255) fx = 255; if (fx < 0) fx = 0; }
      else ix = (int)floorf(tx);
      int ia = ix < ixmin ? ixmin : ix > ixmax ? ixmax : ix, ib = ix + 1 < ixmin ? ixmin : ix + 1 > ixmax ? ixmax : ix + 1;
      cxa[i] = ia; cxb[i] = ib; cfx[i] = fx;
    }
    for (int j = 0; j < nh; j++) {
      float uy = inv.d * (y0 + j + .5f) + inv.f; rfy[j] = 0; rya[j] = -1;
      if (uy < ry0 || uy >= ry1) continue;
      float ty = (sy + (uy - dy) / dh * sh) * inv2; int iy, fy = 0;
      if (smooth) { ty -= .5f; iy = (int)floorf(ty); fy = (int)((ty - iy) * 256.f); if (fy > 255) fy = 255; if (fy < 0) fy = 0; }
      else iy = (int)floorf(ty);
      rya[j] = iy < iymin ? iymin : iy > iymax ? iymax : iy; ryb[j] = iy + 1 < iymin ? iymin : iy + 1 > iymax ? iymax : iy + 1; rfy[j] = fy;
    }
    unsigned al8 = (unsigned)(al * 256.f + .5f); if (al8 > 256) al8 = 256;
    for (int j = 0; j < nh; j++) {
      if (rya[j] < 0) continue;
      uint32_t *row = c->surf->px + (size_t)(y0 + j) * c->surf->w + x0;
      const uint32_t *ra = m->px + (size_t)rya[j] * m->w, *rb = m->px + (size_t)ryb[j] * m->w; unsigned fy = (unsigned)rfy[j];
      for (int i = 0; i < nw; i++) {
        if (cxa[i] < 0) continue;
        uint32_t p;
        unsigned fx = (unsigned)cfx[i];
        if (!fx && !fy) p = ra[cxa[i]];
        else {
          uint32_t t0 = ra[cxa[i]], t1 = fx ? ra[cxb[i]] : t0;
          if (fx) t0 = lerp8(t0, t1, fx);
          if (fy) { uint32_t u0 = rb[cxa[i]], u1 = fx ? rb[cxb[i]] : u0; if (fx) u0 = lerp8(u0, u1, fx); t0 = lerp8(t0, u0, fy); }
          p = t0;
        }
        unsigned a = p >> 24;
        if (op == 0 && al8 == 256) {
          if (a == 255) { row[i] = p; continue; }
          if (a == 0) continue;
          uint32_t d = row[i], inv8 = 255 - a;
          uint32_t rb2 = (d & 0x00ff00ffu) * inv8 + 0x00800080u; rb2 = ((rb2 + ((rb2 >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
          uint32_t ag2 = ((d >> 8) & 0x00ff00ffu) * inv8 + 0x00800080u; ag2 = (ag2 + ((ag2 >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
          row[i] = ((p & 0x00ff00ffu) + rb2) | ((p & 0xff00ff00u) + ag2); /* premultiplied: kanalen blijven <= 255 */
          continue;
        }
        if (op == 2) {
          unsigned a8 = (a * al8 + 128) >> 8; if (a8 > 255) a8 = 255; uint32_t d = row[i];
          if (a8 == 255) continue; if (a8 == 0) { row[i] = 0; continue; }
          uint32_t rb2 = (d & 0x00ff00ffu) * a8 + 0x00800080u; rb2 = ((rb2 + ((rb2 >> 8) & 0x00ff00ffu)) >> 8) & 0x00ff00ffu;
          uint32_t ag2 = ((d >> 8) & 0x00ff00ffu) * a8 + 0x00800080u; ag2 = (ag2 + ((ag2 >> 8) & 0x00ff00ffu)) & 0xff00ff00u;
          row[i] = rb2 | ag2; continue;
        }
        if (a == 0) continue;
        unsigned r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255;
        row[i] = blend(row[i], (r * al8 + 128) >> 8, (g * al8 + 128) >> 8, (b * al8 + 128) >> 8, (a * al8 + 128) >> 8, op);
      }
    }
    free(colA); free(rowA);
    c->surf->gen++; return;
  }
  for (int y = y0; y < y1; y++) {
    uint32_t *row = c->surf->px + (size_t)y * c->surf->w;
    for (int x = x0; x < x1; x++) {
      float px = x + .5f, py = y + .5f, ux = inv.a * px + inv.c * py + inv.e, uy = inv.b * px + inv.d * py + inv.f;
      if (ux < rx0 || ux >= rx1 || uy < ry0 || uy >= ry1) continue;
      float fu = (ux - dx) / dw, fv = (uy - dy) / dh, tx = (sx + fu * sw) * inv2, ty = (sy + fv * sh) * inv2;
      float r, g, b, a;
      if (smooth) {
        tx -= .5f; ty -= .5f; int ix = (int)floorf(tx), iy = (int)floorf(ty); float fx = tx - ix, fy = ty - iy;
        int xa = ix < ixmin ? ixmin : ix > ixmax ? ixmax : ix, xb = ix + 1 < ixmin ? ixmin : ix + 1 > ixmax ? ixmax : ix + 1, ya = iy < iymin ? iymin : iy > iymax ? iymax : iy, yb = iy + 1 < iymin ? iymin : iy + 1 > iymax ? iymax : iy + 1;
        uint32_t p00 = m->px[ya * m->w + xa], p10 = m->px[ya * m->w + xb], p01 = m->px[yb * m->w + xa], p11 = m->px[yb * m->w + xb];
        float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
        r = (p00 & 255) * w00 + (p10 & 255) * w10 + (p01 & 255) * w01 + (p11 & 255) * w11;
        g = ((p00 >> 8) & 255) * w00 + ((p10 >> 8) & 255) * w10 + ((p01 >> 8) & 255) * w01 + ((p11 >> 8) & 255) * w11;
        b = ((p00 >> 16) & 255) * w00 + ((p10 >> 16) & 255) * w10 + ((p01 >> 16) & 255) * w01 + ((p11 >> 16) & 255) * w11;
        a = (p00 >> 24) * w00 + (p10 >> 24) * w10 + (p01 >> 24) * w01 + (p11 >> 24) * w11;
      } else {
        int ix = (int)floorf(tx), iy = (int)floorf(ty); ix = ix < ixmin ? ixmin : ix > ixmax ? ixmax : ix; iy = iy < iymin ? iymin : iy > iymax ? iymax : iy;
        uint32_t p = m->px[iy * m->w + ix]; r = p & 255; g = (p >> 8) & 255; b = (p >> 16) & 255; a = p >> 24;
      }
      if (a < 0.5f && op != 2) continue;
      if (al >= .999f && a >= 254.5f && op == 0) { row[x] = (uint32_t)clamp8(r) | ((uint32_t)clamp8(g) << 8) | ((uint32_t)clamp8(b) << 16) | 0xff000000u; continue; }
      row[x] = blend(row[x], (unsigned)(r * al + .5f), (unsigned)(g * al + .5f), (unsigned)(b * al + .5f), (unsigned)(a * al + .5f), op);
    }
  }
  c->surf->gen++;
}

/* ------------------------------------------------------------------ tekst */
static stbtt_fontinfo g_font[3]; static int g_fontok[3];
int font_load(int face, const uint8_t *data) {
  if (face < 0 || face > 2) return 0;
  int off = stbtt_GetFontOffsetForIndex(data, 0); if (off < 0) return 0;
  g_fontok[face] = stbtt_InitFont(&g_font[face], data, off); return g_fontok[face];
}
typedef struct { uint64_t key; unsigned char *bm; int w, h, xo, yo; } Glyph;
#define GCAP 4096
static Glyph g_gl[GCAP]; static int g_gcount = 0;
static Glyph *glyph_get(int face, int cp, float px, int sub) {
  uint64_t key = ((uint64_t)face << 60) | ((uint64_t)(unsigned)cp << 24) | ((uint64_t)((unsigned)(px * 8.f + .5f) & 0xffff) << 4) | (unsigned)sub; key |= 1ull << 63;
  unsigned h = (unsigned)((key * 11400714819323198485ull) >> 52) & (GCAP - 1);
  for (int i = 0; i < GCAP; i++) { Glyph *g = &g_gl[(h + i) & (GCAP - 1)]; if (g->key == key) return g; if (!g->key) break; }
  if (g_gcount > GCAP * 3 / 4) { for (int i = 0; i < GCAP; i++) if (g_gl[i].key) { stbtt_FreeBitmap(g_gl[i].bm, 0); } memset(g_gl, 0, sizeof g_gl); g_gcount = 0; }
  stbtt_fontinfo *f = &g_font[face]; float sc = stbtt_ScaleForMappingEmToPixels(f, px); int gi = stbtt_FindGlyphIndex(f, cp);
  unsigned hh = (unsigned)((key * 11400714819323198485ull) >> 52) & (GCAP - 1);
  Glyph *g; for (int i = 0; ; i++) { g = &g_gl[(hh + i) & (GCAP - 1)]; if (!g->key) break; }
  g->key = key; g->bm = stbtt_GetGlyphBitmapSubpixel(f, sc, sc, sub / 4.f, 0, gi, &g->w, &g->h, &g->xo, &g->yo); g_gcount++;
  return g;
}
static int utf8_next(const unsigned char **sp) {
  const unsigned char *s = *sp; int c = *s++;
  if (c < 0x80) { } else if ((c & 0xe0) == 0xc0 && (s[0] & 0xc0) == 0x80) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
  else if ((c & 0xf0) == 0xe0 && (s[0] & 0xc0) == 0x80) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
  else if ((c & 0xf8) == 0xf0) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
  *sp = s; return c;
}
static int map_cp(stbtt_fontinfo *f, int cp) { if (stbtt_FindGlyphIndex(f, cp)) return cp; if (cp == 0x2192) return '>'; if (cp == 0x00a0) return ' '; return cp; }
float ctx_text_width(Ctx *c, const char *u) {
  if (!g_fontok[c->face]) return 0; stbtt_fontinfo *f = &g_font[c->face]; float sc = stbtt_ScaleForMappingEmToPixels(f, c->fsize), w = 0;
  for (const unsigned char *s = (const unsigned char *)u; *s;) { int cp = map_cp(f, utf8_next(&s)), adv, lsb; stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb); w += adv * sc; }
  return w;
}
void ctx_fill_text(Ctx *c, const char *u, float x, float y) {
  if (!g_fontok[c->face]) return;
  stbtt_fontinfo *f = &g_font[c->face]; float sd = mat_scale(c->m), px = c->fsize * sd; if (px < 1) return;
  float sc = stbtt_ScaleForMappingEmToPixels(f, px), total = 0;
  for (const unsigned char *s = (const unsigned char *)u; *s;) { int cp = map_cp(f, utf8_next(&s)), adv, lsb; stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb); total += adv * sc; }
  float X, Y; dev(c, x, y, &X, &Y); int asc, desc, gap; stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
  float pen = X - (c->talign == 1 ? total / 2 : c->talign == 2 ? total : 0), base = Y;
  if (c->tbase == 1) base += (asc + desc) * 0.5f * sc; else if (c->tbase == 2 || c->tbase == 3) base += asc * sc; else if (c->tbase == 4) base += desc * sc;
  int by = (int)floorf(base + .5f);
  for (const unsigned char *s = (const unsigned char *)u; *s;) {
    int cp = map_cp(f, utf8_next(&s)), adv, lsb; stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb);
    int ipx = (int)floorf(pen); int sub = (int)((pen - ipx) * 4.f); if (sub > 3) sub = 3;
    Glyph *g = glyph_get(c->face, cp, px, sub);
    if (g->bm && g->w > 0 && g->h > 0) {
      if (g->w > g_cov_cap) { g_cov = (float *)realloc(g_cov, g->w * 4); g_cov_cap = g->w; }
      for (int r = 0; r < g->h; r++) { for (int q = 0; q < g->w; q++) g_cov[q] = g->bm[r * g->w + q] / 255.f; paint_row(c, &c->fill, by + g->yo + r, ipx + g->xo, g->w, g_cov); }
    }
    pen += adv * sc;
  }
  c->surf->gen++;
}

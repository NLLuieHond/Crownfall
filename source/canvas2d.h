/* Een kleine software-versie van de HTML canvas 2D tekenlaag.
   Alleen wat Crownfall nodig heeft. Geen afhankelijkheid van JavaScript of van de 3DS. */
#ifndef CANVAS2D_H
#define CANVAS2D_H
#include <stdint.h>

/* Pixels: 0xAABBGGRR (bytes R,G,B,A) met "premultiplied alpha" */
typedef struct Surf {
  int w, h;
  uint32_t *px;
  int gen;                 /* verhoogt bij elke tekenbeurt */
  struct Surf *mip[12];
  int mipgen[12];
} Surf;

Surf *surf_new(int w, int h);
void surf_free(Surf *s);
void surf_resize(Surf *s, int w, int h);
Surf *surf_from_rgba(const uint8_t *rgba, int w, int h);
Surf *surf_mip(Surf *s, int level);

typedef struct { float off; uint8_t r, g, b, a; } Stop;
typedef struct Grad {
  int radial; float x0, y0, r0, x1, y1, r1;
  int n; Stop st[16];
  uint32_t lut[256]; int lutok;
} Grad;
void grad_stop(Grad *g, float off, const char *css);

typedef struct { int kind; uint8_t r, g, b, a; Grad *grad; Surf *pat; } Paint; /* kind 0=kleur 1=verloop 2=patroon */

typedef struct { float a, b, c, d, e, f; } Mat;

typedef struct {
  float *x, *y; int n, cap;
  int *sub; char *cl; int ns, scap;
  int afterClose;
} Path;

typedef struct Ctx {
  Surf *surf;
  Mat m;
  float alpha; int op;           /* op 0=source-over 1=source-atop 2=destination-in */
  Paint fill, stroke;
  float lineWidth;
  float dash[8]; int ndash;
  int face; float fsize; int italic;
  int talign;                    /* 0 links 1 midden 2 rechts */
  int tbase;                     /* 0 alphabetic 1 middle 2 top 3 hanging 4 bottom */
  int smooth;
  Path path;
  struct { Mat m; float alpha; int op; Paint fill, stroke; float lw; float dash[8]; int nd; int face; float fsize; int italic; int ta; int tb; int smooth; } stack[32];
  int sp;
} Ctx;

Ctx *ctx_new(Surf *s);
void ctx_free(Ctx *c);
void ctx_save(Ctx *c); void ctx_restore(Ctx *c);
void ctx_translate(Ctx *c, float x, float y); void ctx_scale(Ctx *c, float x, float y);
int  css_color(const char *s, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
void ctx_set_font(Ctx *c, const char *css);
void ctx_begin(Ctx *c); void ctx_move(Ctx *c, float x, float y); void ctx_line(Ctx *c, float x, float y); void ctx_close(Ctx *c);
void ctx_arc(Ctx *c, float x, float y, float r, float a0, float a1, int ccw);
void ctx_ellipse(Ctx *c, float x, float y, float rx, float ry, float rot, float a0, float a1, int ccw);
void ctx_fill(Ctx *c); void ctx_stroke(Ctx *c);
void ctx_fill_rect(Ctx *c, float x, float y, float w, float h);
void ctx_stroke_rect(Ctx *c, float x, float y, float w, float h);
void ctx_draw_image(Ctx *c, Surf *img, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh);
void ctx_fill_text(Ctx *c, const char *utf8, float x, float y);
float ctx_text_width(Ctx *c, const char *utf8);
void ctx_set_dash(Ctx *c, const float *d, int n);

int font_load(int face, const uint8_t *data); /* 0=semibold 1=bold 2=italic */
#endif

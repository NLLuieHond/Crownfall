/* Crownfall voor de 3DS: draait de JavaScript-game met een eigen canvas-laag. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "quickjs.h"
#include "canvas2d.h"
#include "platform.h"
#include "stb_image.h"

static JSClassID surf_cid, ctx_cid, grad_cid, pat_cid;
static Surf *g_top = 0, *g_bot = 0;
static JSValue g_topv, g_botv;
static int g_ls_top = -1, g_ls_bot = -1;

typedef struct { Surf *s; JSValue owner; } Pat;
typedef struct { Ctx *c; JSValue surfv, fillv, strokev; } JCtx;

static double num(JSContext *ctx, JSValueConst v) { double d = 0; JS_ToFloat64(ctx, &d, v); return d; }

/* ---------- klassen ---------- */
static void surf_fin(JSRuntime *rt, JSValue v) { Surf *s = (Surf *)JS_GetOpaque(v, surf_cid); if (s) surf_free(s); }
static void grad_fin(JSRuntime *rt, JSValue v) { Grad *g = (Grad *)JS_GetOpaque(v, grad_cid); free(g); }
static void pat_fin(JSRuntime *rt, JSValue v) { Pat *p = (Pat *)JS_GetOpaque(v, pat_cid); if (p) { JS_FreeValueRT(rt, p->owner); free(p); } }
static void ctx_fin(JSRuntime *rt, JSValue v) {
  JCtx *j = (JCtx *)JS_GetOpaque(v, ctx_cid); if (!j) return;
  JS_FreeValueRT(rt, j->surfv); JS_FreeValueRT(rt, j->fillv); JS_FreeValueRT(rt, j->strokev); ctx_free(j->c); free(j);
}
static JSClassDef surf_def = { "Surf", .finalizer = surf_fin }, grad_def = { "Gradient", .finalizer = grad_fin }, pat_def = { "Pattern", .finalizer = pat_fin }, ctx_def = { "CanvasRenderingContext2D", .finalizer = ctx_fin };

static Surf *to_surf(JSContext *ctx, JSValueConst v) {
  if (!JS_IsObject(v)) return 0;
  Surf *s = (Surf *)JS_GetOpaque(v, surf_cid); if (s) return s;
  JSValue p = JS_GetPropertyStr(ctx, v, "_s"); s = (Surf *)JS_GetOpaque(p, surf_cid); JS_FreeValue(ctx, p); return s;
}

/* ---------- Surf-functies ---------- */
static JSValue js_surfNew(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  JSValue o = JS_NewObjectClass(ctx, surf_cid); JS_SetOpaque(o, surf_new((int)num(ctx, argv[0]), (int)num(ctx, argv[1]))); return o;
}
static JSValue js_surfResize(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  Surf *s = (Surf *)JS_GetOpaque(argv[0], surf_cid); if (s) surf_resize(s, (int)num(ctx, argv[1]), (int)num(ctx, argv[2])); return JS_UNDEFINED;
}
static JSValue js_surfW(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { Surf *s = (Surf *)JS_GetOpaque(argv[0], surf_cid); return JS_NewInt32(ctx, s ? s->w : 0); }
static JSValue js_surfH(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { Surf *s = (Surf *)JS_GetOpaque(argv[0], surf_cid); return JS_NewInt32(ctx, s ? s->h : 0); }

static int b64v(int c) { if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26; if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+' || c == '-') return 62; if (c == '/' || c == '_') return 63; return -1; }
static JSValue js_decode(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *s = JS_ToCString(ctx, argv[0]); if (!s) return JS_NULL;
  const char *b = strstr(s, "base64,"); b = b ? b + 7 : s; size_t n = strlen(b);
  uint8_t *buf = (uint8_t *)malloc(n * 3 / 4 + 4); size_t o = 0; unsigned acc = 0; int bits = 0;
  for (size_t i = 0; i < n; i++) { int v = b64v((unsigned char)b[i]); if (v < 0) continue; acc = (acc << 6) | v; bits += 6; if (bits >= 8) { bits -= 8; buf[o++] = (acc >> bits) & 255; } }
  JS_FreeCString(ctx, s);
  int w, h, c; uint8_t *px = stbi_load_from_memory(buf, (int)o, &w, &h, &c, 4); free(buf);
  if (!px) return JS_NULL;
  Surf *sf = surf_from_rgba(px, w, h); stbi_image_free(px);
  JSValue ob = JS_NewObjectClass(ctx, surf_cid); JS_SetOpaque(ob, sf); return ob;
}
static JSValue js_loadFont(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *name = JS_ToCString(ctx, argv[1]); size_t len = 0; uint8_t *d = plat_read_asset(name, &len); JS_FreeCString(ctx, name);
  if (!d) return JS_FALSE; return JS_NewBool(ctx, font_load((int)num(ctx, argv[0]), d)); /* data blijft in geheugen */
}
static JSValue js_fileRead(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *name = JS_ToCString(ctx, argv[0]); size_t len = 0; uint8_t *d = plat_read_save(name, &len); JS_FreeCString(ctx, name);
  if (!d) return JS_NULL; JSValue r = JS_NewStringLen(ctx, (const char *)d, len); free(d); return r;
}
static JSValue js_fileWrite(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *name = JS_ToCString(ctx, argv[0]), *data = JS_ToCString(ctx, argv[1]); int ok = plat_write_save(name, data, strlen(data));
  JS_FreeCString(ctx, name); JS_FreeCString(ctx, data); return JS_NewBool(ctx, ok);
}
static JSValue js_fileRemove(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { const char *name = JS_ToCString(ctx, argv[0]); plat_remove_save(name); JS_FreeCString(ctx, name); return JS_UNDEFINED; }
static JSValue js_now(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewFloat64(ctx, plat_now_ms()); }
static JSValue js_log(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { const char *s = JS_ToCString(ctx, argv[0]); if (s) { plat_log(s); JS_FreeCString(ctx, s); } return JS_UNDEFINED; }
static JSValue js_quit(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { plat_quit(); return JS_UNDEFINED; }
static JSValue js_bindScreens(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  g_top = (Surf *)JS_GetOpaque(argv[0], surf_cid); g_bot = (Surf *)JS_GetOpaque(argv[1], surf_cid); g_topv = JS_DupValue(ctx, argv[0]); g_botv = JS_DupValue(ctx, argv[1]); return JS_UNDEFINED;
}

/* ---------- Context2D ---------- */
static JCtx *jc(JSContext *ctx, JSValueConst t) { return (JCtx *)JS_GetOpaque2(ctx, t, ctx_cid); }
#define CTXFN(name) static JSValue name(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv)
#define GET JCtx *j = jc(ctx, t); if (!j) return JS_EXCEPTION; Ctx *c = j->c; (void)c
#define N(i) ((float)num(ctx, argv[i]))
CTXFN(c_save) { GET; ctx_save(c); return JS_UNDEFINED; }
CTXFN(c_restore) { GET; ctx_restore(c); return JS_UNDEFINED; }
CTXFN(c_translate) { GET; ctx_translate(c, N(0), N(1)); return JS_UNDEFINED; }
CTXFN(c_scale) { GET; ctx_scale(c, N(0), N(1)); return JS_UNDEFINED; }
CTXFN(c_beginPath) { GET; ctx_begin(c); return JS_UNDEFINED; }
CTXFN(c_moveTo) { GET; ctx_move(c, N(0), N(1)); return JS_UNDEFINED; }
CTXFN(c_lineTo) { GET; ctx_line(c, N(0), N(1)); return JS_UNDEFINED; }
CTXFN(c_closePath) { GET; ctx_close(c); return JS_UNDEFINED; }
CTXFN(c_arc) { GET; ctx_arc(c, N(0), N(1), N(2), N(3), N(4), argc > 5 && JS_ToBool(ctx, argv[5])); return JS_UNDEFINED; }
CTXFN(c_ellipse) { GET; ctx_ellipse(c, N(0), N(1), N(2), N(3), N(4), N(5), N(6), argc > 7 && JS_ToBool(ctx, argv[7])); return JS_UNDEFINED; }
CTXFN(c_fill) { GET; ctx_fill(c); return JS_UNDEFINED; }
CTXFN(c_stroke) { GET; ctx_stroke(c); return JS_UNDEFINED; }
CTXFN(c_fillRect) { GET; ctx_fill_rect(c, N(0), N(1), N(2), N(3)); return JS_UNDEFINED; }
CTXFN(c_strokeRect) { GET; ctx_stroke_rect(c, N(0), N(1), N(2), N(3)); return JS_UNDEFINED; }
CTXFN(c_clearRect) {
  GET; int op = c->op; Paint f = c->fill; float a = c->alpha; c->op = 2; c->alpha = 1; memset(&c->fill, 0, sizeof c->fill); c->fill.a = 0; /* destination-in met alpha 0 wist */
  ctx_fill_rect(c, N(0), N(1), N(2), N(3)); c->op = op; c->fill = f; c->alpha = a; return JS_UNDEFINED;
}
CTXFN(c_drawImage) {
  GET; Surf *s = to_surf(ctx, argv[0]); if (!s) return JS_UNDEFINED;
  if (argc >= 9) ctx_draw_image(c, s, N(1), N(2), N(3), N(4), N(5), N(6), N(7), N(8));
  else if (argc >= 5) ctx_draw_image(c, s, 0, 0, (float)s->w, (float)s->h, N(1), N(2), N(3), N(4));
  else ctx_draw_image(c, s, 0, 0, (float)s->w, (float)s->h, N(1), N(2), (float)s->w, (float)s->h);
  return JS_UNDEFINED;
}
CTXFN(c_fillText) { GET; const char *s = JS_ToCString(ctx, argv[0]); if (s) { ctx_fill_text(c, s, N(1), N(2)); JS_FreeCString(ctx, s); } return JS_UNDEFINED; }
CTXFN(c_measureText) {
  GET; const char *s = JS_ToCString(ctx, argv[0]); float w = s ? ctx_text_width(c, s) : 0; if (s) JS_FreeCString(ctx, s);
  JSValue o = JS_NewObject(ctx); JS_SetPropertyStr(ctx, o, "width", JS_NewFloat64(ctx, w)); return o;
}
CTXFN(c_setLineDash) {
  GET; float d[8]; int n = 0;
  if (argc > 0 && JS_IsObject(argv[0])) { JSValue lv = JS_GetPropertyStr(ctx, argv[0], "length"); int len = 0; JS_ToInt32(ctx, &len, lv); JS_FreeValue(ctx, lv); for (int i = 0; i < len && n < 8; i++) { JSValue e = JS_GetPropertyUint32(ctx, argv[0], i); d[n++] = (float)num(ctx, e); JS_FreeValue(ctx, e); } }
  ctx_set_dash(c, d, n); return JS_UNDEFINED;
}
CTXFN(c_createLinear) { Grad *g = (Grad *)calloc(1, sizeof(Grad)); g->x0 = N(0); g->y0 = N(1); g->x1 = N(2); g->y1 = N(3); JSValue o = JS_NewObjectClass(ctx, grad_cid); JS_SetOpaque(o, g); return o; }
CTXFN(c_createRadial) { Grad *g = (Grad *)calloc(1, sizeof(Grad)); g->radial = 1; g->x0 = N(0); g->y0 = N(1); g->r0 = N(2); g->x1 = N(3); g->y1 = N(4); g->r1 = N(5); JSValue o = JS_NewObjectClass(ctx, grad_cid); JS_SetOpaque(o, g); return o; }
CTXFN(c_createPattern) {
  Surf *s = to_surf(ctx, argv[0]); if (!s) return JS_NULL;
  Pat *p = (Pat *)calloc(1, sizeof(Pat)); p->s = s; p->owner = JS_DupValue(ctx, JS_IsObject(argv[0]) ? argv[0] : JS_UNDEFINED);
  JSValue o = JS_NewObjectClass(ctx, pat_cid); JS_SetOpaque(o, p); return o;
}
static JSValue g_addStop(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  Grad *g = (Grad *)JS_GetOpaque2(ctx, t, grad_cid); if (!g) return JS_EXCEPTION; const char *c = JS_ToCString(ctx, argv[1]); if (c) { grad_stop(g, (float)num(ctx, argv[0]), c); JS_FreeCString(ctx, c); } return JS_UNDEFINED;
}
static void set_paint(JSContext *ctx, JCtx *j, Paint *p, JSValue *keep, JSValueConst v) {
  if (JS_IsString(v)) {
    const char *s = JS_ToCString(ctx, v); uint8_t r, g, b, a;
    if (s && css_color(s, &r, &g, &b, &a)) { JS_FreeValue(ctx, *keep); *keep = JS_UNDEFINED; memset(p, 0, sizeof *p); p->r = r; p->g = g; p->b = b; p->a = a; }
    if (s) JS_FreeCString(ctx, s); return;
  }
  Grad *g = (Grad *)JS_GetOpaque(v, grad_cid);
  if (g) { JS_FreeValue(ctx, *keep); *keep = JS_DupValue(ctx, v); memset(p, 0, sizeof *p); p->kind = 1; p->grad = g; return; }
  Pat *pt = (Pat *)JS_GetOpaque(v, pat_cid);
  if (pt) { JS_FreeValue(ctx, *keep); *keep = JS_DupValue(ctx, v); memset(p, 0, sizeof *p); p->kind = 2; p->pat = pt->s; }
}
static JSValue p_get(JSContext *ctx, JSValueConst t, int magic) {
  JCtx *j = jc(ctx, t); if (!j) return JS_EXCEPTION; Ctx *c = j->c;
  switch (magic) { case 2: return JS_NewFloat64(ctx, c->lineWidth); case 3: return JS_NewFloat64(ctx, c->alpha); case 8: return JS_NewBool(ctx, c->smooth); }
  return JS_NewString(ctx, "");
}
static JSValue p_set(JSContext *ctx, JSValueConst t, JSValueConst v, int magic) {
  JCtx *j = jc(ctx, t); if (!j) return JS_EXCEPTION; Ctx *c = j->c;
  switch (magic) {
    case 0: set_paint(ctx, j, &c->fill, &j->fillv, v); break;
    case 1: set_paint(ctx, j, &c->stroke, &j->strokev, v); break;
    case 2: c->lineWidth = (float)num(ctx, v); break;
    case 3: { float a = (float)num(ctx, v); if (a >= 0 && a <= 1) c->alpha = a; break; }
    case 4: { const char *s = JS_ToCString(ctx, v); if (s) { c->op = !strcmp(s, "source-atop") ? 1 : !strcmp(s, "destination-in") ? 2 : 0; JS_FreeCString(ctx, s); } break; }
    case 5: { const char *s = JS_ToCString(ctx, v); if (s) { ctx_set_font(c, s); JS_FreeCString(ctx, s); } break; }
    case 6: { const char *s = JS_ToCString(ctx, v); if (s) { c->talign = (!strcmp(s, "center")) ? 1 : (!strcmp(s, "right") || !strcmp(s, "end")) ? 2 : 0; JS_FreeCString(ctx, s); } break; }
    case 7: { const char *s = JS_ToCString(ctx, v); if (s) { c->tbase = !strcmp(s, "middle") ? 1 : !strcmp(s, "top") ? 2 : !strcmp(s, "hanging") ? 3 : !strcmp(s, "bottom") ? 4 : 0; JS_FreeCString(ctx, s); } break; }
    case 8: c->smooth = JS_ToBool(ctx, v); break;
    default: break;
  }
  return JS_UNDEFINED;
}
static JSValue js_newCtx(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  Surf *s = (Surf *)JS_GetOpaque(argv[0], surf_cid); if (!s) return JS_ThrowTypeError(ctx, "geen oppervlak");
  JCtx *j = (JCtx *)calloc(1, sizeof(JCtx)); j->c = ctx_new(s); j->surfv = JS_DupValue(ctx, argv[0]); j->fillv = JS_UNDEFINED; j->strokev = JS_UNDEFINED;
  JSValue proto = JS_GetClassProto(ctx, ctx_cid); JSValue o = JS_NewObjectProtoClass(ctx, proto, ctx_cid); JS_FreeValue(ctx, proto); JS_SetOpaque(o, j); return o;
}

static const JSCFunctionListEntry ctx_funcs[] = {
  JS_CFUNC_DEF("save", 0, c_save), JS_CFUNC_DEF("restore", 0, c_restore), JS_CFUNC_DEF("translate", 2, c_translate), JS_CFUNC_DEF("scale", 2, c_scale),
  JS_CFUNC_DEF("beginPath", 0, c_beginPath), JS_CFUNC_DEF("moveTo", 2, c_moveTo), JS_CFUNC_DEF("lineTo", 2, c_lineTo), JS_CFUNC_DEF("closePath", 0, c_closePath),
  JS_CFUNC_DEF("arc", 5, c_arc), JS_CFUNC_DEF("ellipse", 7, c_ellipse), JS_CFUNC_DEF("fill", 0, c_fill), JS_CFUNC_DEF("stroke", 0, c_stroke),
  JS_CFUNC_DEF("fillRect", 4, c_fillRect), JS_CFUNC_DEF("strokeRect", 4, c_strokeRect), JS_CFUNC_DEF("clearRect", 4, c_clearRect),
  JS_CFUNC_DEF("drawImage", 3, c_drawImage), JS_CFUNC_DEF("fillText", 3, c_fillText), JS_CFUNC_DEF("measureText", 1, c_measureText), JS_CFUNC_DEF("setLineDash", 1, c_setLineDash),
  JS_CFUNC_DEF("createLinearGradient", 4, c_createLinear), JS_CFUNC_DEF("createRadialGradient", 6, c_createRadial), JS_CFUNC_DEF("createPattern", 2, c_createPattern),
  JS_CGETSET_MAGIC_DEF("fillStyle", p_get, p_set, 0), JS_CGETSET_MAGIC_DEF("strokeStyle", p_get, p_set, 1), JS_CGETSET_MAGIC_DEF("lineWidth", p_get, p_set, 2),
  JS_CGETSET_MAGIC_DEF("globalAlpha", p_get, p_set, 3), JS_CGETSET_MAGIC_DEF("globalCompositeOperation", p_get, p_set, 4), JS_CGETSET_MAGIC_DEF("font", p_get, p_set, 5),
  JS_CGETSET_MAGIC_DEF("textAlign", p_get, p_set, 6), JS_CGETSET_MAGIC_DEF("textBaseline", p_get, p_set, 7), JS_CGETSET_MAGIC_DEF("imageSmoothingEnabled", p_get, p_set, 8),
  JS_CGETSET_MAGIC_DEF("imageSmoothingQuality", p_get, p_set, 9),
};
static const JSCFunctionListEntry grad_funcs[] = { JS_CFUNC_DEF("addColorStop", 2, g_addStop) };

/* ---------- invoer ---------- */
static Input g_in;
static JSValue js_poll(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  JSValue a = JS_NewArray(ctx); double v[6] = { g_in.down, g_in.held, g_in.up, g_in.tx, g_in.ty, g_in.touching };
  for (int i = 0; i < 6; i++) JS_SetPropertyUint32(ctx, a, i, JS_NewFloat64(ctx, v[i])); return a;
}

static void report(JSContext *ctx, const char *where) {
  JSValue ex = JS_GetException(ctx); const char *s = JS_ToCString(ctx, ex); char buf[1200];
  JSValue st = JS_GetPropertyStr(ctx, ex, "stack"); const char *ss = JS_IsUndefined(st) ? 0 : JS_ToCString(ctx, st);
  snprintf(buf, sizeof buf, "%s: %s\n%s", where, s ? s : "?", ss ? ss : ""); plat_log(buf);
  if (ss) JS_FreeCString(ctx, ss); JS_FreeValue(ctx, st); if (s) JS_FreeCString(ctx, s); JS_FreeValue(ctx, ex);
}
static int eval_asset(JSContext *ctx, const char *name) {
  size_t len = 0; uint8_t *d = plat_read_asset(name, &len);
  if (!d) { char b[200]; snprintf(b, sizeof b, "Bestand ontbreekt: %s", name); plat_fatal(b); return 0; }
  JSValue r = JS_Eval(ctx, (const char *)d, len, name, JS_EVAL_TYPE_GLOBAL); free(d);
  if (JS_IsException(r)) { char b[300]; snprintf(b, sizeof b, "Fout in %s", name); report(ctx, b);
    JSValue ex = JS_UNDEFINED; (void)ex; plat_fatal("Fout bij het starten van het spel. Zie log.txt op de SD-kaart (3ds/crownfall)."); JS_FreeValue(ctx, r); return 0; }
  JS_FreeValue(ctx, r); return 1;
}
static void call_global(JSContext *ctx, const char *fn, int argc, JSValue *args) {
  JSValue g = JS_GetGlobalObject(ctx), f = JS_GetPropertyStr(ctx, g, fn);
  if (JS_IsFunction(ctx, f)) { JSValue r = JS_Call(ctx, f, g, argc, args); if (JS_IsException(r)) report(ctx, fn); JS_FreeValue(ctx, r); }
  JS_FreeValue(ctx, f); JS_FreeValue(ctx, g);
}

int game_run(void) {
  JSRuntime *rt = JS_NewRuntime(); if (!rt) { plat_fatal("Geen geheugen voor JavaScript."); return 1; }
  JS_SetMaxStackSize(rt, 700 * 1024);
  JSContext *ctx = JS_NewContext(rt);
  JS_NewClassID(&surf_cid); JS_NewClassID(&ctx_cid); JS_NewClassID(&grad_cid); JS_NewClassID(&pat_cid);
  JS_NewClass(rt, surf_cid, &surf_def); JS_NewClass(rt, grad_cid, &grad_def); JS_NewClass(rt, pat_cid, &pat_def); JS_NewClass(rt, ctx_cid, &ctx_def);
  JSValue cp = JS_NewObject(ctx); JS_SetPropertyFunctionList(ctx, cp, ctx_funcs, sizeof(ctx_funcs) / sizeof(ctx_funcs[0])); JS_SetClassProto(ctx, ctx_cid, cp);
  JSValue gp = JS_NewObject(ctx); JS_SetPropertyFunctionList(ctx, gp, grad_funcs, 1); JS_SetClassProto(ctx, grad_cid, gp);
  JSValue G = JS_GetGlobalObject(ctx);
#define REG(n, f, a) JS_SetPropertyStr(ctx, G, n, JS_NewCFunction(ctx, f, n, a))
  REG("__surfNew", js_surfNew, 2); REG("__surfResize", js_surfResize, 3); REG("__surfW", js_surfW, 1); REG("__surfH", js_surfH, 1); REG("__decode", js_decode, 1);
  REG("__loadFont", js_loadFont, 2); REG("__fileRead", js_fileRead, 1); REG("__fileWrite", js_fileWrite, 2); REG("__fileRemove", js_fileRemove, 1);
  REG("__now", js_now, 0); REG("__log", js_log, 1); REG("__quit", js_quit, 0); REG("__bindScreens", js_bindScreens, 2); REG("__newCtx", js_newCtx, 1); REG("__poll", js_poll, 0);
  JS_FreeValue(ctx, G);

  if (!eval_asset(ctx, "prelude.js")) return 1;
  if (!eval_asset(ctx, "game.js")) return 1;
  JSContext *pc;
  while (JS_ExecutePendingJob(rt, &pc) > 0) { }
  if (!g_top || !g_bot) { plat_fatal("Schermen niet gekoppeld."); return 1; }

  int errs = 0;
  while (plat_running()) {
    plat_poll(&g_in);
    JSValue args[6] = { JS_NewFloat64(ctx, g_in.down), JS_NewFloat64(ctx, g_in.held), JS_NewFloat64(ctx, g_in.up), JS_NewFloat64(ctx, g_in.tx), JS_NewFloat64(ctx, g_in.ty), JS_NewInt32(ctx, g_in.touching) };
    call_global(ctx, "__frame", 6, args);
    for (int i = 0; i < 6; i++) JS_FreeValue(ctx, args[i]);
    int guard = 0; int r; while ((r = JS_ExecutePendingJob(rt, &pc)) != 0 && guard++ < 1000) { if (r < 0) { report(pc, "belofte"); if (++errs > 50) break; } }
    int tc = g_top->gen != g_ls_top, bc = g_bot->gen != g_ls_bot;
    if (tc || bc) { plat_present(g_top, g_bot, tc, bc); g_ls_top = g_top->gen; g_ls_bot = g_bot->gen; }
    plat_wait_frame();
  }
  call_global(ctx, "__shutdown", 0, 0);
  return 0;
}

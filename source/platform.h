/* Alles wat per apparaat verschilt: scherm, invoer, tijd en bestanden. */
#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>
#include <stddef.h>
#include "canvas2d.h"

/* toetsbits (zelfde waarden als libctru KEY_*) */
#define K_A 1u
#define K_B 2u
#define K_SELECT 4u
#define K_START 8u
#define K_RIGHT 16u
#define K_LEFT 32u
#define K_UP 64u
#define K_DOWN 128u
#define K_R 256u
#define K_L 512u
#define K_X 1024u
#define K_Y 2048u

typedef struct { uint32_t down, held, up; int tx, ty, touching; } Input;

void plat_init(void);
void plat_shutdown(void);
int plat_running(void);
void plat_poll(Input *in);
void plat_present(Surf *top, Surf *bot, int topChanged, int botChanged);
void plat_wait_frame(void);
double plat_now_ms(void);
uint8_t *plat_read_asset(const char *name, size_t *len);        /* uit romfs, met 0-byte erachter */
uint8_t *plat_read_save(const char *name, size_t *len);         /* uit opslagmap */
int plat_write_save(const char *name, const void *data, size_t len);
void plat_remove_save(const char *name);
void plat_music(int id);                                       /* 0 = spelmuziek, 1 = gevechtsmuziek */
void plat_sfx(int id);                                         /* korte geluidseffecten (romfs/sfx.bin) */
void plat_volume(int kind, int level);                            /* kind 0 = muziek, 1 = effecten; level 0 (uit) t/m 3 (hard) */
void plat_log(const char *msg);
void plat_fatal(const char *msg);
void plat_quit(void);

int game_run(void); /* main.c */
#endif

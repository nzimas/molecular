/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the controls and the screens.
 *
 *   powering on         holding the encoder powers the chip and boots us: POWER ON?  NO / YES (NO first);
 *                       turn: choose; press: do it. NO, or nothing for 10 s: the power is cut again
 *   on the molecule     encoder turn: volume   press: a new molecule   held 0.8 s: the menu
 *                       left button: the molecule rewired (the same atoms, bound anew); held 0.8 s: the LFOs
 *                       (the generator's and the atoms') anew
 *                       right button: the atoms' values anew; held 0.8 s: MOTION on or off (motion.c)
 *                       (the side buttons act at their release, or at 0.8 s held; both held: neither, as both
 *                       held 3 s hand over to update mode)
 *   in the menu         one item at a time, SETTINGS, PATCHES, POWER; turn: the next; press: do it; held: back
 *                       SETTINGS: the patch's own, MOTION RATE (events a minute); turn: the value; press, held
 *                       or a side button: back
 *                       PATCHES: the slots, four a row, numbered; turn: choose; press: the patch there loaded;
 *                       held: the molecule saved there (a filled slot written over); a side button: back
 *                       POWER: POWER OFF?  NO / YES (NO first); turn: choose; press: do it
 *
 * The platform feeds events (app_left .. app_release) and calls app_tick and app_draw from its main loop. app_tick
 * returns 1 when the power is to be cut: the device cuts it; the simulator goes dark (SC_ASLEEP), where holding the
 * encoder asks POWER ON? again, as the device's power button would. */

#define APP_HOLD_MS 800u
#define APP_OVERLAY_MS 1400u
#define APP_OFF_MS 700u          /* the OFF screen before the power goes */
#define APP_WAKE_MS 10000u       /* POWER ON? unanswered this long: off again */

enum { SC_ASLEEP, SC_WAKE, SC_MOL, SC_MENU, SC_SLOTS, SC_CONFIRM, SC_OFF, SC_SETTINGS };
enum { MI_SETTINGS, MI_PATCHES, MI_POWER, MI_N };
enum { OV_NONE, OV_TEXT, OV_VOL };

/* the kinds' colours by family, bright behind black letters: the generator yellow; filters and formants cyan;
 * drive and wave shaping orange; frequency shifting and cross-modulation pink; resonators green; time and texture
 * violet; space (SPRING, DELAY, REVERB) pale blue */
#define C_GEN RGB(255, 230, 60)
#define C_FLT RGB(80, 220, 255)
#define C_DRV RGB(255, 150, 40)
#define C_MOD RGB(255, 100, 210)
#define C_RES RGB(120, 255, 120)
#define C_TIM RGB(180, 160, 255)
#define C_SPC RGB(210, 210, 255)
static const uint16_t AT_COLOUR[AT_N] = {
    C_GEN, C_FLT, C_DRV, C_DRV, C_MOD, C_RES, C_RES, C_FLT, C_TIM, C_TIM, C_SPC,       /* FM FILTER .. SPRING */
    C_TIM, C_TIM, C_DRV, C_MOD, C_DRV, C_DRV, C_RES, C_TIM, C_TIM, C_DRV, C_DRV,       /* GLITCH .. XOR */
    C_DRV, C_DRV, C_MOD, C_MOD, C_SPC, C_SPC,                                          /* DUST .. REVERB */
};

static struct {
    int screen;
    int32_t vol;                 /* 0 .. 100 */
    int down;                    /* the encoder held */
    int held_fired;              /* .. and its long press already acted */
    uint32_t down_at, off_at, wake_at;
    int yes;                     /* POWER ON? / POWER OFF?: 0 NO, 1 YES */
    int cut;                     /* the power to be cut at the next tick */
    int ov;
    const char *ov_text;
    uint32_t ov_until;
    int want_family;             /* show the patch's family once the new molecule is drawn */
    int item;                    /* the menu's: MI_* */
    int32_t slot;                /* PATCHES: the chosen slot */
    uint32_t used;               /* .. and those that hold a patch, a bit each */
    int side_down[2];            /* the left, the right button held */
    int side_fired[2];           /* .. and its long press already acted */
    uint32_t side_at[2];
    char ov_buf[12];
    rnd_t r;
} app;

static void app_overlay(int kind, const char *text, uint32_t now)
{
    app.ov = kind;
    app.ov_text = text;
    app.ov_until = now + APP_OVERLAY_MS;
}
static void app_set_volume(void) { mol_volume(app.vol * app.vol * Q15 / 10000); }   /* (a square: felt evenly) */

static void app_new_molecule(void)
{
    mol_new(rnd_next(&app.r));
    app.want_family = 1;
    app.ov = OV_NONE;
}

static void app_ask_power_on(uint32_t now)
{
    app.screen = SC_WAKE;
    app.yes = 0;
    app.wake_at = now;
}

/* the chip has just been powered by a held encoder: ask before sounding. held: the encoder is still down (the
 * device, booted by the hold; the simulator starts with it up) */
static void app_init(uint32_t entropy, uint32_t now, int held)
{
    memset(&app, 0, sizeof app);
    rnd_seed(&app.r, entropy);
    app.vol = 70;
    app_set_volume();
    mol_off();                                       /* silent until YES */
    motion_stop();
    app_ask_power_on(now);
    app.down = held;                                 /* (the hold that powered us, still going on: its release */
    app.held_fired = held;                           /*  must not answer the question) */
}

/* "WORD 12" into the overlay */
static void app_say_slot(const char *w, uint32_t now)
{
    uint32_t n = (uint32_t)app.slot + 1u, k = 0;
    while (*w)
        app.ov_buf[k++] = *w++;
    if (n >= 10u)
        app.ov_buf[k++] = (char)('0' + n / 10u);
    app.ov_buf[k++] = (char)('0' + n % 10u);
    app.ov_buf[k] = 0;
    app_overlay(OV_TEXT, app.ov_buf, now);
}

/* a side button down (0 left, 1 right): out of the slots or the settings; on the molecule, timed (app_tick) */
static void app_side(int b, uint32_t now)
{
    if (app.screen == SC_SLOTS || app.screen == SC_SETTINGS) {
        app.screen = SC_MOL;
        app.ov = OV_NONE;
        return;
    }
    if (app.screen != SC_MOL)
        return;
    app.side_down[b] = 1;
    app.side_fired[b] = 0;
    app.side_at[b] = now;
    if (app.side_down[b ^ 1])                        /* both: the update mode's gesture, neither acts */
        app.side_fired[0] = app.side_fired[1] = 1;
}
/* .. and up: a short press acts */
static void app_side_up(int b, uint32_t now)
{
    int was = app.side_down[b] && !app.side_fired[b];
    app.side_down[b] = 0;
    if (!was || app.screen != SC_MOL)
        return;
    if (b == 0)
        app_overlay(OV_TEXT, mol_rewire(rnd_next(&app.r), mot.on) ? "REWIRE" : "NO REWIRE", now);
    else {
        mol_shake_fx(rnd_next(&app.r));
        app_overlay(OV_TEXT, "FX", now);
    }
}
/* .. held 0.8 s */
static void app_side_held(int b, uint32_t now)
{
    if (b == 0) {
        mol_shake_lfos(rnd_next(&app.r));
        app_overlay(OV_TEXT, "LFO", now);
    } else if (mot.on) {
        motion_stop();
        app_overlay(OV_TEXT, "STILL", now);
    } else {
        motion_start(rnd_next(&app.r), now);
        app_overlay(OV_TEXT, "MOTION", now);
    }
}
static void app_left(uint32_t now) { app_side(0, now); }
static void app_right(uint32_t now) { app_side(1, now); }
static void app_left_up(uint32_t now) { app_side_up(0, now); }
static void app_right_up(uint32_t now) { app_side_up(1, now); }
static void app_turn(int32_t d, uint32_t now)
{
    if (app.screen == SC_MOL) {
        app.vol = clampi(app.vol + 2 * d, 0, 100);
        app_set_volume();
        app_overlay(OV_VOL, 0, now);
    } else if (app.screen == SC_MENU)
        app.item = clampi(app.item + d, 0, MI_N - 1);
    else if (app.screen == SC_SLOTS)
        app.slot = clampi(app.slot + d, 0, (int32_t)PATCH_SLOTS - 1);
    else if (app.screen == SC_SETTINGS)
        motion_rate_step(d);
    else if ((app.screen == SC_CONFIRM || app.screen == SC_WAKE) && d) {
        app.yes = d > 0;
        app.wake_at = now;                           /* (a choice being made: no timeout yet) */
    }
}
static void app_press(uint32_t now)
{
    app.down = 1;
    app.held_fired = 0;
    app.down_at = now;
}
static void app_release(uint32_t now)
{
    int was = app.down && !app.held_fired;
    app.down = 0;
    if (!was)
        return;                                      /* a long press acted already */
    switch (app.screen) {
    case SC_WAKE:
        if (app.yes) {
            mol_wake();
            app_new_molecule();
            app.screen = SC_MOL;
        } else
            app.cut = 1;
        break;
    case SC_MOL:
        app_new_molecule();
        break;
    case SC_MENU:
        if (app.item == MI_SETTINGS)
            app.screen = SC_SETTINGS;
        else if (app.item == MI_PATCHES) {
            app.used = patch_used_mask();
            app.slot = 0;
            app.screen = SC_SLOTS;
        } else {
            app.screen = SC_CONFIRM;
            app.yes = 0;
        }
        break;
    case SC_SETTINGS:
        app.screen = SC_MOL;
        break;
    case SC_SLOTS:                                   /* a short press: the patch there, or EMPTY (stay) */
        if (patch_load((uint32_t)app.slot)) {
            app.screen = SC_MOL;
            app_say_slot("LOADED ", now);
        } else
            app_say_slot("EMPTY ", now);
        break;
    case SC_CONFIRM:
        if (app.yes) {
            app.screen = SC_OFF;
            app.off_at = now;
            mol_off();
        } else
            app.screen = SC_MOL;
        break;
    }
}

/* 1 when the power is to be cut (and the screen goes dark) */
static int app_tick(uint32_t now)
{
    int silent;
    if (app.down && !app.held_fired && now - app.down_at >= APP_HOLD_MS) {
        app.held_fired = 1;
        if (app.screen == SC_ASLEEP)
            app_ask_power_on(now);
        else if (app.screen == SC_MOL) {
            app.screen = SC_MENU;
            app.item = MI_SETTINGS;
            app.ov = OV_NONE;
        } else if (app.screen == SC_SLOTS) {         /* held on a slot: the molecule saved there */
            app_say_slot(patch_save((uint32_t)app.slot) ? "SAVED " : "FAILED ", now);
            app.screen = SC_MOL;
        } else if (app.screen == SC_MENU || app.screen == SC_CONFIRM || app.screen == SC_SETTINGS)
            app.screen = SC_MOL;
    }
    {
        int b;
        for (b = 0; b < 2; b++)
            if (app.side_down[b] && !app.side_fired[b] && now - app.side_at[b] >= APP_HOLD_MS) {
                app.side_fired[b] = 1;
                if (app.screen == SC_MOL)
                    app_side_held(b, now);
            }
    }
    if (app.screen == SC_WAKE && !app.down && now - app.wake_at >= APP_WAKE_MS)
        app.cut = 1;
    if (app.want_family && !mol_pending) {
        app.want_family = 0;
        app_overlay(OV_TEXT, fam_name(mol_d.gen.patch.family), now);
    }
    if (app.ov && (int32_t)(now - app.ov_until) >= 0)
        app.ov = OV_NONE;
    silent = mol_poll();
    if (app.screen != SC_OFF)
        motion_tick(now);
    /* 0.56: never silent (the audio stopped: no fade ends) still powers off, 2 s later */
    if (app.screen == SC_OFF && (silent ? now - app.off_at >= APP_OFF_MS : now - app.off_at >= APP_OFF_MS + 2000u))
        app.cut = 1;
    if (!app.cut)
        return 0;
    app.cut = 0;
    app.screen = SC_ASLEEP;
    app.ov = OV_NONE;
    mol_off();                                       /* (already silent, or never sounded) */
    motion_stop();
    return 1;
}

/* ------------------------------------------------------------ screens --- */
static void draw_molecule(void)
{
    const mol_shape_t *s = &mol_d.shape;
    uint32_t k, t = mol_meter.blocks;
    gfx_clear(C_BLACK);
    for (k = 1; k < s->n; k++) {                                       /* the bonds */
        const mol_node_t *a = &s->node[k], *p = &s->node[a->parent];
        gfx_line(p->x, p->y, a->x, a->y, 3, C_WHITE);
    }
    for (k = 1; k < s->n; k++) {                                       /* the signal travelling along them */
        const mol_node_t *a = &s->node[k], *p = &s->node[a->parent];
        int32_t lv = mol_meter.level[a->parent], j, pr = 2 + clampi(lv * 4 / Q15, 0, 2);   /* bigger when louder */
        if (lv < 300)
            continue;
        for (j = 0; j < 2; j++) {
            int32_t f = (int32_t)(((t * 3u) + k * 97u + (uint32_t)j * 512u) & 1023u);   /* 0 .. 1023 along the bond */
            gfx_disc(p->x + (a->x - p->x) * f / 1024, p->y + (a->y - p->y) * f / 1024, pr, AT_COLOUR[p->kind]);
        }
    }
    for (k = 0; k < s->n; k++) {                                       /* the atoms; a ring round each that sounds */
        const mol_node_t *a = &s->node[k];
        int32_t z = motion_size(k), r = (node_r(a) * z) >> 8;           /* (MOTION: swelling in, shrinking away) */
        if (r < 2)
            continue;
        if (mol_meter.level[k] > 1600 && z == 256)
            gfx_ring(a->x, a->y, r + 2, r + 3, 0, AT_COLOUR[a->kind]);
        gfx_disc(a->x, a->y, r, AT_COLOUR[a->kind]);
        if (z >= 200)
            gfx_text_c(a->x, a->y - 7, AT_SYM[a->kind], 2, C_BLACK);
    }
}

/* a box across the middle of the molecule: the volume, or a word */
#define OV_X 8
#define OV_Y 28
#define OV_W (SCR_W - 16)
#define OV_H 72
static void draw_overlay(void)
{
    char num[4];
    gfx_rect(OV_X, OV_Y, OV_W, OV_H, C_BLACK);
    gfx_frame(OV_X, OV_Y, OV_W, OV_H, 3, C_WHITE);
    if (app.ov == OV_VOL) {
        int32_t v = app.vol, bw = OV_W - 32;
        num[0] = v >= 100 ? '1' : ' ';
        num[1] = v >= 10 ? (char)('0' + (v / 10) % 10) : ' ';
        num[2] = (char)('0' + v % 10);
        num[3] = 0;
        gfx_text_c(SCR_W / 2, OV_Y + 8, num[0] == ' ' ? (num[1] == ' ' ? num + 2 : num + 1) : num, 4, C_WHITE);
        gfx_frame(OV_X + 16, OV_Y + OV_H - 24, bw, 14, 2, C_WHITE);
        gfx_rect(OV_X + 18, OV_Y + OV_H - 22, (bw - 4) * v / 100, 10, C_WHITE);
    } else {
        int32_t sc = gfx_fit(app.ov_text, OV_W - 12, 4);
        gfx_text_c(SCR_W / 2, OV_Y + OV_H / 2 - 7 * sc / 2, app.ov_text, sc, C_WHITE);
    }
}

/* the menu: one item, its colour round the screen, its picture and its word; the dots below: which of the three */
static void draw_menu(void)
{
    static const char *const WORD[MI_N] = {"SETTINGS", "PATCHES", "POWER"};
    static const uint16_t COL[MI_N] = {C_MOD, C_RES, C_GEN};
    int32_t i, cx = SCR_W / 2;
    gfx_clear(C_BLACK);
    gfx_frame(6, 4, SCR_W - 12, SCR_H - 8, 4, COL[app.item]);
    if (app.item == MI_SETTINGS) {                   /* three sliders, their knobs at different places */
        static const int8_t KNOB[3] = {-14, 10, -2};
        for (i = 0; i < 3; i++) {
            gfx_rect(cx - 30, 22 + i * 16, 60, 4, C_WHITE);
            gfx_disc(cx + KNOB[i], 24 + i * 16, 7, C_WHITE);
        }
    } else if (app.item == MI_PATCHES) {             /* four slots, one filled */
        gfx_rect(cx - 26, 20, 22, 18, C_WHITE);
        gfx_frame(cx + 4, 20, 22, 18, 3, C_WHITE);
        gfx_frame(cx - 26, 42, 22, 18, 3, C_WHITE);
        gfx_frame(cx + 4, 42, 22, 18, 3, C_WHITE);
    } else
        gfx_power(cx, 40, 20, C_WHITE);
    gfx_text_c(cx, 70, WORD[app.item], gfx_fit(WORD[app.item], SCR_W - 28, 3), C_WHITE);
    for (i = 0; i < MI_N; i++) {
        int32_t x = cx + (i - 1) * 22;
        if (i == app.item)
            gfx_disc(x, 107, 6, C_WHITE);
        else
            gfx_ring(x, 107, 4, 6, 0, C_WHITE);
    }
}

/* PATCHES: its word above, the slots four a row, numbered: an empty one outlined, a filled one solid white; the chosen
 * one ringed in yellow */
#define SL_SQ 32
#define SL_GX 6
#define SL_GY 4
#define SL_X ((SCR_W - 4 * SL_SQ - 3 * SL_GX) / 2)
#define SL_Y 20
static void draw_slots(void)
{
    uint32_t i;
    gfx_clear(C_BLACK);
    gfx_text_c(SCR_W / 2, 2, "PATCHES", 2, C_WHITE);
    for (i = 0; i < PATCH_SLOTS; i++) {
        int32_t x = SL_X + (int32_t)(i % 4u) * (SL_SQ + SL_GX), y = SL_Y + (int32_t)(i / 4u) * (SL_SQ + SL_GY);
        int used = (app.used >> i) & 1u;
        char num[3];
        num[0] = i + 1u >= 10u ? '1' : (char)('1' + i);
        num[1] = i + 1u >= 10u ? (char)('0' + (i + 1u) % 10u) : 0;
        num[2] = 0;
        if ((int32_t)i == app.slot)
            gfx_frame(x - 3, y - 3, SL_SQ + 6, SL_SQ + 6, 3, C_GEN);
        if (used)
            gfx_rect(x, y, SL_SQ, SL_SQ, C_WHITE);
        else
            gfx_frame(x, y, SL_SQ, SL_SQ, 2, C_WHITE);
        gfx_text_c(x + SL_SQ / 2, y + (SL_SQ - 14) / 2, num, 2, used ? C_BLACK : C_WHITE);
    }
}

/* SETTINGS: the patch's MOTION RATE, its events a minute large; the turn's way shown by the arrows either side */
static void draw_settings(void)
{
    char num[4];
    uint32_t v = motion_rate(), k = 0, i = motion_rate_index();
    gfx_clear(C_BLACK);
    gfx_frame(6, 4, SCR_W - 12, SCR_H - 8, 4, C_MOD);
    gfx_text_c(SCR_W / 2, 12, "MOTION RATE", gfx_fit("MOTION RATE", SCR_W - 28, 2), C_WHITE);
    if (v >= 100u)
        num[k++] = (char)('0' + v / 100u);
    if (v >= 10u)
        num[k++] = (char)('0' + (v / 10u) % 10u);
    num[k++] = (char)('0' + v % 10u);
    num[k] = 0;
    {
        int32_t sc = gfx_fit(num, SCR_W - 76, 6);    /* (clear of the arrows) */
        gfx_text_c(SCR_W / 2, 59 - 7 * sc / 2, num, sc, C_WHITE);
    }
    if (i > 0)                                       /* a lower rate to turn to: an arrow left */
        for (k = 0; k < 12u; k++)
            gfx_rect(20 + (int32_t)k, 59 - (int32_t)k, 2, 2 * (int32_t)k + 2, C_WHITE);
    if (i + 1u < MOTION_NRATES)
        for (k = 0; k < 12u; k++)
            gfx_rect(SCR_W - 22 - (int32_t)k, 59 - (int32_t)k, 2, 2 * (int32_t)k + 2, C_WHITE);
    gfx_text_c(SCR_W / 2, 94, "A MINUTE", 2, C_WHITE);
}

/* a choice: the chosen one filled, the other outlined; both white, so both read */
#define CH_W 68
#define CH_H 42
#define CH_Y (SCR_H - CH_H - 6)
static void draw_choice(int32_t x, const char *s, int on)
{
    if (on) {
        gfx_rect(x, CH_Y, CH_W, CH_H, C_WHITE);
        gfx_text_c(x + CH_W / 2, CH_Y + 11, s, 3, C_BLACK);
    } else {
        gfx_frame(x, CH_Y, CH_W, CH_H, 2, C_WHITE);
        gfx_text_c(x + CH_W / 2, CH_Y + 11, s, 3, C_WHITE);
    }
}
/* POWER ON? or POWER OFF?, NO and YES */
static void draw_question(const char *word)
{
    gfx_clear(C_BLACK);
    gfx_text_c(SCR_W / 2, 6, "POWER", 3, C_WHITE);
    gfx_text_c(SCR_W / 2, 34, word, 4, C_WHITE);
    draw_choice(SCR_W / 2 - CH_W - 6, "NO", !app.yes);
    draw_choice(SCR_W / 2 + 6, "YES", app.yes);
}

static void app_draw(uint32_t now)
{
    (void)now;
    switch (app.screen) {
    case SC_WAKE:
        draw_question("ON?");
        break;
    case SC_MOL:
        draw_molecule();
        if (app.ov)
            draw_overlay();
        break;
    case SC_MENU:
        draw_menu();
        break;
    case SC_SLOTS:
        draw_slots();
        if (app.ov)
            draw_overlay();                          /* (EMPTY 3) */
        break;
    case SC_SETTINGS:
        draw_settings();
        break;
    case SC_CONFIRM:
        draw_question("OFF?");
        break;
    case SC_OFF:
        gfx_clear(C_BLACK);
        gfx_text_c(SCR_W / 2, SCR_H / 2 - 17, "OFF", 5, C_WHITE);
        break;
    default:                                         /* asleep: the screen dark, as the device's when off */
        gfx_clear(C_BLACK);
    }
}

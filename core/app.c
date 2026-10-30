/* SPDX-License-Identifier: GPL-3.0-only
 * MOLECULAR: the controls and the screens.
 *
 *   powering on         holding the encoder powers the chip and boots us: POWER ON?  NO / YES (NO first);
 *                       turn: choose; press: do it. NO, or nothing for 10 s: the power is cut again
 *   on the molecule     encoder turn: volume   press: a new molecule   held 0.8 s: the menu
 *                       left button: the generator's LFOs anew          right button: the atoms' values anew
 *   in the menu         one tile, POWER; press: the confirmation; held: back to the molecule
 *   powering off        POWER OFF?  NO / YES (NO first); turn: choose; press: do it
 *
 * The platform feeds events (app_left .. app_release) and calls app_tick and app_draw from its main loop. app_tick
 * returns 1 when the power is to be cut: the device cuts it; the simulator goes dark (SC_ASLEEP), where holding the
 * encoder asks POWER ON? again, as the device's power button would. */

#define APP_HOLD_MS 800u
#define APP_OVERLAY_MS 1400u
#define APP_OFF_MS 700u          /* the OFF screen before the power goes */
#define APP_WAKE_MS 10000u       /* POWER ON? unanswered this long: off again */

enum { SC_ASLEEP, SC_WAKE, SC_MOL, SC_MENU, SC_CONFIRM, SC_OFF };
enum { OV_NONE, OV_TEXT, OV_VOL };

static const uint16_t AT_COLOUR[AT_N] = {
    RGB(255, 230, 60), RGB(80, 220, 255), RGB(255, 150, 40), RGB(255, 100, 210),
    RGB(120, 255, 120), RGB(180, 160, 255), RGB(255, 120, 120), RGB(210, 210, 255),
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
    app_ask_power_on(now);
    app.down = held;                                 /* (the hold that powered us, still going on: its release */
    app.held_fired = held;                           /*  must not answer the question) */
}

static void app_left(uint32_t now)
{
    if (app.screen != SC_MOL)
        return;
    mol_shake_gen(rnd_next(&app.r));
    app_overlay(OV_TEXT, "LFO", now);
}
static void app_right(uint32_t now)
{
    if (app.screen != SC_MOL)
        return;
    mol_shake_fx(rnd_next(&app.r));
    app_overlay(OV_TEXT, "FX", now);
}
static void app_turn(int32_t d, uint32_t now)
{
    if (app.screen == SC_MOL) {
        app.vol = clampi(app.vol + 2 * d, 0, 100);
        app_set_volume();
        app_overlay(OV_VOL, 0, now);
    } else if ((app.screen == SC_CONFIRM || app.screen == SC_WAKE) && d) {
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
        app.screen = SC_CONFIRM;
        app.yes = 0;
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
            app.ov = OV_NONE;
        } else if (app.screen == SC_MENU || app.screen == SC_CONFIRM)
            app.screen = SC_MOL;
    }
    if (app.screen == SC_WAKE && !app.down && now - app.wake_at >= APP_WAKE_MS)
        app.cut = 1;
    if (app.want_family && !mol_pending) {
        app.want_family = 0;
        app_overlay(OV_TEXT, FF_NAME[mol_d.gen.patch.family < FF_N ? mol_d.gen.patch.family : 0], now);
    }
    if (app.ov && (int32_t)(now - app.ov_until) >= 0)
        app.ov = OV_NONE;
    silent = mol_poll();
    if (app.screen == SC_OFF && silent && now - app.off_at >= APP_OFF_MS)
        app.cut = 1;
    if (!app.cut)
        return 0;
    app.cut = 0;
    app.screen = SC_ASLEEP;
    app.ov = OV_NONE;
    mol_off();                                       /* (already silent, or never sounded) */
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
        int32_t r = node_r(a);
        if (mol_meter.level[k] > 1600)
            gfx_ring(a->x, a->y, r + 2, r + 3, 0, AT_COLOUR[a->kind]);
        gfx_disc(a->x, a->y, r, AT_COLOUR[a->kind]);
        gfx_text_c(a->x, a->y - 7, AT_SYM[a->kind], 2, C_BLACK);
    }
}

static void draw_overlay(void)
{
    char num[4];
    gfx_rect(6, 48, SCR_W - 12, 64, C_BLACK);
    gfx_frame(6, 48, SCR_W - 12, 64, 3, C_WHITE);
    if (app.ov == OV_VOL) {
        int32_t v = app.vol;
        num[0] = v >= 100 ? '1' : ' ';
        num[1] = v >= 10 ? (char)('0' + (v / 10) % 10) : ' ';
        num[2] = (char)('0' + v % 10);
        num[3] = 0;
        gfx_text_c(SCR_W / 2, 56, num[0] == ' ' ? (num[1] == ' ' ? num + 2 : num + 1) : num, 4, C_WHITE);
        gfx_frame(14, 92, 100, 12, 2, C_WHITE);
        gfx_rect(16, 94, 96 * v / 100, 8, C_WHITE);
    } else {
        int32_t sc = gfx_fit(app.ov_text, SCR_W - 24, 4);
        gfx_text_c(SCR_W / 2, 80 - 7 * sc / 2, app.ov_text, sc, C_WHITE);
    }
}

static void draw_menu(void)
{
    gfx_clear(C_BLACK);
    gfx_frame(12, 22, SCR_W - 24, 116, 4, AT_COLOUR[AT_GEN]);
    gfx_power(SCR_W / 2, 66, 22, C_WHITE);
    gfx_text_c(SCR_W / 2, 104, "POWER", 3, C_WHITE);
}

/* a choice: the chosen one filled, the other outlined; both white, so both read */
static void draw_choice(int32_t x, const char *s, int on)
{
    if (on) {
        gfx_rect(x, 100, 58, 44, C_WHITE);
        gfx_text_c(x + 29, 112, s, 3, C_BLACK);
    } else {
        gfx_frame(x, 100, 58, 44, 2, C_WHITE);
        gfx_text_c(x + 29, 112, s, 3, C_WHITE);
    }
}
/* POWER ON? or POWER OFF?, NO and YES */
static void draw_question(const char *word)
{
    gfx_clear(C_BLACK);
    gfx_text_c(SCR_W / 2, 16, "POWER", 3, C_WHITE);
    gfx_text_c(SCR_W / 2, 48, word, 4, C_WHITE);
    draw_choice(3, "NO", !app.yes);
    draw_choice(67, "YES", app.yes);
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
    case SC_CONFIRM:
        draw_question("OFF?");
        break;
    case SC_OFF:
        gfx_clear(C_BLACK);
        gfx_text_c(SCR_W / 2, 62, "OFF", 5, C_WHITE);
        break;
    default:                                         /* asleep: the screen dark, as the device's when off */
        gfx_clear(C_BLACK);
    }
}

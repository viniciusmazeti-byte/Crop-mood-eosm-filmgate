/* Tests for the Bolex 1620p: its wandering speed, its 133 degree shutter, the cuts, the intermittency - run on a computer, see tests/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#define ISO_100 72
#define ISO_200 80
#define ISO_400 88
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))
#define COERCE(x,lo,hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#include "bolex_snippets.h"   /* the real code, copied out of crop_rec.c by tests/run.sh */

/* mlv_lite's height rule, as recorded with lossless compression */
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define OUTPUT_COMPRESSION 1
#define BPP 14
#define MOD(a,b) ((((a) % (b)) + (b)) % (b))
#include "bolex_mlv_snippets.h"      /* BOLEX_RES_X and calc_res_y, copied out of mlv_lite.c */

/* the recorded size for a reading, and how 4:3 it is once stretched */
static void check_43(const char * name, int max_res_y, int skip_y)
{
    /* lines 1 of (1+skip), columns binned 3; skip_y 0 = the 1x1 readout (no binning) */
    float squeeze = skip_y ? (1 + skip_y) / 3.0f : 1.0f;
    int w = BOLEX_RES_X;
    int h = calc_res_y(w, max_res_y, 4, 3, squeeze);
    float aspect = w / (h * squeeze);
    CHECK(h <= max_res_y && (h & 1) == 0, "%s: height %d (max %d)", name, h, max_res_y);
    CHECK(fabs(aspect - 4.0 / 3.0) < 0.01, "%s: %dx%d stretched %.3f is %.3f:1, not 4:3", name, w, h, squeeze, aspect);
    printf("%s: %dx%d, stretch %.3f -> %.3f:1\n", name, w, h, squeeze, aspect);
}

int main(void)
{
    const int want = BOLEX_SHUTTER_X1000;

    /* Bolex: 1620p / 2.8K at 20..25 fps, 976p at 42..46.8, all played at 24, 133 degrees */
    CHECK(BOLEX_FPS_X1000 == 24000, "Bolex camera speed %d", BOLEX_FPS_X1000);
    CHECK(BOLEX_ANGLE == 133, "Bolex shutter angle %d", BOLEX_ANGLE);
    CHECK(want > 64000 && want < 66000, "133 degrees at 24 fps must be ~1/65 s, got 1/%d.%03d", want / 1000, want % 1000);

    /* the three modes: dannephoto's timers; idle and fastest speeds */
    {
        const struct bolex_mode * m = bolex_modes;
        CHECK(m[BOLEX_MODE_1620].timer_a == 0x279 && m[BOLEX_MODE_166].timer_a == 0x325 && m[BOLEX_MODE_185].timer_a == 0x325 && m[BOLEX_MODE_976].timer_a == 0x20F, "timer A");
        for (int i = 0; i < BOLEX_MODES; i++)
        {
            if (m[i].sena)
                continue;
            double idle = 32e6 / ((m[i].timer_a + 1.0) * (m[i].timer_b_idle + 1.0));
            double top  = 32e6 / ((m[i].timer_a + 1.0) * (m[i].timer_b_min + 1.0));
            printf("mode %s: idle %.3f fps, fastest %.3f, wanders %d..%d\n", m[i].name, idle, top, m[i].fps_min, m[i].fps_max);
            CHECK(fabs(top * 1000 - m[i].fps_max) < 30, "%s: fastest %.3f vs %d", m[i].name, top, m[i].fps_max);
            CHECK(bolex_timer_b_for(&m[i], m[i].fps_max) == m[i].timer_b_min, "%s: B at max", m[i].name);
            CHECK(bolex_timer_b_for(&m[i], m[i].fps_max + 5000) == m[i].timer_b_min, "%s: never faster", m[i].name);
        }
        CHECK(bolex_timer_b_for(&m[BOLEX_MODE_1620], 23976) == 0x838, "1620p 23.976");
        CHECK(m[BOLEX_MODE_976].fps_min == 42000 && m[BOLEX_MODE_976].fps_max == 46800, "976p range");
    }

    /* the speed: wandering within each mode's range all through a take, smooth, using the
     * range; the file plays at 24 */
    CHECK(BOLEX_PLAY_FPS == 24, "projection speed %d", BOLEX_PLAY_FPS);
    for (int mi = 0; mi < BOLEX_MODES; mi++)
    {
        const struct bolex_mode * m = &bolex_modes[mi];
        if (m->sena)
            continue;
        int span = m->fps_max - m->fps_min;
        struct bolex_wander w;
        int lo = 999999, hi = 0, maxstep = 0, low = 0, high = 0, n = 0;
        for (int take = 0; take < 50; take++)
        {
            int start = (m->fps_min + m->fps_max) / 2;
            bolex_wander_start(&w, 1234567u * (take + 1), start);
            int prev = start;
            for (int t = 0; t < 60000; t += 40, n++)       /* a minute, the task's 40 ms */
            {
                int f = bolex_wander_step(&w, 40, m->fps_min, m->fps_max);
                CHECK(f >= m->fps_min && f <= m->fps_max, "%s speed %d", m->name, f);
                maxstep = MAX(maxstep, ABS(f - prev));
                prev = f;
                lo = f < lo ? f : lo; hi = f > hi ? f : hi;
                low += f < m->fps_min + span / 5; high += f > m->fps_max - span / 5;
                int b = bolex_timer_b_for(m, f);
                CHECK(b >= m->timer_b_min && b <= bolex_timer_b_for(m, m->fps_min), "%s timer B %x", m->name, b);
            }
        }
        printf("%s speed: %d..%d fps x1000, largest change per 40 ms %d, %d%% in the low fifth, %d%% in the high\n",
               m->name, lo, hi, maxstep, low * 100 / n, high * 100 / n);
        CHECK(lo < m->fps_min + span / 5 && hi > m->fps_max - span / 10, "%s range used %d..%d", m->name, lo, hi);
        CHECK(maxstep <= 150, "%s: a jump of %d", m->name, maxstep);
        CHECK(low * 100 / n >= 5 && high * 100 / n >= 5, "%s: range rarely reached", m->name);
    }

    /* the shutter: 133 degrees at every speed of the range (the blanking for the Timer B) */
    for (int mi = 0; mi < BOLEX_MODES; mi++)
        for (int f = bolex_modes[mi].fps_min; f <= bolex_modes[mi].fps_max; f += 100)
        {
            int tb = bolex_timer_b_for(&bolex_modes[mi], f) + 1;
            double angle = 360.0 * (tb - bolex_blanking(tb, bolex_modes[mi].angle, 1000)) / tb;
            CHECK(fabs(angle - bolex_modes[mi].angle) < 1.5, "%s %d fps x1000: %.2f degrees", bolex_modes[mi].name, f, angle);
        }

    /* 2^ev */
    for (int ev = -1000; ev <= 1000; ev += 10)
    {
        double want = 1000 * pow(2.0, ev / 1000.0);
        CHECK(fabs(bolex_pow2_x1000(ev) - want) < 1.5, "2^%d: %d, want %.1f", ev, bolex_pow2_x1000(ev), want);
    }

    /* the flicker in the camera: within +-0.059 stop, eased over 3 frames, breaths of 0.023..0.044 stop every
     * 1..3 s; the angle it gives stays inside the frame */
    {
        struct bolex_flicker fl;
        bolex_flicker_start(&fl, 99);
        int lo = 0, hi = 0, breaths = 0, was = 0, n = 0, prev = 0, big_steps = 0;
        int alo = 999, ahi = 0;
        long sum = 0;
        for (int t = 0; t < 600000; t += 42, n++)          /* 10 minutes at ~24 fps */
        {
            int ev = bolex_flicker_step(&fl, 42);
            lo = ev < lo ? ev : lo; hi = ev > hi ? ev : hi;
            sum += ev;
            int in = fl.t - fl.breath_at < fl.breath_len;
            breaths += in && !was; was = in;
            big_steps += n && ABS(ev - prev) > 40; prev = ev;
            CHECK(ABS(ev) <= BOLEX_FLICKER_X1000 + BOLEX_BREATH_MAX_X1000, "ev %d", ev);
            int tb = bolex_timer_b_for(&bolex_modes[BOLEX_MODE_976], 46800) + 1;   /* the shortest frame */
            int ang = 360 * (tb - bolex_blanking(tb, BOLEX_ANGLE, bolex_pow2_x1000(ev))) / tb;
            alo = ang < alo ? ang : alo; ahi = ang > ahi ? ang : ahi;
            CHECK(ang > 60 && ang < 360, "angle %d", ang);
        }
        printf("flicker: %d..%d mEV, mean %ld, %d breaths in 10 min (one per %.1f s), angles %d..%d, %d%% frames jump > 0.04 EV\n",
               lo, hi, sum / n, breaths, 600.0 / MAX(breaths, 1), alo, ahi, big_steps * 100 / n);
        CHECK(lo < -BOLEX_FLICKER_X1000 - BOLEX_BREATH_MIN_X1000 && hi > BOLEX_FLICKER_X1000 + BOLEX_BREATH_MIN_X1000, "breaths not reaching %d..%d", lo, hi);
        CHECK(breaths > 600 / 9 && breaths < 600 / 3, "breaths %d", breaths);
        CHECK(ABS(sum / n) < 10, "flicker not centred: %ld", sum / n);
    }
    printf("shutter: 20 fps 1/%.0f s, 25 fps 1/%.0f s, 46.8 fps 1/%.0f s (all %d degrees)\n",
           360.0 * 20 / BOLEX_ANGLE, 360.0 * 25 / BOLEX_ANGLE, 360.0 * 46.8 / BOLEX_ANGLE, BOLEX_ANGLE);

    /* the Sena: on the 2.8K line time, 4 / 6 / 8 / 12 fps (Timer B fits the register), 360 degrees,
     * played at 12; the next speed never the same, all used; ~2 in 5 frames dropped, at most 4 in
     * a row; colour temperatures 2000..6000 K */
    {
        const struct bolex_mode * m = &bolex_modes[BOLEX_MODE_SENA];
        CHECK(m->sena && m->timer_a == 0x325 && m->angle == 360 && m->play_fps == 12, "Sena mode");
        CHECK(m->readout == BOLEX_RO_28K && bolex_modes[BOLEX_MODE_166].timer_a == m->timer_a, "Sena on the 2.8K readout");
        for (int i = 0; i < (int) COUNT(sena_fps); i++)
        {
            int b = bolex_timer_b_for(m, sena_fps[i] * 1000);
            double f = 32e6 / ((m->timer_a + 1.0) * (b + 1.0));
            CHECK(fabs(f - sena_fps[i]) < 0.01 && b < 0xFFFF, "Sena %d fps: B %d, %.3f", sena_fps[i], b, f);
        }
        CHECK(bolex_timer_b_for(m, 12000) == m->timer_b_idle, "Sena idle %d", bolex_timer_b_for(m, 12000));
        uint32_t seed = 7; int cur = -1, used[4] = {0}, same = 0;
        for (int k = 0; k < 10000; k++) { int n = sena_next(&seed, cur); same |= n == cur; used[n]++; cur = n; }
        CHECK(!same && used[0] && used[1] && used[2] && used[3], "Sena speeds");
        int left = 0, dropped = 0, run = 0, maxrun = 0;
        seed = 11;
        for (int f = 0; f < 100000; f++)
        {
            int d = sena_drop_frame(&seed, &left);
            dropped += d; run = d ? run + 1 : 0; maxrun = MAX(maxrun, run);
        }
        printf("Sena: drops %.1f%%, at most %d in a row\n", dropped / 1000.0, maxrun);
        CHECK(dropped > 37000 && dropped < 43000 && maxrun <= 4, "Sena drops %d, run %d", dropped, maxrun);
        int klo = 99999, khi = 0;
        for (int f = 0; f < 20000; f++) { int k = sena_kelvin(99, f); klo = k < klo ? k : klo; khi = k > khi ? k : khi; }
        CHECK(klo >= SENA_KELVIN_MIN && khi <= SENA_KELVIN_MAX && klo < 2100 && khi > 5900, "Sena kelvin %d..%d", klo, khi);
    }

    /* the cut at the end: by time (the speed wanders), here 1 s of 40 ms frames is 25 */
    {
        int times[100];
        for (int k = 0; k < 100; k++)
            times[k] = 100000 - k * 40;
        CHECK(bolex_trim_count(times, 100, 1000) == 25, "trim %d", bolex_trim_count(times, 100, 1000));
        for (int k = 0; k < 100; k++)
            times[k] = 100000 - k * 50;     /* 20 fps */
        CHECK(bolex_trim_count(times, 100, 1000) == 20, "trim 20 fps %d", bolex_trim_count(times, 100, 1000));
        CHECK(bolex_trim_count(times, 10, 1000) == 10, "short take");
        CHECK(BOLEX_TRIM_MS == 0 && BOLEX_HEAD_MS == 0, "no cuts at the ends");
        /* INFO twice: QUAD (at power on) -> 1.66 -> 976p 1.66 -> SENA -> QUAD */
        CHECK(bolex_mode == BOLEX_MODE_166_16 && COUNT(bolex_cycle) == 5, "start mode / cycle");
        CHECK(bolex_next_mode(BOLEX_MODE_166_16) == BOLEX_MODE_166 && bolex_next_mode(BOLEX_MODE_166) == BOLEX_MODE_976_166 &&
              bolex_next_mode(BOLEX_MODE_976_166) == BOLEX_MODE_694_166 && bolex_next_mode(BOLEX_MODE_694_166) == BOLEX_MODE_SENA &&
              bolex_next_mode(BOLEX_MODE_SENA) == BOLEX_MODE_166_16 && bolex_next_mode(BOLEX_MODE_SQUARE) == BOLEX_MODE_166_16, "cycle order");
        /* the 16-bit 1.66 frame: 2032x1224 x 2 bytes at its fastest within ~80 MB/s; 60P its fastest 60 */
        const struct bolex_mode * m16 = &bolex_modes[BOLEX_MODE_166_16], * m60 = &bolex_modes[BOLEX_MODE_694_166];
        double mbs = 2032.0 * 1224 * 2 * m16->fps_max / 1000 / 1e6;
        printf("2.8K 1.66 16: %.0f MB/s at %d fps; 60P: %.2f fps at its fastest\n", mbs, m16->fps_max / 1000,
               32e6 / ((m60->timer_a + 1.0) * (m60->timer_b_min + 1.0)));
        CHECK(m16->bits == 16 && m16->aspect_x100 == 166 && !m16->window_x && !m16->path && mbs < 81, "16-bit mode %.0f MB/s", mbs);
        CHECK(m60->readout == BOLEX_RO_694 && m60->aspect_x100 == 166 && m60->bits == 14 && m60->play_fps == 24, "60P mode");
        /* 60P for the hand: the shutter wanders 290-350 degrees, the blanking keeps it inside the frame */
        CHECK(m60->angle == (BOLEX_60P_ANGLE_MIN + BOLEX_60P_ANGLE_MAX) / 2, "60P middle angle %d", m60->angle);
        {
            struct bolex_wander wa;
            bolex_wander_start(&wa, 77, m60->angle * 1000);
            int alo = 999, ahi = 0;
            for (int t = 0; t < 600000; t += 40)
            {
                int a = bolex_wander_step(&wa, 40, BOLEX_60P_ANGLE_MIN * 1000, BOLEX_60P_ANGLE_MAX * 1000) / 1000;
                alo = MIN(alo, a); ahi = MAX(ahi, a);
                int tb = m60->timer_b_min + 1;
                int real = 360 * (tb - bolex_blanking(tb, a, bolex_pow2_x1000(BOLEX_FLICKER_X1000 + BOLEX_BREATH_MAX_X1000))) / tb;
                CHECK(real <= 360, "angle %d over the frame", real);
            }
            printf("60P shutter: %d..%d degrees\n", alo, ahi);
            CHECK(alo < 300 && ahi > 340, "60P angle range %d..%d", alo, ahi);
        }
    }

    /* intermittency per frame: within the ranges, the weave smooth (small steps) */
    {
        struct bolex_int v = { 0, 0, 0, 0 };
        int maxstep = 0, kmin = 99999, kmax = 0, emin = 9999, emax = -9999, ymin = 9999, ymax = -9999;
        int py = 0;
        for (int i = 0; i < 20000; i++)
        {
            bolex_int_next(0xABCDu, i, &v);
            kmin = v.kelvin < kmin ? v.kelvin : kmin; kmax = v.kelvin > kmax ? v.kelvin : kmax;
            emin = v.ev_x1000 < emin ? v.ev_x1000 : emin; emax = v.ev_x1000 > emax ? v.ev_x1000 : emax;
            ymin = v.dy_x100 < ymin ? v.dy_x100 : ymin; ymax = v.dy_x100 > ymax ? v.dy_x100 : ymax;
            if (i) maxstep = abs(v.dy_x100 - py) > maxstep ? abs(v.dy_x100 - py) : maxstep;
            py = v.dy_x100;
            CHECK(abs(v.dx_x100) <= BOLEX_WEAVE_X_X100 && abs(v.dy_x100) <= BOLEX_WEAVE_Y_X100, "weave %d %d", v.dx_x100, v.dy_x100);
        }
        printf("intermittency: %d..%d K, %d..%d mEV, dy %d..%d (x0.01 px), largest dy step %d\n", kmin, kmax, emin, emax, ymin, ymax, maxstep);
        CHECK(kmin >= BOLEX_KELVIN_BASE - BOLEX_KELVIN_JITTER && kmax <= BOLEX_KELVIN_BASE + BOLEX_KELVIN_JITTER, "kelvin");
        CHECK(emin >= -BOLEX_EV_JITTER_X1000 && emax <= BOLEX_EV_JITTER_X1000, "ev");
        CHECK(ymin < -BOLEX_WEAVE_Y_X100 / 2 && ymax > BOLEX_WEAVE_Y_X100 / 2, "weave too small %d..%d", ymin, ymax);
    }

    /* sporadic drops: about 1 in 40, never two in a row */
    {
        uint32_t seed = 5; int last = 0, dropped = 0, prev = 0, twice = 0;
        for (int f = 0; f < 100000; f++)
        {
            int d = bolex_drop_frame(&seed, &last);
            dropped += d; twice |= (d && prev); prev = d;
        }
        printf("drops: %.2f%%\n", dropped / 1000.0);
        CHECK(dropped > 2000 && dropped < 3000, "dropped %d of 100000", dropped);
        CHECK(!twice, "two in a row");
    }

    /* Up / Down arrows: ISO 100 / 200 / 400, no wrap-around */
    CHECK(bolex_isos[0] == ISO_100 && bolex_isos[1] == ISO_200 && bolex_isos[2] == ISO_400, "ISO list");
    CHECK(bolex_iso_step(0, 1) == 1 && bolex_iso_step(1, 1) == 2 && bolex_iso_step(2, 1) == 2, "ISO up");
    CHECK(bolex_iso_step(2, -1) == 1 && bolex_iso_step(0, -1) == 0, "ISO down");

    /* 976p 3x3 readout: 976 picture lines, square pixels, the centre 4:3 recorded */
    check_43("4:3 1620p", 1620, 0);

    /* 976p INF: second on INFO; a 1024x768 window inside the 1736x976 readout, travelling an
     * infinity sign on the 8-pixel grid the crop really moves on */
    {
        const struct bolex_mode * m = &bolex_modes[BOLEX_MODE_INF];
        CHECK(m->readout == BOLEX_RO_976 && m->window_x == 1024 && m->aspect_x100 == 133 && BOLEX_MODE_INF == 1,
              "INF mode, second");
        CHECK(m->fps_min == 42000 && m->fps_max == 46800, "INF speeds");
        for (int t = 0; t < 3600; t++)
            CHECK(fabs(bolex_sin_x1000(t) - 1000 * sin(t * M_PI / 1800)) <= 2.5, "sin %d: %d", t, bolex_sin_x1000(t));
        int res_x = 1024, res_y = calc_res_y(res_x, 976, 4, 3, 1.0f);
        int room_x = 1736 - res_x, room_y = 976 - res_y;
        int left_margin = 72;                    /* any multiple of 8 */
        int px = 0, py = 0, maxdx = 0, maxdy = 0, xlo = 9999, xhi = 0, ylo = 9999, yhi = 0, holds = 0;
        for (int n = 0; n < 3 * BOLEX_INF_FRAMES; n++)
        {
            int x, y;
            bolex_infinity_pos(n, room_x, room_y, &x, &y);
            /* what mlv_lite records: skip_x = margin + centre + offset, crop = (skip_x + 7) & ~7 */
            int skip_x = left_margin + room_x / 2 + (x - room_x / 2);
            int crop_x = (skip_x + 7) & ~7;
            CHECK(crop_x == left_margin + x, "x %d not on the grid", x);
            CHECK(x % 8 == 0 && y % 2 == 0 && x >= 0 && x <= room_x && y >= 0 && y <= room_y, "pos %d %d", x, y);
            if (n) { maxdx = MAX(maxdx, ABS(x - px)); maxdy = MAX(maxdy, ABS(y - py)); holds += x == px; }
            xlo = MIN(xlo, x); xhi = MAX(xhi, x); ylo = MIN(ylo, y); yhi = MAX(yhi, y);
            px = x; py = y;
            if (n == 0) CHECK(ABS(x - room_x / 2) <= 4 && ABS(y - room_y / 2) <= 1, "starts in the centre %d %d", x, y);
        }
        printf("infinity: %dx%d window, x %d..%d, y %d..%d, largest step %d px / %d lines, %d%% frames still across, loop %.1f s at 24\n",
               res_x, res_y, xlo, xhi, ylo, yhi, maxdx, maxdy, holds * 100 / (3 * BOLEX_INF_FRAMES), BOLEX_INF_FRAMES / 24.0);
        CHECK(res_y == 768, "window height %d", res_y);
        CHECK(xlo == 0 && xhi == (room_x & ~7) && ylo <= 2 && yhi >= room_y - 2, "the whole room used");
        CHECK(maxdx <= 16 && maxdy <= 8, "steps %d %d", maxdx, maxdy);
    }

    /* 2.8K QUAD (16-bit): a 1.66 window of 1600 in the 2880x1226 readout, a rectangle with
     * rounded corners at 2 px a frame along the path, on even columns and lines, starting at the
     * top of the right side going down; no jumps anywhere, the corners gradual */
    {
        const struct bolex_mode * m = &bolex_modes[BOLEX_MODE_SQUARE];
        CHECK(m->readout == BOLEX_RO_28K && m->window_x == 1600 && m->aspect_x100 == 166 && m->path == BOLEX_PATH_SQUARE &&
              bolex_modes[BOLEX_MODE_INF].path == BOLEX_PATH_INFINITY, "QUAD mode");
        int res_x = 1600, res_y = calc_res_y(res_x, 1226, 166, 100, 1.0f);
        int room_x = 2880 - res_x, room_y = 1226 - res_y;
        int px, py, x, y, maxdx = 0, maxdy = 0, xlo = 9999, xhi = 0, ylo = 9999, yhi = 0, loopn = 0, still = 0;
        bolex_square_pos(0, room_x, room_y, 2, &px, &py);
        CHECK(px == room_x && py == BOLEX_SQ_RADIUS, "starts at the top of the right side: %d %d", px, py);
        int x0 = px, y0 = py;
        for (int n = 1; n < 6000; n++)
        {
            bolex_square_pos(n, room_x, room_y, 2, &x, &y);
            CHECK(x % 2 == 0 && y % 2 == 0 && x >= 0 && x <= room_x && y >= 0 && y <= room_y, "pos %d %d", x, y);
            maxdx = MAX(maxdx, ABS(x - px)); maxdy = MAX(maxdy, ABS(y - py)); still += x == px && y == py;
            xlo = MIN(xlo, x); xhi = MAX(xhi, x); ylo = MIN(ylo, y); yhi = MAX(yhi, y);
            if (!loopn && n > 100 && x == x0 && y == y0) loopn = n;
            if (n == 5) CHECK(x == room_x && y == BOLEX_SQ_RADIUS + 10, "goes down first: %d %d", x, y);
            px = x; py = y;
        }
        printf("rounded rectangle: %dx%d window, room %dx%d, loop %d frames (%.1f s at 24), largest step %d px / %d lines, %d frames still\n",
               res_x, res_y, room_x, room_y, loopn, loopn / 24.0, maxdx, maxdy, still);
        CHECK(res_y == 964, "window height %d", res_y);
        CHECK(maxdx <= 2 && maxdy <= 2, "steps %d %d", maxdx, maxdy);
        CHECK(xlo == 0 && xhi == room_x && ylo == 0 && yhi == room_y, "the whole room");
        /* at 14-bit the same path on the 8-pixel grid */
        bolex_square_pos(7, room_x, room_y, 8, &x, &y);
        CHECK(x % 8 == 0, "8-pixel grid");
    }

    /* each mode's recorded frame, as mlv_lite works it out from the readout height */
    for (int mi = 0; mi < BOLEX_MODES; mi++)
    {
        const struct bolex_mode * m = &bolex_modes[mi];
        int max_y = m->readout == BOLEX_RO_1620 ? 1620 : m->readout == BOLEX_RO_28K ? 1226 : m->readout == BOLEX_RO_694 ? 694 : 976;
        int max_x = m->readout == BOLEX_RO_1620 ? 2160 : m->readout == BOLEX_RO_28K ? 2880 : 1736;
        int num = m->aspect_x100 == 133 ? 4 : m->aspect_x100, den = m->aspect_x100 == 133 ? 3 : 100;
        int w = MIN(max_y * num / den, max_x) & ~15;
        if (m->window_x)
            w = MIN(m->window_x, max_x) & ~15;
        int h = calc_res_y(w, max_y, num, den, 1.0f);
        double ar = (double) w / h;
        printf("frame %s: %dx%d, %.3f:1\n", m->name, w, h, ar);
        CHECK(fabs(ar - m->aspect_x100 / 100.0) < 0.012 && h <= max_y && w % 16 == 0, "%s frame %dx%d", m->name, w, h);
    }
    CHECK(BOLEX_RES_X == ((1620 * 4 / 3) & ~15), "width %d", BOLEX_RES_X);

    /* the width suits the raw format (8 pixels) and the lossless encoder (16) */
    CHECK(BOLEX_RES_X % 16 == 0, "width %d is not a multiple of 16", BOLEX_RES_X);


    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

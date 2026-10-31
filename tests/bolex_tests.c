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

    /* Bolex 1620p: filmed at 20..25 fps (dannephoto's 1620p line time), played at 24, 133 degrees */
    CHECK(BOLEX_FPS_X1000 == 24000, "Bolex camera speed %d", BOLEX_FPS_X1000);
    CHECK(BOLEX_ANGLE == 133, "Bolex shutter angle %d", BOLEX_ANGLE);
    CHECK(want > 64000 && want < 66000, "133 degrees at 24 fps must be ~1/65 s, got 1/%d.%03d", want / 1000, want % 1000);

    /* the speed: wandering between 20 and 25 fps all through a take, smooth, using the range;
     * the file plays at 24 */
    CHECK(BOLEX_PLAY_FPS == 24, "projection speed %d", BOLEX_PLAY_FPS);
    CHECK(bolex_timer_b_for(25000) == 0x7E2 && bolex_timer_b_for(23976) == BOLEX_1620_TIMER_B, "timers %x %x",
          bolex_timer_b_for(25000), bolex_timer_b_for(23976));
    {
        struct bolex_wander w;
        int lo = 99999, hi = 0, maxstep = 0, below21 = 0, above24 = 0, n = 0;
        for (int take = 0; take < 50; take++)
        {
            bolex_wander_start(&w, 1234567u * (take + 1), 23976);
            int prev = 23976;
            for (int t = 0; t < 60000; t += 40, n++)       /* a minute, the task's 40 ms */
            {
                int f = bolex_wander_step(&w, 40);
                CHECK(f >= BOLEX_FPS_MIN_X1000 && f <= BOLEX_FPS_MAX_X1000, "speed %d", f);
                maxstep = MAX(maxstep, ABS(f - prev));
                prev = f;
                lo = f < lo ? f : lo; hi = f > hi ? f : hi;
                below21 += f < 21000; above24 += f > 24000;
                int b = bolex_timer_b_for(f);
                CHECK(b >= 0x7E2 && b <= bolex_timer_b_for(20000), "timer B %x", b);
            }
        }
        printf("speed: %d..%d fps x1000, largest change per 40 ms %d, %d%% below 21, %d%% above 24\n",
               lo, hi, maxstep, below21 * 100 / n, above24 * 100 / n);
        CHECK(lo < 21000 && hi > 24500, "range used %d..%d", lo, hi);
        CHECK(maxstep <= 150, "a jump of %d", maxstep);
        CHECK(below21 * 100 / n >= 5 && above24 * 100 / n >= 5, "range rarely reached");
    }

    /* the shutter: 133 degrees at every speed of the range (the blanking for the Timer B) */
    for (int f = BOLEX_FPS_MIN_X1000; f <= BOLEX_FPS_MAX_X1000; f += 100)
    {
        int tb = bolex_timer_b_for(f) + 1;
        double angle = 360.0 * (tb - bolex_blanking(tb)) / tb;
        CHECK(fabs(angle - BOLEX_ANGLE) < 0.3, "%d fps x1000: %.2f degrees", f, angle);
    }
    {
        int t20 = bolex_timer_b_for(20000) + 1, t25 = bolex_timer_b_for(25000) + 1;
        printf("shutter: 20 fps 1/%.0f s, 25 fps 1/%.0f s (both %d degrees)\n",
               360.0 * 20 / BOLEX_ANGLE, 360.0 * 25 / BOLEX_ANGLE, BOLEX_ANGLE);
        (void) t20; (void) t25;
    }

    /* the cut at the end: by time (the speed wanders), here 1 s of 40 ms frames is 25 */
    {
        int times[100];
        for (int k = 0; k < 100; k++)
            times[k] = 100000 - k * 40;
        CHECK(bolex_trim_count(times, 100, BOLEX_TRIM_MS) == 25, "trim %d", bolex_trim_count(times, 100, BOLEX_TRIM_MS));
        for (int k = 0; k < 100; k++)
            times[k] = 100000 - k * 50;     /* 20 fps */
        CHECK(bolex_trim_count(times, 100, BOLEX_TRIM_MS) == 20, "trim 20 fps %d", bolex_trim_count(times, 100, BOLEX_TRIM_MS));
        CHECK(bolex_trim_count(times, 10, BOLEX_TRIM_MS) == 10, "short take");
        CHECK(BOLEX_TRIM_MS == 1000 && BOLEX_HEAD_MS == 1000, "1 s at each end");
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
    CHECK(BOLEX_RES_X == ((1620 * 4 / 3) & ~15), "width %d", BOLEX_RES_X);

    /* the width suits the raw format (8 pixels) and the lossless encoder (16) */
    CHECK(BOLEX_RES_X % 16 == 0, "width %d is not a multiple of 16", BOLEX_RES_X);


    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

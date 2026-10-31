/* Tests for the Bolex speeds (32 fps played at 18), its shutter (360 degrees) and its correction in crop_rec.c - run on a computer, see tests/run.sh */
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

    /* filmed at 32 fps (976p readout, Bolex timers), played at 18; 360 degrees is 1/32 s */
    /* Bolex 1620p: filmed at 23.976 (dannephoto's 1620p timers), played at 18 */
    CHECK(BOLEX_FPS_X1000 == 24000, "Bolex camera speed %d", BOLEX_FPS_X1000);
    CHECK(BOLEX_ANGLE == 133, "Bolex shutter angle %d", BOLEX_ANGLE);
    CHECK(want > 64000 && want < 66000, "133 degrees at 24 fps must be ~1/65 s, got 1/%d.%03d", want / 1000, want % 1000);

    /* the speed: 18 fps at the start of a take, rising to 23.976 over BOLEX_RAMP_MS, then held;
     * the file plays at 24 */
    CHECK(BOLEX_PLAY_FPS == 24, "projection speed %d", BOLEX_PLAY_FPS);
    {
        double f0 = 32e6 / ((BOLEX_1620_TIMER_A + 1.0) * (bolex_ramp_timer_b(0) + 1.0));
        double fe = 32e6 / ((BOLEX_1620_TIMER_A + 1.0) * (bolex_ramp_timer_b(BOLEX_RAMP_MS) + 1.0));
        CHECK(fabs(f0 - 18.0) < 0.01, "start %.3f fps", f0);
        CHECK(fabs(fe - 23.976) < 0.01 && bolex_ramp_timer_b(BOLEX_RAMP_MS + 5000) == BOLEX_1620_TIMER_B, "end %.3f fps", fe);
        int prev = bolex_ramp_timer_b(0);
        for (int t = 0; t <= BOLEX_RAMP_MS; t += 40)
        {
            int b = bolex_ramp_timer_b(t);
            CHECK(b <= prev && b >= BOLEX_1620_TIMER_B, "ramp not monotonic at %d ms: %d", t, b);
            prev = b;
        }
        printf("ramp: %.2f fps at 0 s, %.2f at 0.5 s, %.2f at 1 s, %.2f at 2 s\n", f0,
               32e6 / ((BOLEX_1620_TIMER_A + 1.0) * (bolex_ramp_timer_b(500) + 1.0)),
               32e6 / ((BOLEX_1620_TIMER_A + 1.0) * (bolex_ramp_timer_b(1000) + 1.0)), fe);
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

    /* the one shutter: Canon raw 108 at 31.999 fps, through crop_rec's mapping, is ~133 degrees
     * (a Bolex H16 Reflex), and no other Canon 1/8-EV code is nearer */
    {
        int fps = 23976, best = 0, best_err = 1000;
        for (int raw = 96; raw <= 152; raw++)
        {
            int us = (int)(1e6 * pow(2.0, (56 - raw) / 8.0));
            int angle = (int)((long long) bolex_exposure_us(us, fps) * fps * 360 / 1000000000LL);
            int err = abs(angle - 133);
            if (err < best_err) { best_err = err; best = raw; }
        }
        int us = (int)(1e6 * pow(2.0, (56 - BOLEX_RAW_SHUTTER) / 8.0));
        int angle = (int)((long long) bolex_exposure_us(us, fps) * fps * 360 / 1000000000LL);
        printf("shutter raw %d: 1/%d s, %d degrees (nearest to 133: raw %d)\n", BOLEX_RAW_SHUTTER,
               1000000 / bolex_exposure_us(us, fps), angle, best);
        CHECK(BOLEX_RAW_SHUTTER == best, "raw %d is not the nearest to 133 degrees (%d is)", BOLEX_RAW_SHUTTER, best);
        CHECK(angle >= 128 && angle <= 138, "angle %d", angle);
    }

    /* the real exposure at 23.976 fps: Canon 1/30 is the whole frame (360 degrees), faster
     * Canon values shorter, 1/4000 almost nothing */
    {
        int fps = 23976, frame = 1000000000 / fps;
        CHECK(bolex_exposure_us(33333, fps) == frame, "1/30 -> %d us, frame %d", bolex_exposure_us(33333, fps), frame);
        int e50 = bolex_exposure_us(20000, fps), e100 = bolex_exposure_us(10000, fps), e4000 = bolex_exposure_us(250, fps);
        printf("exposure at 23.976: Canon 1/50 -> 1/%d, 1/100 -> 1/%d, 1/4000 -> %d us\n", 1000000 / e50, 1000000 / e100, e4000);
        CHECK(e50 < frame && e100 < e50 && e4000 >= 1 && e4000 < 100, "exposures %d %d %d", e50, e100, e4000);
        CHECK(bolex_exposure_us(0, 0) == 0, "no fps");
    }

    /* 976p 3x3 readout: 976 picture lines, square pixels, the centre 4:3 recorded */
    check_43("4:3 1620p", 1620, 0);
    CHECK(BOLEX_RES_X == ((1620 * 4 / 3) & ~15), "width %d", BOLEX_RES_X);

    /* the width suits the raw format (8 pixels) and the lossless encoder (16) */
    CHECK(BOLEX_RES_X % 16 == 0, "width %d is not a multiple of 16", BOLEX_RES_X);


    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

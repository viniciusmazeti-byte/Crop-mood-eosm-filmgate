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

    /* Sena: 12 fps in Live View, 4 .. 12 fps drawn while recording, played at 12,
     * always 360 degrees */
    CHECK(BOLEX_FPS_X1000 == 12000, "Live View speed %d", BOLEX_FPS_X1000);
    CHECK(BOLEX_PLAY_FPS == 12, "projection speed %d", BOLEX_PLAY_FPS);
    CHECK(BOLEX_ANGLE == 360, "shutter angle %d", BOLEX_ANGLE);
    CHECK(want == 12000, "360 degrees at 12 fps must be 1/12 s, got 1/%d.%03d", want / 1000, want % 1000);
    CHECK(SENA_TIMER_A == 0x325, "Timer A 0x%X, not 2.8K's line time 0x325", SENA_TIMER_A);
    CHECK(SENA_HOLD_MIN_MS == 500 && SENA_HOLD_MAX_MS == 2000, "hold %d..%d ms", SENA_HOLD_MIN_MS, SENA_HOLD_MAX_MS);
    for (int i = 0; i < (int)(sizeof sena_fps / sizeof sena_fps[0]); i++)
    {
        int b = sena_timer_b(sena_fps[i]);
        double f = 32e6 / ((SENA_TIMER_A + 1.0) * (b + 1.0));
        printf("Sena %2d fps: Timer B %d -> %.3f fps, %.1fx faster at %d\n", sena_fps[i], b, f, BOLEX_PLAY_FPS / f, BOLEX_PLAY_FPS);
        CHECK(fabs(f - sena_fps[i]) < 0.01, "%d fps -> %.3f", sena_fps[i], f);
        CHECK(b > 0x4D3 + 100 && b <= 0xFFFF, "Timer B %d out of range (2.8K frame, 16 bits)", b);
        /* 360 degrees: Canon 1/30 is the whole frame at this speed */
        int us = (int)(1e6 * pow(2.0, (56 - BOLEX_RAW_SHUTTER) / 8.0));
        int fx = sena_fps[i] * 1000;
        CHECK(bolex_exposure_us(us, fx) == 1000000000 / fx, "%d fps: exposure %d us, not the whole frame", sena_fps[i], bolex_exposure_us(us, fx));
    }
    CHECK(sena_timer_b(BOLEX_FPS_X1000 / 1000) == 32000000 / (806 * 12) - 1, "12 fps Timer B %d", sena_timer_b(12));

    /* the speed draw: never the same speed twice in a row, all speeds used, holds in range */
    {
        uint32_t seed = 12345;
        int cur = -1, seen[8] = {0};
        for (int n = 0; n < 1000; n++)
        {
            int i;
            do i = sena_rand(&seed) % (sizeof sena_fps / sizeof sena_fps[0]); while (i == cur);
            CHECK(i != cur, "same speed twice");
            cur = i; seen[i]++;
            int hold = SENA_HOLD_MIN_MS + sena_rand(&seed) % (SENA_HOLD_MAX_MS - SENA_HOLD_MIN_MS + 1);
            CHECK(hold >= SENA_HOLD_MIN_MS && hold <= SENA_HOLD_MAX_MS, "hold %d", hold);
        }
        for (int i = 0; i < (int)(sizeof sena_fps / sizeof sena_fps[0]); i++)
            CHECK(seen[i] > 150, "speed %d drawn %d times of 1000", sena_fps[i], seen[i]);
    }

    /* Up / Down arrows: ISO 100 / 200 / 400, no wrap-around */
    CHECK(bolex_isos[0] == ISO_100 && bolex_isos[1] == ISO_200 && bolex_isos[2] == ISO_400, "ISO list");
    CHECK(bolex_iso_step(0, 1) == 1 && bolex_iso_step(1, 1) == 2 && bolex_iso_step(2, 1) == 2, "ISO up");
    CHECK(bolex_iso_step(2, -1) == 1 && bolex_iso_step(0, -1) == 0, "ISO down");

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

    /* Sena colour temperature per frame: 2000..6000 K, spread over the range, the same for the
     * same seed and frame (the .KEL is written at the end from them), different per take */
    {
        int lo = 99999, hi = 0, bins[4] = {0}, same = 1;
        for (int f = 0; f < 4000; f++)
        {
            int k = sena_kelvin(0x1234u, f);
            lo = k < lo ? k : lo; hi = k > hi ? k : hi;
            bins[(k - SENA_KELVIN_MIN) * 4 / (SENA_KELVIN_MAX - SENA_KELVIN_MIN + 1)]++;
            same &= (k == sena_kelvin(0x1234u, f));
        }
        CHECK(lo >= 2000 && hi <= 6000 && lo < 2050 && hi > 5950, "kelvin range %d..%d", lo, hi);
        for (int b = 0; b < 4; b++)
            CHECK(bins[b] > 800, "kelvin quarter %d drawn %d of 4000", b, bins[b]);
        CHECK(same, "kelvin not repeatable");
        CHECK(sena_kelvin(0x1234u, 7) != sena_kelvin(0x9876u, 7) || sena_kelvin(0x1234u, 8) != sena_kelvin(0x9876u, 8), "seed has no effect");
        printf("Sena kelvins: %d..%d, quarters %d %d %d %d\n", lo, hi, bins[0], bins[1], bins[2], bins[3]);
    }

    /* Sena frame drops: about 2 in 5, bursts of 1..4, never longer */
    {
        uint32_t seed = 77; int left = 0, dropped = 0, run = 0, maxrun = 0, bursts[5] = {0};
        for (int f = 0; f < 100000; f++)
        {
            if (sena_drop_frame(&seed, &left)) { dropped++; run++; }
            else { if (run) bursts[run > 4 ? 4 : run]++; maxrun = run > maxrun ? run : maxrun; run = 0; }
        }
        double frac = dropped / 100000.0;
        printf("Sena drops: %.1f%% dropped, bursts 1:%d 2:%d 3:%d 4+:%d, longest %d\n", frac * 100, bursts[1], bursts[2], bursts[3], bursts[4], maxrun);
        CHECK(frac > 0.37 && frac < 0.43, "dropped %.3f, not ~0.4", frac);
        CHECK(bursts[1] > 0 && bursts[2] > 0 && bursts[3] > 0 && bursts[4] > 0, "all burst lengths");
        CHECK(maxrun == 4, "longest run %d, not 4", maxrun);
    }

    /* 2.8K 1x1 readout: 1226 picture lines, square pixels, the centre 4:3 recorded */
    check_43("4:3 2.8K", 1226, 0);
    CHECK(BOLEX_RES_X == ((1226 * 4 / 3) & ~15), "width %d", BOLEX_RES_X);
    printf("2.8K 4:3: %.1f x %.1f mm of sensor, rolling shutter %.1f ms\n", BOLEX_RES_X * 0.0043, 1224 * 0.0043,
           1224 * (SENA_TIMER_A + 1.0) / 32e6 * 1000);

    /* the width suits the raw format (8 pixels) and the lossless encoder (16) */
    CHECK(BOLEX_RES_X % 16 == 0, "width %d is not a multiple of 16", BOLEX_RES_X);


    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

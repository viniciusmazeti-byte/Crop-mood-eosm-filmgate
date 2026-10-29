/* Tests for the Bolex speeds (32 fps played at 18), its shutter (360 degrees) and its correction in crop_rec.c - run on a computer, see tests/run.sh */
#include <stdio.h>
#include <math.h>
#include <stdint.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

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
    float squeeze = (1 + skip_y) / 3.0f;            /* lines 1 of (1+skip), columns binned 3 */
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

    /* filmed at 32 fps, played at 18; 360 degrees at 32 fps is 1/32 s */
    CHECK(BOLEX_FPS_X1000 == 32000, "Bolex camera speed %d", BOLEX_FPS_X1000);
    CHECK(BOLEX_PLAY_FPS == 18, "Bolex projection speed %d", BOLEX_PLAY_FPS);
    CHECK(BOLEX_ANGLE == 360, "Bolex shutter angle %d", BOLEX_ANGLE);
    CHECK(want == 32000, "360 degrees at 32 fps must be 1/32 s, got 1/%d.%03d", want / 1000, want % 1000);

    /* on target (and within half a step of it): no change */
    CHECK(bolex_shutter_steps(want, want) == 0, "on target");
    CHECK(bolex_shutter_steps(want * 1040 / 1000, want) == 0, "+4%% counts as on target");
    CHECK(bolex_shutter_steps(want * 960 / 1000, want) == 0, "-4%% counts as on target");

    /* one stop too slow (1/16): 8 steps faster; one stop too fast (1/64): 8 steps slower */
    CHECK(bolex_shutter_steps(want / 2, want) == 8, "1/16 -> %d steps, expected 8", bolex_shutter_steps(want / 2, want));
    CHECK(bolex_shutter_steps(want * 2, want) == -8, "1/64 -> %d steps, expected -8", bolex_shutter_steps(want * 2, want));

    /* nothing measured yet: no change */
    CHECK(bolex_shutter_steps(0, want) == 0, "no reading");

    /* a camera whose shutter follows the raw value exactly (1/8 EV per step): starting anywhere
     * from 4 stops slow to 4 stops fast, one correction lands within half a step of 1/32 */
    for (int off = -32; off <= 32; off++)
    {
        double real = want * pow(2.0, off / 8.0);
        int steps = bolex_shutter_steps((int) real, want);
        double after = real * pow(2.0, steps / 8.0);
        CHECK(fabs(after / want - 1.0) <= 0.045, "start %+d steps: after %d steps 1/%.2f", off, steps, after / 1000);
    }

    /* the steps never run away */
    CHECK(bolex_shutter_steps(1, want) <= 48, "very slow reading is capped");
    CHECK(bolex_shutter_steps(2000000000 / 2, want) >= -48, "very fast reading is capped");

    /* 3x5: Canon x1 raw is 727 lines, 28 on top and 3 at the bottom are not picture */
    check_43("4:3 3x5", (727 - 28 - 3) & ~1, 4);

    /* the width suits the raw format (8 pixels) and the lossless encoder (16) */
    CHECK(BOLEX_RES_X % 16 == 0, "width %d is not a multiple of 16", BOLEX_RES_X);

    /* Bolex 25 mm C-mount: in a 1536-wide frame from the camera, black starts at ~76% of the
     * half diagonal (both upper corners and both sides).  The recorded 4:3 frame is centred,
     * so its corners sit at BOLEX_RES_X / 1536 of that half diagonal: keep 2% of margin. */
    CHECK(BOLEX_RES_X * 100 / 1536 <= 74, "corners at %d%% of the old frame: inside the black (76%%)", BOLEX_RES_X * 100 / 1536);
    CHECK(BOLEX_RES_X * 100 / 1536 >= 68, "frame %d%% smaller than needed", BOLEX_RES_X * 100 / 1536);

    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

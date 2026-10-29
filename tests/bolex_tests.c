/* Tests for the Bolex shutter (360 degrees at 18 fps) and its correction in crop_rec.c - run on a computer, see tests/run.sh */
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
#include "bolex_mlv_snippets.h"      /* calc_res_y, copied out of mlv_lite.c */

/* the recorded size for a reading, and how 4:3 it is once stretched */
static void check_43(const char * name, int max_res_y, int skip_y)
{
    float squeeze = (1 + skip_y) / 3.0f;            /* lines 1 of (1+skip), columns binned 3 */
    int w = 1536;
    int h = calc_res_y(w, max_res_y, 4, 3, squeeze);
    float aspect = w / (h * squeeze);
    CHECK(h <= max_res_y && (h & 1) == 0, "%s: height %d (max %d)", name, h, max_res_y);
    CHECK(fabs(aspect - 4.0 / 3.0) < 0.01, "%s: %dx%d stretched %.3f is %.3f:1, not 4:3", name, w, h, squeeze, aspect);
    printf("%s: %dx%d, stretch %.3f -> %.3f:1\n", name, w, h, squeeze, aspect);
}

int main(void)
{
    const int want = BOLEX_SHUTTER_X1000;

    /* 360 degrees at 18 fps is 1/18 s */
    CHECK(BOLEX_FPS_X1000 == 18000, "Bolex frame rate %d", BOLEX_FPS_X1000);
    CHECK(BOLEX_ANGLE == 360, "Bolex shutter angle %d", BOLEX_ANGLE);
    CHECK(want == 18000, "360 degrees at 18 fps must be 1/18 s, got 1/%d.%03d", want / 1000, want % 1000);

    /* on target (and within half a step of it): no change */
    CHECK(bolex_shutter_steps(want, want) == 0, "on target");
    CHECK(bolex_shutter_steps(want * 1040 / 1000, want) == 0, "+4%% counts as on target");
    CHECK(bolex_shutter_steps(want * 960 / 1000, want) == 0, "-4%% counts as on target");

    /* one stop too slow (1/9): 8 steps faster; one stop too fast (1/36): 8 steps slower */
    CHECK(bolex_shutter_steps(want / 2, want) == 8, "1/9 -> %d steps, expected 8", bolex_shutter_steps(want / 2, want));
    CHECK(bolex_shutter_steps(want * 2, want) == -8, "1/36 -> %d steps, expected -8", bolex_shutter_steps(want * 2, want));

    /* nothing measured yet: no change */
    CHECK(bolex_shutter_steps(0, want) == 0, "no reading");

    /* a camera whose shutter follows the raw value exactly (1/8 EV per step): starting anywhere
     * from 4 stops slow to 4 stops fast, one correction lands within half a step of 1/18 */
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
    /* 3x7: BOLEX_X7_ROWS lines read, plus the 29-line margin, 28 of it on top */
    check_43("4:3 3x7", (BOLEX_X7_ROWS + 29 - 28) & ~1, 6);
    CHECK(BOLEX_X7_ROWS * 7 <= 3480, "3x7 reads %d sensor lines, more than the sensor has", BOLEX_X7_ROWS * 7);

    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

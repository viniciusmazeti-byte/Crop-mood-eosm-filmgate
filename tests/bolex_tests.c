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

    /* reading tests: each one changes the proven 694p readout (1736x694, 1 line of 3) */
    CHECK(BOLEX_READINGS == 8, "%d readings", BOLEX_READINGS);
    CHECK(bolex_readings[0].rows == 0 && bolex_readings[0].fps18, "reading 0 is the normal one (no preset)");
    CHECK(bolex_readings[1].rows == 694 && bolex_readings[1].skip == 3 && bolex_readings[1].cmos7 == 0x806 &&
          !bolex_readings[1].fps18, "T0 must be the untouched 694p readout");
    CHECK(bolex_readings[2].rows == 738 && bolex_readings[2].skip == 3 && bolex_readings[2].ar == 3 &&
          !bolex_readings[2].fps18, "T0B must be the untouched 738p readout");
    CHECK(bolex_readings[3].rows == 694 && bolex_readings[3].skip == 3 && bolex_readings[3].cmos7 == 0x806 &&
          bolex_readings[3].fps18, "T1 must be the 694p readout at 18 fps");
    for (int i = 3; i < BOLEX_READINGS; i++)
        CHECK(bolex_readings[i].ar == 4, "%s must use the 694p readout", bolex_readings[i].name);
    for (int i = 3; i < BOLEX_READINGS; i++)
        CHECK(bolex_readings[i].fps18, "%s must run at 18 fps", bolex_readings[i].name);
    for (int i = 1; i < BOLEX_READINGS; i++)
    {
        const struct bolex_reading * r = &bolex_readings[i];
        CHECK(r->rows > 0 && r->rows <= 738 && (r->rows & 1) == 0, "%s: %d lines", r->name, r->rows);
        CHECK(r->skip >= 3 && r->skip <= 16, "%s: skip %d", r->name, r->skip);
        CHECK(r->cmos7 == 0x800 || r->cmos7 == 0x806, "%s: CMOS[7] %x", r->name, r->cmos7);
        /* readings that cover the sensor from the top, within its 3480 lines: a true 4:3 frame */
        if (r->cmos7 == 0x800 && r->rows * r->skip <= 3480)
        {
            check_43(r->name, r->rows, r->skip - 1);
            if (r->skip > 5)
                CHECK(r->rows * r->skip >= 3400, "%s covers only %d of the sensor's lines", r->name, r->rows * r->skip);
        }
    }
    /* T4 / T5 read fewer lines than Canon's x1 (696): that is the point */
    CHECK(bolex_readings[6].rows < 696 * 3 / 4 && bolex_readings[7].rows < 696 / 2, "fewer lines than 3x5");

    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

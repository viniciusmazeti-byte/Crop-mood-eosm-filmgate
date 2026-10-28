/* Tests for the Bolex shutter correction in crop_rec.c - run on a computer, see tests/run.sh */
#include <stdio.h>
#include <math.h>
#include <stdint.h>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#include "bolex_snippets.h"   /* the real code, copied out of crop_rec.c by tests/run.sh */

int main(void)
{
    const int want = BOLEX_SHUTTER_X1000;

    /* 180 degrees at 18 fps is 1/36 s */
    CHECK(BOLEX_FPS_X1000 == 18000, "Bolex frame rate %d", BOLEX_FPS_X1000);
    CHECK(want == 36000, "180 degrees at 18 fps must be 1/36 s, got 1/%d.%03d", want / 1000, want % 1000);

    /* on target (and within half a step of it): no change */
    CHECK(bolex_shutter_steps(want, want) == 0, "on target");
    CHECK(bolex_shutter_steps(want * 1040 / 1000, want) == 0, "+4%% counts as on target");
    CHECK(bolex_shutter_steps(want * 960 / 1000, want) == 0, "-4%% counts as on target");

    /* one stop too slow (1/18): 8 steps faster; one stop too fast (1/72): 8 steps slower */
    CHECK(bolex_shutter_steps(want / 2, want) == 8, "1/18 -> %d steps, expected 8", bolex_shutter_steps(want / 2, want));
    CHECK(bolex_shutter_steps(want * 2, want) == -8, "1/72 -> %d steps, expected -8", bolex_shutter_steps(want * 2, want));

    /* nothing measured yet: no change */
    CHECK(bolex_shutter_steps(0, want) == 0, "no reading");

    /* a camera whose shutter follows the raw value exactly (1/8 EV per step): starting anywhere
     * from 4 stops slow to 4 stops fast, one correction lands within half a step of 1/36 */
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

    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

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

    /* filmed at 23.976 (1620p's timers), played at 18; 360 degrees at 24 fps is 1/24 s */
    CHECK(BOLEX_FPS_X1000 == 24000, "Bolex camera speed %d", BOLEX_FPS_X1000);
    CHECK(BOLEX_PLAY_FPS == 18, "Bolex projection speed %d", BOLEX_PLAY_FPS);
    CHECK(BOLEX_ANGLE == 360, "Bolex shutter angle %d", BOLEX_ANGLE);
    CHECK(want == 24000, "360 degrees at 24 fps must be 1/24 s, got 1/%d.%03d", want / 1000, want % 1000);

    /* 1620p 1x1 readout: 1620 picture lines, square pixels, the whole readout recorded */
    check_43("4:3 1620p", 1620, 0);
    CHECK(BOLEX_RES_X == 2160, "width %d, not the whole 1620p readout", BOLEX_RES_X);

    /* 2160x1620 of 4.3 um pixels: about the 16 mm film gate (10.26 x 7.49 mm), no larger */
    double w_mm = BOLEX_RES_X * 0.0043, h_mm = 1620 * 0.0043;
    printf("1620p: %.2f x %.2f mm of sensor\n", w_mm, h_mm);
    CHECK(w_mm <= 10.26 && h_mm <= 7.49, "%.2f x %.2f mm is larger than the 16 mm gate", w_mm, h_mm);

    /* the width suits the raw format (8 pixels) and the lossless encoder (16) */
    CHECK(BOLEX_RES_X % 16 == 0, "width %d is not a multiple of 16", BOLEX_RES_X);


    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

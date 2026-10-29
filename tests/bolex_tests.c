/* Tests for the Bolex speeds (32 fps played at 18), its shutter (360 degrees) and its correction in crop_rec.c - run on a computer, see tests/run.sh */
#include <stdio.h>
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

    /* filmed at 32 fps (976p readout, Bolex timers), played at 18; 360 degrees is 1/32 s */
    CHECK(BOLEX_FPS_X1000 == 32000, "Bolex camera speed %d", BOLEX_FPS_X1000);
    double fps = 32e6 / ((BOLEX_3X3_TIMER_A + 1.0) * (BOLEX_3X3_TIMER_B + 1.0));
    CHECK(fabs(fps - 32.0) < 0.01, "976p timers give %.4f fps, not 32", fps);
    CHECK(BOLEX_3X3_TIMER_A == 0x20F, "Timer A 0x%X, not 976p's line time 0x20F", BOLEX_3X3_TIMER_A);
    /* the frame (Timer B lines) is longer than the 976p readout (RAW_V 0x3ED) */
    CHECK(BOLEX_3X3_TIMER_B > 0x3ED + 100, "Timer B 0x%X too short for 1005 lines", BOLEX_3X3_TIMER_B);
    printf("976p timers: %.4f fps, rolling shutter over 972 lines %.1f ms\n", fps,
           972 * (BOLEX_3X3_TIMER_A + 1.0) / 32e6 * 1000);
    CHECK(BOLEX_PLAY_FPS == 18, "Bolex projection speed %d", BOLEX_PLAY_FPS);
    CHECK(BOLEX_ANGLE == 360, "Bolex shutter angle %d", BOLEX_ANGLE);
    CHECK(want == 32000, "360 degrees at 32 fps must be 1/32 s, got 1/%d.%03d", want / 1000, want % 1000);

    /* Up / Down arrows: ISO 100 / 200 / 400, no wrap-around */
    CHECK(bolex_isos[0] == ISO_100 && bolex_isos[1] == ISO_200 && bolex_isos[2] == ISO_400, "ISO list");
    CHECK(bolex_iso_step(0, 1) == 1 && bolex_iso_step(1, 1) == 2 && bolex_iso_step(2, 1) == 2, "ISO up");
    CHECK(bolex_iso_step(2, -1) == 1 && bolex_iso_step(0, -1) == 0, "ISO down");

    /* rear dial: Canon's 1/3 stops from 1/30 to 1/4000, both ways */
    {
        static const int thirds[] = {96, 99, 101, 104, 107, 109, 112};   /* 1/30 1/40 1/50 1/60 1/80 1/100 1/125 */
        for (int i = 0; i + 1 < (int)(sizeof thirds / sizeof thirds[0]); i++)
        {
            CHECK(bolex_next_third(thirds[i], 1) == thirds[i + 1], "%d +1 -> %d", thirds[i], bolex_next_third(thirds[i], 1));
            CHECK(bolex_next_third(thirds[i + 1], -1) == thirds[i], "%d -1 -> %d", thirds[i + 1], bolex_next_third(thirds[i + 1], -1));
        }
        int r = BOLEX_RAW_1_30, n = 0;
        while (r < BOLEX_RAW_1_4000) { r = bolex_next_third(r, 1); n++; }
        CHECK(r == BOLEX_RAW_1_4000 && n == 21, "1/30 -> 1/4000 in %d clicks, ends at %d", n, r);
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
    check_43("4:3 976p", 976, 2);
    CHECK(BOLEX_RES_X == 1296, "width %d", BOLEX_RES_X);

    /* the width suits the raw format (8 pixels) and the lossless encoder (16) */
    CHECK(BOLEX_RES_X % 16 == 0, "width %d is not a multiple of 16", BOLEX_RES_X);


    printf("bolex_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

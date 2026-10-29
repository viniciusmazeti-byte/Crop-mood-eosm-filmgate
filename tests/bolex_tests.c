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

    /* 32 fps from crop_rec's timers on the 3x3 3:2 readout: 32 MHz / ((A+1) * (B+1)) */
    double fps = 32e6 / ((BOLEX_3X3_TIMER_A + 1.0) * (BOLEX_3X3_TIMER_B + 1.0));
    CHECK(fabs(fps - 32.0) < 0.01, "3x3 timers give %.4f fps, not 32", fps);
    printf("3x3 timers: %.4f fps\n", fps);

    /* the line time of the proven 24/30 fps on this readout (rolling shutter unchanged) */
    CHECK(BOLEX_3X3_TIMER_A == 0x20F, "Timer A 0x%X, not the 24/30 fps line time 0x20F", BOLEX_3X3_TIMER_A);

    /* the frame is longer than the 1189-line readout (RAW_V 0x4A5) plus blanking, and not
     * shorter than the 36 fps frame already used on this readout (Timer B 0x693) */
    CHECK(BOLEX_3X3_TIMER_B > 0x693, "Timer B 0x%X shorter than the 36 fps one", BOLEX_3X3_TIMER_B);

    /* rolling shutter over the recorded 828 lines: ~13.7 ms */
    double rs_ms = 828 * (BOLEX_3X3_TIMER_A + 1.0) / 32e6 * 1000;
    printf("rolling shutter over the frame: %.1f ms\n", rs_ms);
    CHECK(rs_ms < 14.0, "rolling shutter %.1f ms", rs_ms);

    /* 3x3 3:2 readout: 1160 picture lines (square pixels) */
    check_43("4:3 3x3", 1160, 2);
    /* the width also stays inside the old 3x5 view (Canon x1 raw is 727 lines, 28 on top and
     * 3 at the bottom are not picture), should the Bolex go back to it */
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

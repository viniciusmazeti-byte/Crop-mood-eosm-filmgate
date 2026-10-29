/* Tests for the Bolex tripod mode arithmetic (src/bolex-stab.h) - run on a computer, see tests/run.sh */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "bolex-stab.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* a "landscape": smooth random profile, sampled at any (fractional) position */
static double scene[2000];
static void make_scene(unsigned seed)
{
    srand(seed);
    double v = 0;
    for (int i = 0; i < 2000; i++)
    {
        v = 0.9 * v + (rand() % 2001 - 1000);
        scene[i] = 20000 + v;
    }
}
static double scene_at(double x)
{
    int i = (int) floor(x);
    double f = x - i;
    return scene[i] * (1 - f) + scene[i + 1] * f;
}
/* profile of n bins, the picture moved by 'shift' bins, brightness changed by 'gain' */
static void profile(int32_t * p, int n, double shift, double gain, int noise)
{
    for (int i = 0; i < n; i++)
        p[i] = (int32_t)(scene_at(500 + i - shift) * gain + (noise ? rand() % noise - noise / 2 : 0));
}

int main(void)
{
    static int32_t a[STAB_MAX_BINS], b[STAB_MAX_BINS];

    /* the measured move, columns (288 bins, +-16) and rows (96 bins, +-8) */
    for (int seed = 1; seed <= 5; seed++)
    {
        make_scene(seed);
        for (double sh = -12; sh <= 12; sh += 0.75)
        {
            profile(a, 288, 0, 1.0, 0);
            profile(b, 288, sh, 1.0, 0);
            int m = stab_match(b, a, 288, 16);
            CHECK(fabs(m / 16.0 - sh) <= 0.35, "cols seed %d: moved %.2f, measured %.2f", seed, sh, m / 16.0);
        }
        for (double sh = -6; sh <= 6; sh += 0.5)
        {
            profile(a, 96, 0, 1.0, 0);
            profile(b, 96, sh, 1.0, 0);
            int m = stab_match(b, a, 96, 8);
            CHECK(fabs(m / 16.0 - sh) <= 0.35, "rows seed %d: moved %.2f, measured %.2f", seed, sh, m / 16.0);
        }
        /* a brighter frame (clouds, auto exposure) and sensor noise must not look like a move */
        profile(a, 288, 0, 1.0, 0);
        profile(b, 288, 3, 1.05, 400);
        int m = stab_match(b, a, 288, 16);
        CHECK(fabs(m / 16.0 - 3) <= 0.5, "brighter + noise: moved 3, measured %.2f", m / 16.0);
        profile(b, 288, 0, 1.08, 400);
        m = stab_match(b, a, 288, 16);
        CHECK(abs(m) <= 8, "brighter, still: measured %.2f", m / 16.0);
    }
    /* too short to compare: no move */
    CHECK(stab_match(b, a, 20, 8) == 0, "short profile");

    /* rounding to the compressor's steps */
    CHECK(stab_round_to(11, 8) == 8 && stab_round_to(12, 8) == 16 && stab_round_to(-11, 8) == -8 &&
          stab_round_to(-12, 8) == -16 && stab_round_to(3, 2) == 4 && stab_round_to(0, 8) == 0, "round");

    /* the window: follows the scene in steps, stops at the margin, drifts back to the centre */
    struct stab_state st = {0};
    stab_update(&st, 20 * 16, 6 * 16, 176, 36, 8, 2, 0);
    CHECK(st.off_x == 24 && st.off_y == 6, "follow: %d %d", st.off_x, st.off_y);
    stab_update(&st, -20 * 16, -6 * 16, 176, 36, 8, 2, 0);
    CHECK(st.off_x == 0 && st.off_y == 0, "back: %d %d", st.off_x, st.off_y);
    for (int i = 0; i < 50; i++)
        stab_update(&st, 40 * 16, 40 * 16, 176, 36, 8, 2, 0);
    CHECK(st.off_x <= 176 && st.off_x >= 168 && st.off_y <= 36 && st.off_y >= 34, "margin: %d %d", st.off_x, st.off_y);
    CHECK(st.off_x % 8 == 0 && st.off_y % 2 == 0, "aligned: %d %d", st.off_x, st.off_y);
    stab_update(&st, -10 * 16, 0, 176, 36, 8, 2, 0);
    CHECK(st.off_x < 176, "no wind-up past the margin: %d", st.off_x);
    struct stab_state lk = {0};
    stab_update(&lk, 100 * 16, 0, 176, 36, 8, 2, 48);
    for (int i = 0; i < 18 * 10; i++)
        stab_update(&lk, 0, 0, 176, 36, 8, 2, 48);
    CHECK(lk.off_x == 0, "after 10 s still the window is back in the centre: %d", lk.off_x);

    /* a whole run: hand shake on a still landscape, tracked against a reference, window placed */
    for (int scene_seed = 9; scene_seed <= 12; scene_seed++)
    {
        make_scene(scene_seed);
        static int32_t ref_c[STAB_MAX_BINS], ref_r[STAB_MAX_BINS], cur_c[STAB_MAX_BINS], cur_r[STAB_MAX_BINS];
        struct stab_tracker tr = {0};
        struct stab_state run = {0};
        double worst_before = 0, worst_after = 0, sum_after = 0;
        int frames = 18 * 10;
        for (int f = 0; f < frames; f++)
        {
            /* sway (slow) + tremor (fast), in column bins; rows the same, smaller */
            double cam_x = 6.0 * sin(f * 0.21) + 3.0 * sin(f * 0.9) + 1.5 * sin(f * 2.3);
            double cam_y = 1.5 * sin(f * 0.17) + 0.8 * sin(f * 1.1);
            profile(cur_c, 288, cam_x, 1.0, 200);
            for (int j = 0; j < 96; j++)
                cur_r[j] = (int32_t)(scene_at(900 + j - cam_y) + rand() % 200 - 100);
            int dx16, dy16;
            stab_track(&tr, ref_c, ref_r, cur_c, cur_r, 288, 96, 8, 4, 30, 8, &dx16, &dy16);
            /* bins x16 -> raw pixels x16: 2 LV px per column bin, 2.4 raw px per LV px */
            stab_update(&run, dx16 * 2 * 24 / 10, dy16 * 4 * 145 / 100, 176, 36, 8, 2, 96);
            double scene_raw = cam_x * 2 * 2.4;
            double err = fabs(scene_raw - run.off_x);
            worst_before = fmax(worst_before, fabs(scene_raw));
            if (f > 18)
            {
                worst_after = fmax(worst_after, err);
                sum_after += err;
            }
        }
        double mean_after = sum_after / (frames - 19);
        printf("hand shake run %d: worst %.1f raw px without, %.1f with (mean %.1f)\n",
               scene_seed, worst_before, worst_after, mean_after);
        /* the window keeps drifting back to the centre (leak, as in mlv_lite), so it lags slow sway a bit */
        CHECK(mean_after < worst_before / 3, "run %d: the tripod mode must cut the shake a lot (%.1f -> mean %.1f)",
              scene_seed, worst_before, mean_after);
    }

    printf("stab_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

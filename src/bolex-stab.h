/** \file
 * Bolex tripod mode: the arithmetic of the in-camera stabilizer (mlv_lite.c does the rest).
 *
 * Each frame, mlv_lite sums the Live View luma along columns and along rows (two short
 * "profiles").  Comparing a profile with the previous frame's tells how far the picture moved
 * sideways and up/down.  The moves add up to where the scene is now; the recorded window
 * (a little smaller than the sensor's picture) is placed there, so it stays on the scene.
 * The sum slowly returns to zero, so a deliberate reframing is followed after a few seconds,
 * and it never goes past the margin around the window.
 *
 * Only translation: rotation and rolling shutter wobble stay.  Header-only (static), so the
 * module has its own copy and tests/bolex_tests.c compiles the same code on a computer.
 */
#ifndef _BOLEX_STAB_H_
#define _BOLEX_STAB_H_

#define STAB_MAX_BINS   400     /* profile length limit (Live View is 720 wide, binned by 2) */
#define STAB_MAX_SHIFT  32      /* search range limit, in bins */

/* v rounded to the nearest multiple of 'step' (also for negative v) */
static inline int stab_round_to(int v, int step)
{
    return v >= 0 ? (v + step / 2) / step * step : -((-v + step / 2) / step * step);
}

/* How far 'cur' moved against 'prev' (both n values long): cur[i] ~ prev[i - shift].
 * The shift is searched within centre +- smax bins (smax at most STAB_MAX_SHIFT); the result
 * is in 1/16 bin.  The profiles are compared by their slopes (differences between
 * neighbouring bins), so a change of brightness between the frames does not count as a move.
 * Returns 'centre' (x16) when there is too little picture to compare. */
static inline int stab_match_around(const int32_t * cur, const int32_t * prev, int n, int centre, int smax)
{
    if (smax > STAB_MAX_SHIFT)
        smax = STAB_MAX_SHIFT;
    int lo = centre - smax, hi = centre + smax;
    if (n < 8 || (lo < 0 ? -lo : lo) * 3 > n || (hi < 0 ? -hi : hi) * 3 > n)
        return centre * 16;

    /* mean absolute difference of the slopes for each shift, over the part both cover */
    int64_t score[2 * STAB_MAX_SHIFT + 1];
    int best = centre;
    for (int s = lo; s <= hi; s++)
    {
        int64_t sad = 0;
        int count = 0;
        int i0 = (s > 0 ? s : 0) + 1;
        int i1 = s < 0 ? n + s : n;
        for (int i = i0; i < i1; i++)
        {
            int64_t dc = (int64_t) cur[i] - cur[i - 1];
            int64_t dp = (int64_t) prev[i - s] - prev[i - s - 1];
            int64_t d = dc - dp;
            sad += d < 0 ? -d : d;
            count++;
        }
        score[s - lo] = count ? sad * 1024 / count : ((int64_t) 1 << 62);
        if (score[s - lo] < score[best - lo])
            best = s;
    }

    /* a parabola through the best score and its neighbours gives the fraction */
    int frac16 = 0;
    if (best > lo && best < hi)
    {
        int64_t a = score[best - 1 - lo], b = score[best - lo], c = score[best + 1 - lo];
        int64_t den = a - 2 * b + c;
        if (den > 0)
        {
            frac16 = (int)(8 * (a - c) / den);
            if (frac16 > 8) frac16 = 8;
            if (frac16 < -8) frac16 = -8;
        }
    }
    return best * 16 + frac16;
}

/* the same, searched around no move */
static inline int stab_match(const int32_t * cur, const int32_t * prev, int n, int smax)
{
    if (n < 4 * smax + 8)
        return 0;
    return stab_match_around(cur, prev, n, 0, smax);
}

/* Where the picture is against a reference frame (not just against the previous frame: those
 * small errors would add up).  Each frame is compared with the reference, searching around
 * where it was last time; when the picture has gone too far from the reference, the current
 * frame becomes the new reference.  The caller owns the reference profiles.
 * Gives the move since the previous frame (x16 bins), to feed stab_update(). */
struct stab_tracker
{
    int have_ref;
    int last_x16, last_y16;     /* position against the reference, x16 bins */
};

static inline void stab_track(struct stab_tracker * t,
                              int32_t * ref_col, int32_t * ref_row,
                              const int32_t * col, const int32_t * row, int nc, int nr,
                              int search_x, int search_y, int rekey_x, int rekey_y,
                              int * dx16, int * dy16)
{
    *dx16 = *dy16 = 0;
    if (!t->have_ref)
    {
        for (int i = 0; i < nc; i++) ref_col[i] = col[i];
        for (int i = 0; i < nr; i++) ref_row[i] = row[i];
        t->have_ref = 1;
        t->last_x16 = t->last_y16 = 0;
        return;
    }

    int cx = stab_round_to(t->last_x16, 16) / 16;
    int cy = stab_round_to(t->last_y16, 16) / 16;
    int x16 = stab_match_around(col, ref_col, nc, cx, search_x);
    int y16 = stab_match_around(row, ref_row, nr, cy, search_y);
    *dx16 = x16 - t->last_x16;
    *dy16 = y16 - t->last_y16;
    t->last_x16 = x16;
    t->last_y16 = y16;

    /* too far from the reference: start again from here */
    if ((x16 < 0 ? -x16 : x16) > rekey_x * 16 || (y16 < 0 ? -y16 : y16) > rekey_y * 16)
    {
        for (int i = 0; i < nc; i++) ref_col[i] = col[i];
        for (int i = 0; i < nr; i++) ref_row[i] = row[i];
        t->last_x16 = t->last_y16 = 0;
    }
}

struct stab_state
{
    int acc_x16, acc_y16;       /* where the scene is, relative to the centre (raw pixels x16) */
    int off_x, off_y;           /* the window offset in use (raw pixels, aligned) */
};

/* One frame: the scene moved by (dx16, dy16) raw pixels x16.  The window may move up to
 * +-max_x / +-max_y pixels; it moves in steps of step_x / step_y (the compressor's alignment).
 * leak: the sum loses 1/leak of itself per frame (returns to the centre); 0 = never. */
static inline void stab_update(struct stab_state * st, int dx16, int dy16,
                               int max_x, int max_y, int step_x, int step_y, int leak)
{
    st->acc_x16 += dx16;
    st->acc_y16 += dy16;

    if (leak > 0)
    {
        st->acc_x16 -= st->acc_x16 / leak;
        st->acc_y16 -= st->acc_y16 / leak;
    }

    /* never wind up past the margin: at the edge the window simply stops */
    int lim_x = max_x * 16, lim_y = max_y * 16;
    if (st->acc_x16 >  lim_x) st->acc_x16 =  lim_x;
    if (st->acc_x16 < -lim_x) st->acc_x16 = -lim_x;
    if (st->acc_y16 >  lim_y) st->acc_y16 =  lim_y;
    if (st->acc_y16 < -lim_y) st->acc_y16 = -lim_y;

    int ox = stab_round_to(st->acc_x16 / 16, step_x);
    int oy = stab_round_to(st->acc_y16 / 16, step_y);
    if (ox >  max_x) ox -= step_x;
    if (ox < -max_x) ox += step_x;
    if (oy >  max_y) oy -= step_y;
    if (oy < -max_y) oy += step_y;
    st->off_x = ox;
    st->off_y = oy;
}

#endif

#!/bin/sh
# Runs the computer-side tests.  Needs only gcc and python3.   Usage:  sh tests/run.sh
# Nothing here touches the camera build; it only compiles copies of the pure logic.
set -e
cd "$(dirname "$0")/.."
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
CF="-std=gnu99 -Wall -Wextra -Werror -Isrc -I$OUT"
CR=modules/crop_rec/crop_rec.c

# 1. copy the real pieces out of crop_rec.c
{
  python3 tests/extract.py $CR block    "static const struct setting_range crop_settings[]"
  python3 tests/extract.py $CR line     "#define CROP_SETTINGS_VERSION"
  python3 tests/extract.py $CR function crop_settings_load
  python3 tests/extract.py $CR function slim_film_frame_get
  python3 tests/extract.py $CR function slim_film_frame_set
} > "$OUT/crop_rec_snippets.h"
# the Film Format / Standard menu logic (tests/menu_tests.c)
{
  python3 tests/extract.py $CR function slim_film_frame_get
  python3 tests/extract.py $CR function slim_film_frame_set
  python3 tests/extract.py $CR function slim_video_standard
  python3 tests/extract.py $CR function slim_film_menu_readout
  python3 tests/extract.py $CR function slim_film_sync
  python3 tests/extract.py $CR function slim_1x1_resolve
  python3 tests/extract.py $CR function slim_film_frame_height
  python3 tests/extract.py $CR function slim_line_clocks
  python3 tests/extract.py $CR function slim_readout_x10
  python3 tests/extract.py $CR function slim_crop_fps_mask
} > "$OUT/crop_rec_menu_snippets.h"
python3 tests/extract.py $CR function slim_film_apply > "$OUT/crop_rec_apply_snippet.h"
# the Bolex shutter and its correction (tests/bolex_tests.c)
{
  python3 tests/extract.py $CR line     "#define BOLEX_FPS_X1000"
  python3 tests/extract.py $CR line     "#define BOLEX_PLAY_FPS"
  python3 tests/extract.py $CR line     "#define BOLEX_ANGLE"
  python3 tests/extract.py $CR line     "#define BOLEX_SHUTTER_X1000"
  python3 tests/extract.py $CR line     "#define BOLEX_FLICKER_X1000"
  python3 tests/extract.py $CR line     "#define BOLEX_FLICKER_EVERY"
  python3 tests/extract.py $CR line     "#define BOLEX_BREATH_MIN_X1000"
  python3 tests/extract.py $CR line     "#define BOLEX_BREATH_MAX_X1000"
  python3 tests/extract.py $CR line     "#define BOLEX_BREATH_LEN_MIN_MS"
  python3 tests/extract.py $CR line     "#define BOLEX_BREATH_LEN_MAX_MS"
  python3 tests/extract.py $CR line     "#define BOLEX_BREATH_EVERY_MIN_MS"
  python3 tests/extract.py $CR line     "#define BOLEX_BREATH_EVERY_MAX_MS"
  python3 tests/extract.py $CR function bolex_rand
  python3 tests/extract.py $CR function bolex_rand_in
  python3 tests/extract.py $CR function bolex_pow2_x1000
  python3 tests/extract.py $CR line     "struct bolex_flicker {"
  python3 tests/extract.py $CR function bolex_flicker_start
  python3 tests/extract.py $CR function bolex_flicker_step
  python3 tests/extract.py $CR function bolex_blanking
  python3 tests/extract.py $CR line     "#define BOLEX_RO_1620"
  python3 tests/extract.py $CR line     "#define BOLEX_RO_28K"
  python3 tests/extract.py $CR line     "#define BOLEX_RO_976"
  python3 tests/extract.py $CR line     "#define BOLEX_MODE_1620"
  python3 tests/extract.py $CR line     "#define BOLEX_MODE_166"
  python3 tests/extract.py $CR line     "#define BOLEX_MODE_185"
  python3 tests/extract.py $CR line     "#define BOLEX_MODE_976"
  python3 tests/extract.py $CR line     "#define BOLEX_MODE_SENA"
  python3 tests/extract.py $CR line     "#define BOLEX_MODES"
  python3 tests/extract.py $CR line     "struct bolex_mode {"
  python3 tests/extract.py $CR block    "static const struct bolex_mode bolex_modes[BOLEX_MODES]"
  python3 tests/extract.py $CR line     "static const int sena_fps[]"
  python3 tests/extract.py $CR line     "#define SENA_HOLD_MIN_MS"
  python3 tests/extract.py $CR line     "#define SENA_HOLD_MAX_MS"
  python3 tests/extract.py $CR line     "#define BOLEX_WANDER_HOLD_MIN_MS"
  python3 tests/extract.py $CR line     "#define BOLEX_WANDER_HOLD_MAX_MS"
  python3 tests/extract.py $CR line     "#define BOLEX_WANDER_TAU_MS"
  python3 tests/extract.py $CR function bolex_timer_b_for
  python3 tests/extract.py $CR line     "struct bolex_wander {"
  python3 tests/extract.py $CR function bolex_wander_start
  python3 tests/extract.py $CR function bolex_wander_step
  python3 tests/extract.py $CR function sena_next
  python3 tests/extract.py $CR line     "static const int bolex_isos[]"
  python3 tests/extract.py $CR function bolex_iso_step
} > "$OUT/bolex_snippets.h"
{
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_RES_X"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c function calc_res_y
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_KELVIN_BASE"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_KELVIN_JITTER"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_EV_JITTER_X1000"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_WEAVE_X_X100"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_WEAVE_Y_X100"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c function bolex_hash
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c function bolex_jitter
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "struct bolex_int {"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c function bolex_int_next
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_DROP_PERMIL"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c function bolex_drop_frame
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_TRIM_MS"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define BOLEX_HEAD_MS"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c function bolex_trim_count
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define SENA_DROP_START_PERMIL"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c function sena_drop_frame
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define SENA_KELVIN_MIN"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c line     "#define SENA_KELVIN_MAX"
  python3 tests/extract.py modules/mlv_lite/mlv_lite.c function sena_kelvin
} > "$OUT/bolex_mlv_snippets.h"

# 2. structure check: the film table exists once, in src/film-formats.h only
if grep -n '"Super 8 Actual"\|"A35 Anamorphic 2x"' modules/crop_rec/crop_rec.c modules/mlv_lite/mlv_lite.c; then
  echo "FAIL: a film table is duplicated in a module"; exit 1
fi

# 3. build and run
for t in film settings menu gain ltc bolex; do
  echo "== $t"
  gcc $CF -o "$OUT/$t" tests/${t}_tests.c -lm
  "$OUT/$t"
done
echo "ALL TESTS PASSED"

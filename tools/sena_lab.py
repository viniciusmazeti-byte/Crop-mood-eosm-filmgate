#!/usr/bin/env python3
"""Sena lab: the colour temperature of every frame, as the camera drew it.

The Sena mode (EOS M, branch "sena") draws a colour temperature between 2000 K and
6000 K for every frame and writes it next to the clip (M31-0705.MLV -> M31-0705.KEL,
"frame kelvin" per line).  The RAW keeps no colour temperature and MLV App uses one
per clip, so:

  1. In MLV App, develop the clip at 5500 K (the white balance the camera writes),
     with all its other settings as you like, and export it (ProRes, 24 fps).
  2. Put the .KEL from the card next to the exported .mov (same name).
  3. Run:  python sena_lab.py M31-0705.mov      (or drag the .mov onto sena_lab.bat)
     -> M31-0705_sena.mov, ProRes 422 HQ, each frame at its own colour temperature.

A frame drawn at 2000 K comes out deep blue (as if the white balance were set to
2000 K under daylight), one at 6000 K slightly warm.  Brightness is kept.

Needs: Python 3, numpy (pip install numpy) and ffmpeg / ffprobe on the PATH.
"""
import os
import subprocess
import sys

import numpy as np

BASE_K = 5500


def planck_xy(k):
    """CIE 1931 xy of a black body at k kelvin (Kim et al. cubic spline, 1667..25000 K)."""
    k = float(min(max(k, 1667), 25000))
    if k <= 4000:
        x = -0.2661239e9 / k**3 - 0.2343589e6 / k**2 + 0.8776956e3 / k + 0.179910
    else:
        x = -3.0258469e9 / k**3 + 2.1070379e6 / k**2 + 0.2226347e3 / k + 0.240390
    if k <= 2222:
        y = -1.1063814 * x**3 - 1.34811020 * x**2 + 2.18555832 * x - 0.20219683
    elif k <= 4000:
        y = -0.9549476 * x**3 - 1.37418593 * x**2 + 2.09137015 * x - 0.16748867
    else:
        y = 3.0817580 * x**3 - 5.87338670 * x**2 + 3.75112997 * x - 0.37001483
    return x, y


def white_rgb(k):
    """Linear Rec.709 RGB of a black body at k kelvin (Y = 1)."""
    x, y = planck_xy(k)
    X, Y, Z = x / y, 1.0, (1 - x - y) / y
    m = np.array([[3.2404542, -1.5371385, -0.4985314],
                  [-0.9692660, 1.8760108, 0.0415560],
                  [0.0556434, -0.2040259, 1.0572252]])
    return m @ np.array([X, Y, Z])


def gains(k, base=BASE_K):
    """RGB gains turning a frame developed at base K into one 'developed' at k K."""
    g = white_rgb(base) / white_rgb(k)
    return g / (0.2126 * g[0] + 0.7152 * g[1] + 0.0722 * g[2])


def read_kelvins(path):
    ks = {}
    with open(path) as f:
        for line in f:
            line = line.split('#')[0].split()
            if len(line) >= 2:
                ks[int(line[0])] = int(line[1])
    if not ks:
        raise SystemExit('no frames in ' + path)
    return [ks.get(i, ks[max(k for k in ks if k <= i)] if any(k <= i for k in ks) else ks[min(ks)])
            for i in range(max(ks) + 1)]


def probe(path):
    out = subprocess.check_output(
        ['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
         'stream=width,height,r_frame_rate', '-of', 'csv=p=0', path], text=True)
    w, h, r = out.strip().split(',')[:3]
    return int(w), int(h), r


def run(mov, kel=None, out=None):
    kel = kel or os.path.splitext(mov)[0] + '.KEL'
    if not os.path.exists(kel):
        alt = os.path.splitext(mov)[0] + '.kel'
        kel = alt if os.path.exists(alt) else kel
    if not os.path.exists(kel):
        raise SystemExit('missing ' + kel + ' (copy it from the card, next to the .mov)')
    out = out or os.path.splitext(mov)[0] + '_sena.mov'
    ks = read_kelvins(kel)
    w, h, rate = probe(mov)

    dec = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', mov, '-f', 'rawvideo',
                            '-pix_fmt', 'rgb48le', '-'], stdout=subprocess.PIPE)
    enc = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb48le',
                            '-s', '%dx%d' % (w, h), '-r', rate, '-i', '-',
                            '-c:v', 'prores_ks', '-profile:v', '3', '-pix_fmt', 'yuv422p10le',
                            '-vendor', 'apl0', out], stdin=subprocess.PIPE)
    size = w * h * 3 * 2
    i = 0
    while True:
        buf = dec.stdout.read(size)
        if len(buf) < size:
            break
        img = np.frombuffer(buf, np.uint16).reshape(h, w, 3).astype(np.float32) / 65535
        lin = np.power(img, 2.4)                               # Rec.709 display -> linear
        lin *= gains(ks[min(i, len(ks) - 1)]).astype(np.float32)
        img = np.power(np.clip(lin, 0, 1), 1 / 2.4)
        enc.stdin.write((img * 65535 + 0.5).astype(np.uint16).tobytes())
        i += 1
        if i % 50 == 0:
            print('%d frames' % i, flush=True)
    enc.stdin.close()
    enc.wait()
    dec.wait()
    if i != len(ks):
        print('note: the clip has %d frames, the .KEL %d' % (i, len(ks)))
    print('done: ' + out)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for path in sys.argv[1:]:
        run(path)

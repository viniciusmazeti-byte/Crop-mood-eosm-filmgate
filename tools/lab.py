#!/usr/bin/env python3
"""Lab: the frame-by-frame intermittency the camera drew, applied to a developed clip.

The Bolex / Sena builds (EOS M) write next to each clip a list with one line per frame
(M31-0705.MLV -> M31-0705.KEL):

    frame kelvin [ev dx dy]

  kelvin  the colour temperature of that frame
  ev      exposure change in stops (flicker), optional
  dx, dy  the frame's offset in pixels of the recorded size (gate weave), optional

The RAW keeps none of this (MLV App uses one white balance per clip), so:

  1. In MLV App, develop the clip at 5500 K (the white balance the camera writes),
     everything else as you like, and export it (ProRes).
  2. Put the .KEL from the card next to the exported .mov (same name).
  3. Run:  python lab.py M31-0705.mov      (or drag the .mov onto lab.bat)
     -> M31-0705_lab.mov, ProRes 422 HQ.

     Stronger or weaker intermittency:  python lab.py --forca 2 M31-0705.mov
     (2 = twice the colour, flicker and weave the camera drew; 0.5 = half; 0 = none)

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


def read_list(path):
    """{frame: (kelvin, ev, dx, dy)} and the recorded size (w, h) or None."""
    rows, size = {}, None
    with open(path) as f:
        for line in f:
            if line.startswith('# size'):
                w, h = line.split()[2].split('x')
                size = (int(w), int(h))
            parts = line.split('#')[0].split()
            if len(parts) >= 2:
                vals = [float(v) for v in parts[1:5]] + [0.0] * (5 - len(parts))
                rows[int(parts[0])] = tuple(vals[:4])
    if not rows:
        raise SystemExit('no frames in ' + path)
    last = max(rows)
    out, cur = [], rows[min(rows)]
    for i in range(last + 1):
        cur = rows.get(i, cur)
        out.append(cur)
    return out, size


def shift(img, dx, dy):
    """Move the picture by (dx, dy) pixels, sub-pixel, the edges repeated."""
    if abs(dx) < 1e-3 and abs(dy) < 1e-3:
        return img
    pad = int(np.ceil(max(abs(dx), abs(dy)))) + 1
    p = np.pad(img, ((pad, pad), (pad, pad), (0, 0)), mode='edge')
    h, w = img.shape[:2]
    ix, iy = int(np.floor(-dx)), int(np.floor(-dy))
    fx, fy = -dx - ix, -dy - iy
    def win(ox, oy):
        return p[pad + iy + oy: pad + iy + oy + h, pad + ix + ox: pad + ix + ox + w]
    top = win(0, 0) * (1 - fx) + win(1, 0) * fx
    bot = win(0, 1) * (1 - fx) + win(1, 1) * fx
    return top * (1 - fy) + bot * fy


def probe(path):
    out = subprocess.check_output(
        ['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
         'stream=width,height,r_frame_rate', '-of', 'csv=p=0', path], text=True)
    w, h, r = out.strip().split(',')[:3]
    return int(w), int(h), r


def run(mov, force=1.0):
    kel = os.path.splitext(mov)[0] + '.KEL'
    if not os.path.exists(kel) and os.path.exists(kel[:-4] + '.kel'):
        kel = kel[:-4] + '.kel'
    if not os.path.exists(kel):
        raise SystemExit('missing ' + kel + ' (copy it from the card, next to the .mov)')
    out = os.path.splitext(mov)[0] + '_lab.mov'
    rows, size = read_list(kel)
    w, h, rate = probe(mov)
    scale = (w / size[0]) if size else 1.0      # the offsets are in recorded pixels

    dec = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', mov, '-f', 'rawvideo',
                            '-pix_fmt', 'rgb48le', '-'], stdout=subprocess.PIPE)
    enc = subprocess.Popen(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb48le',
                            '-s', '%dx%d' % (w, h), '-r', rate, '-i', '-',
                            '-c:v', 'prores_ks', '-profile:v', '3', '-pix_fmt', 'yuv422p10le',
                            '-vendor', 'apl0', out], stdin=subprocess.PIPE)
    size_b = w * h * 3 * 2
    i = 0
    while True:
        buf = dec.stdout.read(size_b)
        if len(buf) < size_b:
            break
        k, ev, dx, dy = rows[min(i, len(rows) - 1)]
        k, ev, dx, dy = BASE_K + (k - BASE_K) * force, ev * force, dx * force, dy * force
        img = np.frombuffer(buf, np.uint16).reshape(h, w, 3).astype(np.float32) / 65535
        lin = np.power(img, 2.4)                                     # Rec.709 display -> linear
        lin *= (gains(k) * 2.0 ** ev).astype(np.float32)
        img = np.power(np.clip(lin, 0, 1), 1 / 2.4)
        img = shift(img, dx * scale, dy * scale)
        enc.stdin.write((np.clip(img, 0, 1) * 65535 + 0.5).astype(np.uint16).tobytes())
        i += 1
        if i % 50 == 0:
            print('%d frames' % i, flush=True)
    enc.stdin.close()
    enc.wait()
    dec.wait()
    if i != len(rows):
        print('note: the clip has %d frames, the .KEL %d' % (i, len(rows)))
    print('done: ' + out)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    args, force = sys.argv[1:], 1.0
    if args and args[0] == '--forca':
        force, args = float(args[1]), args[2:]
    for path in args:
        run(path, force)

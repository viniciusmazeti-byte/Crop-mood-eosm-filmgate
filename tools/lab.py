#!/usr/bin/env python3
"""Lab: the frame-by-frame intermittency the camera drew, applied to a developed clip.

The Bolex / Sena builds (EOS M) write next to each clip a list with one line per frame
(M31-0705.MLV -> M31-0705.KEL):

    frame kelvin [ev dx dy]

  kelvin  the colour temperature of that frame
  ev      exposure change in stops (flicker), optional
  dx, dy  the frame's offset in pixels of the recorded size (gate weave), optional

The RAW keeps none of this (MLV App uses one white balance per clip), so:

  1. In MLV App, develop the clip at the white balance the camera wrote (4500 K; the .KEL says),
     everything else as you like, and export it (ProRes).
  2. Put the .KEL from the card next to the exported .mov (same name).
  3. Drag the .mov onto lab.bat  -> M31-0705_lab.mov next to it, ProRes 422 HQ.
     Or put the .mov and .KEL files in the "entrada" folder and double-click lab.bat
     -> the results go to "resultado".

     Stronger or weaker intermittency:  python lab.py --forca 2 M31-0705.mov
     (2 = twice the colour, flicker and weave the camera drew; 0.5 = half; 0 = none)

Needs only Python 3: numpy is installed and ffmpeg downloaded on the first run if missing.
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

BASE_K = 5500
HERE = os.path.dirname(os.path.abspath(__file__))
ENTRADA = os.path.join(HERE, 'entrada')
RESULTADO = os.path.join(HERE, 'resultado')
FFMPEG_ZIP = ('https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/'
              'ffmpeg-master-latest-win64-gpl.zip')
EXE = '.exe' if os.name == 'nt' else ''


def pare(msg):
    print()
    print('ERRO: ' + msg)
    sys.exit(1)


# ---------------------------------------------------------------- numpy, installed if missing
try:
    import numpy as np
except ImportError:
    print('[numpy] nao instalado - instalando (so na primeira vez)...')
    subprocess.call([sys.executable, '-m', 'ensurepip', '--user'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.call([sys.executable, '-m', 'pip', 'install', '--user', '--quiet', 'numpy'])
    import site, importlib
    importlib.reload(site)
    try:
        import numpy as np
    except ImportError:
        pare('nao consegui instalar o numpy. Abra o Prompt de Comando e rode:\n'
             '       python -m pip install numpy')


# ---------------------------------------------------------------- ffmpeg, downloaded if missing
def works(exe):
    try:
        return subprocess.run([exe, '-version'], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=15).returncode == 0
    except Exception:
        return False


def download(url, dest):
    """Python first; if its certificates fail (common on Windows), Windows' own curl or PowerShell."""
    try:
        def hook(b, bs, ts):
            if ts > 0:
                print('\r[ffmpeg] %d%%' % min(100, b * bs * 100 // ts), end='', flush=True)
        urllib.request.urlretrieve(url, dest, reporthook=hook)
        print()
        return True
    except Exception as e:
        print('\n[ffmpeg] download pelo Python falhou (%s), tentando pelo Windows...' % e)
    for cmd in (['curl.exe', '-L', '--fail', '-o', dest, url],
                ['powershell', '-NoProfile', '-Command',
                 "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '%s' -OutFile '%s'" % (url, dest)]):
        try:
            if subprocess.call(cmd) == 0 and os.path.getsize(dest) > 1000000:
                return True
        except Exception:
            pass
    return False


def get_ffmpeg():
    local = os.path.join(HERE, 'ffmpeg_local', 'ffmpeg' + EXE)
    places = [local, os.path.join(HERE, 'ffmpeg' + EXE)]
    places += sorted(glob.glob(os.path.join(HERE, '**', 'ffmpeg' + EXE), recursive=True))
    if shutil.which('ffmpeg'):
        places.append(shutil.which('ffmpeg'))
    if os.name == 'nt':
        for d in ('C:\\', 'D:\\'):
            places += [d + r'ffmpeg\bin\ffmpeg.exe', d + r'Program Files\ffmpeg\bin\ffmpeg.exe',
                       d + r'ProgramData\chocolatey\bin\ffmpeg.exe']
    for p in places:
        if os.path.isfile(p) and works(p):
            return p
    if os.name != 'nt':
        pare('instale o ffmpeg')
    print('[ffmpeg] nao encontrado - baixando (so na primeira vez, ~200 MB)...')
    tmp = os.path.join(tempfile.gettempdir(), 'ffmpeg_lab.zip')
    if not download(FFMPEG_ZIP, tmp):
        pare('nao consegui baixar o ffmpeg. Baixe manualmente:\n       ' + FFMPEG_ZIP +
             '\n       e descompacte dentro de ' + HERE)
    print('[ffmpeg] descompactando...')
    os.makedirs(os.path.dirname(local), exist_ok=True)
    with zipfile.ZipFile(tmp) as z:
        member = [m for m in z.namelist() if m.endswith('/bin/ffmpeg.exe')]
        if not member:
            pare('o zip do ffmpeg veio sem o ffmpeg.exe')
        with z.open(member[0]) as src, open(local, 'wb') as dst:
            shutil.copyfileobj(src, dst)
    try:
        os.unlink(tmp)
    except OSError:
        pass
    if not works(local):
        pare('o ffmpeg baixado nao abriu: ' + local)
    print('[ffmpeg] instalado em ' + local)
    return local


FFMPEG = None


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
    """[(kelvin, ev, dx, dy) per frame], the recorded size (w, h) or None, the base kelvin."""
    rows, size, base = {}, None, BASE_K
    with open(path) as f:
        for line in f:
            m = re.match(r'# develop the clip at (\d+) K', line)
            if m:
                base = int(m.group(1))
            if line.startswith('# size'):
                w, h = line.split()[2].split('x')
                size = (int(w), int(h))
            parts = line.split('#')[0].split()
            if len(parts) >= 2:
                vals = [float(v) for v in parts[1:5]] + [0.0] * (5 - len(parts))
                rows[int(parts[0])] = tuple(vals[:4])
    if not rows:
        pare('o .KEL esta vazio: ' + path)
    last = max(rows)
    out, cur = [], rows[min(rows)]
    for i in range(last + 1):
        cur = rows.get(i, cur)
        out.append(cur)
    return out, size, base


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
    """Width, height and frame rate, read from ffmpeg's own report (no ffprobe needed)."""
    r = subprocess.run([FFMPEG, '-hide_banner', '-i', path], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True, errors='replace')
    m = re.search(r'Video:.*?\b(\d{2,5})x(\d{2,5})\b', r.stderr)
    if not m:
        pare('nao consegui ler o video ' + path + '\n' + r.stderr[-400:])
    f = re.search(r'([\d.]+) fps', r.stderr) or re.search(r'([\d.]+) tbr', r.stderr)
    rate = f.group(1) if f else '24'
    rate = {'23.98': '24000/1001', '23.976': '24000/1001', '29.97': '30000/1001',
            '59.94': '60000/1001', '47.95': '48000/1001'}.get(rate, rate)
    return int(m.group(1)), int(m.group(2)), rate


def find_kel(mov):
    base = os.path.splitext(os.path.basename(mov))[0]
    for d in (os.path.dirname(os.path.abspath(mov)), ENTRADA, HERE):
        for ext in ('.KEL', '.kel'):
            p = os.path.join(d, base + ext)
            if os.path.exists(p):
                return p
    return None


def run(mov, out, force=1.0):
    kel = find_kel(mov)
    if not kel:
        print('PULEI %s: falta o .KEL com o mesmo nome (copie do cartao para junto do .mov)'
              % os.path.basename(mov))
        return False
    print()
    print('>> ' + os.path.basename(mov) + '  (+ ' + os.path.basename(kel) + ')')
    rows, size, base = read_list(kel)
    w, h, rate = probe(mov)
    scale = (w / size[0]) if size else 1.0      # the offsets are in recorded pixels

    ffmpeg = FFMPEG
    dec = subprocess.Popen([ffmpeg, '-v', 'error', '-i', mov, '-f', 'rawvideo',
                            '-pix_fmt', 'rgb48le', '-'], stdout=subprocess.PIPE)
    enc = subprocess.Popen([ffmpeg, '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb48le',
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
        k, ev, dx, dy = base + (k - base) * force, ev * force, dx * force, dy * force
        img = np.frombuffer(buf, np.uint16).reshape(h, w, 3).astype(np.float32) / 65535
        lin = np.power(img, 2.4)                                     # Rec.709 display -> linear
        lin *= (gains(k, base) * 2.0 ** ev).astype(np.float32)
        img = np.power(np.clip(lin, 0, 1), 1 / 2.4)
        img = shift(img, dx * scale, dy * scale)
        enc.stdin.write((np.clip(img, 0, 1) * 65535 + 0.5).astype(np.uint16).tobytes())
        i += 1
        if i % 50 == 0:
            print('\r   %d / %d quadros' % (i, len(rows)), end='', flush=True)
    enc.stdin.close()
    enc.wait()
    dec.wait()
    print()
    if enc.returncode != 0 or i == 0:
        print('FALHOU: ' + os.path.basename(mov))
        return False
    if i != len(rows):
        print('   (o clipe tem %d quadros, o .KEL %d)' % (i, len(rows)))
    print('   pronto: ' + out)
    return True


def main():
    global FFMPEG
    args, force = sys.argv[1:], 1.0
    if args and args[0] == '--forca':
        force, args = float(args[1].replace(',', '.')), args[2:]
    os.makedirs(ENTRADA, exist_ok=True)
    os.makedirs(RESULTADO, exist_ok=True)
    print('=' * 60)
    print('  LAB BOLEX - intermitencia quadro a quadro')
    print('=' * 60)
    FFMPEG = get_ffmpeg()
    print('ffmpeg: ' + FFMPEG)

    if args:                                    # dragged onto lab.bat: result next to each file
        jobs = [(p, os.path.splitext(p)[0] + '_lab.mov') for p in args]
    else:                                       # double-click: everything in entrada -> resultado
        movs = sorted(set(glob.glob(os.path.join(ENTRADA, '*.mov')) + glob.glob(os.path.join(ENTRADA, '*.MOV'))))
        if not movs:
            print()
            print('Nenhum .mov em ' + ENTRADA)
            print('Coloque ali os .mov exportados do MLV App (revelados no balanco de branco da camera)')
            print('junto com os .KEL do cartao, e abra o lab.bat de novo.')
            return
        jobs = [(p, os.path.join(RESULTADO, os.path.splitext(os.path.basename(p))[0] + '_lab.mov'))
                for p in movs]
    ok = sum(1 for mov, out in jobs if run(mov, out, force))
    print()
    print('=' * 60)
    print('  %d de %d clipe(s) prontos' % (ok, len(jobs)))
    print('=' * 60)


if __name__ == '__main__':
    try:
        main()
    except SystemExit:
        raise
    except Exception as e:
        import traceback
        traceback.print_exc()
        pare(str(e))

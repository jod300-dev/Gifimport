#!/usr/bin/env python3
"""
gmd_view.py - lee un .gmd / .gmd2 (nivel exportado de Geometry Dash) y lo visualiza.

  python3 gmd_view.py nivel.gmd                      # resumen: objetos por id, z-orders, triggers
  python3 gmd_view.py nivel.gmd -o vista.png         # dibuja los objetos 3092 del mod (escala/giro/color/z) y marca los triggers
  python3 gmd_view.py nivel.gmd -o vista.png --group 12        # solo los objetos del grupo 12 (un frame del GIF)
  python3 gmd_view.py nivel.gmd -o vista.png --zero-default 12 # simula que GD dibuja z=0 como "por defecto" (valor alto)
  python3 gmd_view.py nivel.gmd --dump objetos.txt   # guarda la cadena de objetos ya descomprimida

Que hace: el .gmd es un plist XML; el nivel esta en <k>k4</k><s>...</s> = base64 (url-safe) + gzip. Aqui se decodifica y se
parsea la cadena clave,valor;... Es una vista APROXIMADA del mod (cuadrados 3092 + marcas de triggers): no tiene las texturas de GD.
Dependencias: Python 3 y (solo para -o) Pillow:  pip install pillow
"""
import argparse, base64, colorsys, collections, gzip, math, re, sys, zlib


def read_level_string(path):
    raw = open(path, 'rb').read()
    txt = raw.decode('utf-8', 'replace')
    m = re.search(r'<k>k4</k>\s*<s>(.*?)</s>', txt, re.S)
    data = m.group(1).strip() if m else txt.strip()
    if data.startswith('kS') or data.startswith('1,'):         # ya viene sin comprimir
        return data
    b = data.replace('-', '+').replace('_', '/')
    b += '=' * (-len(b) % 4)
    blob = base64.b64decode(b)
    for wb in (47, -15, 15):                                    # gzip/zlib automatico, deflate crudo, zlib
        try:
            return zlib.decompress(blob, wb).decode('utf-8', 'replace')
        except Exception:
            pass
    return gzip.decompress(blob).decode('utf-8', 'replace')


def parse_objects(level):
    parts = level.split(';')
    header, objs = parts[0], []
    for item in parts[1:]:
        if not item.strip():
            continue
        p = item.split(',')
        d = {}
        for i in range(0, len(p) - 1, 2):
            d[p[i]] = p[i + 1]
        if '1' in d:
            objs.append(d)
    return header, objs


def f(d, k, default=0.0):
    try:
        return float(d.get(k, default))
    except ValueError:
        return default


def color_of(d, channels):
    hsv = d.get('43', '').split('a')
    if len(hsv) >= 3:
        h, s, v = float(hsv[0]), float(hsv[1]), float(hsv[2])
        r, g, b = colorsys.hsv_to_rgb((h % 360) / 360.0, min(max(s, 0), 1), min(max(v, 0), 1))
        return int(r * 255 + .5), int(g * 255 + .5), int(b * 255 + .5)
    ch = d.get('21')
    return channels.get(ch, (0, 0, 0))


def color_triggers(objs):
    ch = {}
    for d in objs:
        if d['1'] == '899' and '23' in d:
            ch[d['23']] = (int(f(d, '7')), int(f(d, '8')), int(f(d, '9')))
    return ch


def summary(objs):
    ids = collections.Counter(d['1'] for d in objs)
    print(f'{len(objs)} objetos')
    for k, v in ids.most_common(12):
        print(f'  id {k:>5}: {v}')
    z = collections.Counter(int(f(d, '25')) for d in objs if d['1'] == '3092')
    if z:
        print('z-orders de los 3092:', dict(sorted(z.items())[:20]), '...' if len(z) > 20 else '')
        if z.get(0):
            print('  AVISO: hay objetos con z-order 0 (GD lo trata como "por defecto" y puede dibujarlos encima de los z>=1)')
    for name, tid in (('Alpha', '1007'), ('Spawn', '1268')):
        sp = [d for d in objs if d['1'] == tid and '62' in d]          # spawn-triggered (los de frames)
        if not sp:
            continue
        xs = [f(d, '2') for d in sp]; ys = {round(f(d, '3'), 1) for d in sp}
        dup = [x for x, c in collections.Counter(round(x, 1) for x in xs).items() if c > 1]
        mono = all(b > a for a, b in zip(xs, xs[1:]))
        print(f'{name} (spawn-triggered): {len(sp)}  filas(y): {sorted(ys)}  x repetidas: {len(dup)}  x estrictamente creciente en orden de creacion: {mono}')
    start = [d for d in objs if d['1'] == '1268' and '62' not in d]
    if start:
        print('Spawn de arranque (posicion):', [(round(f(d, '2'), 1), round(f(d, '3'), 1)) for d in start])
    first_trig = next((i for i, d in enumerate(objs) if d['1'] in ('1007', '1268', '899')), None)
    last_obj = max((i for i, d in enumerate(objs) if d['1'] == '3092'), default=None)
    if first_trig is not None and last_obj is not None:
        print('triggers ANTES que los objetos:', first_trig < last_obj, '(asi un recorte por el limite de objetos no los pierde)')
    groups = collections.Counter(g for d in objs if d['1'] == '3092' for g in d.get('57', '').split('.') if g)
    if groups:
        print('grupos con objetos 3092:', len(groups), '(mayores:', groups.most_common(3), ')')


def draw(objs, out, group=None, zero_default=None, scale=None):
    from PIL import Image, ImageDraw
    chans = color_triggers(objs)
    sel = [d for d in objs if d['1'] == '3092' and (group is None or str(group) in d.get('57', '').split('.'))]
    if not sel:
        print('no hay objetos 3092 que dibujar'); return
    def z(d):
        v = int(f(d, '25'))
        return zero_default if (zero_default is not None and v == 0) else v
    order = sorted(range(len(sel)), key=lambda i: (z(sel[i]), i))     # empates: orden de creacion
    xs = [f(d, '2') for d in sel]; ys = [f(d, '3') for d in sel]
    ext = max(max(f(d, '128', 1), f(d, '129', 1)) for d in sel)
    x0, x1, y0, y1 = min(xs) - ext, max(xs) + ext, min(ys) - ext, max(ys) + ext
    s = scale or min(1600 / max(x1 - x0, 1), 1200 / max(y1 - y0, 1), 4.0)
    im = Image.new('RGB', (int((x1 - x0) * s) + 1, int((y1 - y0) * s) + 1), (255, 255, 255))
    dr = ImageDraw.Draw(im)
    for i in order:
        d = sel[i]
        x, y, w, h, rot = f(d, '2'), f(d, '3'), f(d, '128', 1), f(d, '129', 1), math.radians(f(d, '6'))
        cs, sn = math.cos(rot), math.sin(rot)
        pts = []
        for lx, ly in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
            gx, gy = lx * cs + ly * sn, -lx * sn + ly * cs        # giro horario, y hacia arriba
            pts.append(((x + gx - x0) * s, (y1 - (y + gy)) * s))
        dr.polygon(pts, fill=color_of(d, chans))
    for d in objs:                                                  # triggers: marca roja/azul (si estan dentro del recuadro)
        if d['1'] in ('1007', '1268'):
            tx, ty = (f(d, '2') - x0) * s, (y1 - f(d, '3')) * s
            if 0 <= tx < im.width and 0 <= ty < im.height:
                dr.rectangle([tx - 3, ty - 3, tx + 3, ty + 3], outline=(220, 0, 0) if d['1'] == '1007' else (0, 80, 220))
    im.save(out); print('guardado', out, im.size)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('gmd'); ap.add_argument('-o', '--out'); ap.add_argument('--group', type=int)
    ap.add_argument('--zero-default', type=int, help='simula z=0 como este z (GD lo trata como por defecto)')
    ap.add_argument('--scale', type=float); ap.add_argument('--dump')
    a = ap.parse_args()
    level = read_level_string(a.gmd)
    header, objs = parse_objects(level)
    if a.dump: open(a.dump, 'w').write(level); print('cadena guardada en', a.dump)
    summary(objs)
    if a.out: draw(objs, a.out, a.group, a.zero_default, a.scale)

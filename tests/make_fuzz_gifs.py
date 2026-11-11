# Genera GIFs "dificiles" para tests/fuzz_gifs.cpp (requiere Pillow y numpy):  python3 make_fuzz_gifs.py  -> escribe en ./fuzz_gifs/
import os, math, random
import numpy as np
from PIL import Image, ImageDraw
random.seed(1); np.random.seed(1)
os.makedirs('fuzz_gifs', exist_ok=True); os.chdir('fuzz_gifs')
def save(n, fr, dur=80, **kw): fr[0].save(n, save_all=True, append_images=fr[1:], duration=dur, loop=0, **kw)
def anim(w, h, n):
    out = []
    for i in range(n):
        im = Image.new('RGB', (w, h), (255, 255, 255)); d = ImageDraw.Draw(im); t = i / max(1, n)
        d.ellipse((w*(0.1+0.5*t), h*0.2, w*(0.1+0.5*t)+w*0.3, h*0.2+h*0.4), fill=(200, 40+int(150*t), 60))
        d.rectangle((0, int(h*0.8), w, h), fill=(30, 90, 200)); out.append(im)
    return out
P = lambda fr, c=64: [f.convert('P', palette=Image.ADAPTIVE, colors=c) for f in fr]
for name, (w, h, n, c) in {'tiny2x2': (2, 2, 3, 4), '1x64': (1, 64, 4, 8), '3x500': (3, 500, 4, 16), '800x4': (800, 4, 4, 16), '16x16': (16, 16, 6, 64),
                           '64x64_2f': (64, 64, 2, 64), '200x120_100f': (200, 120, 100, 64), '1000x1000_4f': (1000, 1000, 4, 128),
                           '2000x300_3f': (2000, 300, 3, 128), '300x2000_3f': (300, 2000, 3, 128)}.items():
    save(name + '.gif', P(anim(w, h, n), c), optimize=False, disposal=1)
def tr(w, h, n, mode):
    out = []
    for i in range(n):
        im = Image.new('RGBA', (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        if mode == 'sprite': d.ellipse((10+i*3, 10, 60+i*3, 60), fill=(220, 60, 60, 255))
        elif mode == 'mostly': d.rectangle((i, i, i+3, i+3), fill=(0, 0, 255, 255))
        out.append(im)
    return out
def savetr(n, fr, disposal):
    p = []
    for im in fr:
        a = np.array(im); alpha = a[..., 3]
        q = Image.fromarray(a[..., :3]).convert('P', palette=Image.ADAPTIVE, colors=31); arr = np.array(q); arr[alpha < 128] = 31
        q = Image.fromarray(arr, 'P'); pal = q.getpalette()[:93] + [0, 0, 0]; q.putpalette(pal + [0]*(768-len(pal))); p.append(q)
    p[0].save(n, save_all=True, append_images=p[1:], duration=70, loop=0, transparency=31, disposal=disposal)
for d in (1, 2, 3): savetr(f'trans_sprite_d{d}.gif', tr(100, 100, 8, 'sprite'), d)
savetr('trans_mostly.gif', tr(100, 100, 10, 'mostly'), 2)
base = P(anim(60, 60, 5))
base[0].save('delay0.gif', save_all=True, append_images=base[1:], duration=0, loop=0)
base[0].save('delay_huge.gif', save_all=True, append_images=base[1:], duration=[650000, 10, 10, 10, 20], loop=0)
base[0].save('delay_mixed.gif', save_all=True, append_images=base[1:], duration=[10, 500, 0, 40, 2000], loop=0)
save('noise.gif', P([Image.fromarray(np.random.randint(0, 255, (120, 120, 3), dtype=np.uint8)) for _ in range(6)], 128))
print('listo')

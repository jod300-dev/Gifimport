# Compara pixel a pixel el decodificador de GIF del mod (src/engine/gifdecode.hpp) con Pillow.
#   python3 tests/make_fuzz_gifs.py   (genera ./fuzz_gifs/*.gif)
#   g++ -O1 -std=c++20 -Iinclude -Isrc/engine tests/dec_dump.cpp -o /tmp/dec_dump
#   python3 tests/check_decoder.py fuzz_gifs/*.gif
# Nota: un GIF con disposal 3 en el PRIMER frame es ambiguo; se sigue la especificacion (lienzo vacio), que puede diferir de Pillow.
import subprocess, sys, numpy as np
from PIL import Image, ImageSequence
total = 0
for path in sys.argv[1:]:
    subprocess.run(['/tmp/dec_dump', path, '/tmp/_f.bin'], check=True)
    raw = open('/tmp/_f.bin', 'rb').read(); n, w, h = np.frombuffer(raw[:12], dtype=np.int32)
    arr = np.frombuffer(raw[12:], dtype=np.uint8).reshape(n, h, w, 4); bad = 0
    for i, fr in enumerate(ImageSequence.Iterator(Image.open(path))):
        ref = np.array(fr.convert('RGBA')).astype(int); a = arr[i].astype(int)
        both = (a[..., 3] > 127) & (ref[..., 3] > 127)
        bad += int(((np.abs(a[..., :3] - ref[..., :3]).max(axis=2) > 0) & both).sum()) + int(((a[..., 3] > 127) != (ref[..., 3] > 127)).sum())
    print(f'{path:40s} {n:4d} frames  pixeles distintos: {bad}'); total += bad
print('TOTAL', total)

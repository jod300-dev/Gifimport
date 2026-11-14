# Genera el banco de GIFs de prueba (tamanos, duraciones, calidad, movimiento): python3 tests/gen_corpus.py [gif_base] [carpeta_salida]
# gif_base por defecto = tests/corpus_base.gif (16 frames, reconstruido de las capturas del nivel 1-17)
import math, random, sys, os
from PIL import Image, ImageDraw, ImageFilter
random.seed(7)
OUT=(sys.argv[2] if len(sys.argv)>2 else 'corpus')+'/'
os.makedirs(OUT, exist_ok=True)
base=Image.open(sys.argv[1] if len(sys.argv)>1 else 'tests/corpus_base.gif'); frames=[]
for i in range(base.n_frames):
    base.seek(i); frames.append(base.convert('RGB'))
def save(name, fr, dur=70, **kw):
    fr[0].save(OUT+name+'.gif', save_all=True, append_images=fr[1:], duration=dur, loop=0, **kw)
def lerp(a,b,t): return Image.blend(a,b,t)
# --- reales
save('real16', frames, 76)
long=[]; 
for i in range(len(frames)):
    for k in range(3): long.append(lerp(frames[i], frames[(i+1)%len(frames)], k/3))
save('real48_fade', long, 40)
save('real_small64', [f.resize((64,64), Image.LANCZOS) for f in frames], 76)
save('real_big384', [f.resize((384,384), Image.BICUBIC) for f in frames], 76)
pp=frames+frames[-2:0:-1]; save('real_pingpong30', pp, 60)
save('real_wide', [f.resize((320,120)) for f in frames], 76)
save('real_tall', [f.resize((100,300)) for f in frames], 76)
save('real_pal16', [f.quantize(16, dither=Image.FLOYDSTEINBERG).convert('RGB') for f in frames], 76)
save('real_nodither8', [f.quantize(8, dither=Image.NONE).convert('RGB') for f in frames], 76)
save('real_2frames', frames[:2], 200)
save('real_1frame', frames[:1], 100)
save('real_fast80', [lerp(frames[i%16], frames[(i+1)%16], (i%3)/3).resize((96,96)) for i in range(80)], 20)
# --- sintéticos
def bounce(n=30,S=128):
    out=[]
    for i in range(n):
        im=Image.new('RGB',(S,S)); d=ImageDraw.Draw(im)
        for y in range(S): d.line([(0,y),(S,y)], fill=(40+y//3,80+y//4,160))
        x=int(20+ (S-40)*abs(math.sin(i*math.pi/n*2))); y=int(S-30-abs(math.sin(i*math.pi/n*3))*(S-60))
        d.ellipse([x-14,y-14,x+14,y+14], fill=(250,200,40)); d.rectangle([S-40,S-26,S-10,S-8], fill=(200,40,60))
        d.polygon([(10,S-10),(30,S-45),(50,S-10)], fill=(40,160,80)); out.append(im)
    return out
save('syn_bounce30', bounce(),60)
def rot(n=24,S=160):
    out=[]
    for i in range(n):
        im=Image.new('RGB',(S,S),(245,245,235)); d=ImageDraw.Draw(im); a=2*math.pi*i/n
        for r,col in ((60,(220,60,60)),(38,(60,90,220)),(18,(250,220,60))):
            pts=[(S/2+r*math.cos(a*(1 if r!=38 else -1)+k*math.pi/2+math.pi/4)*1.4, S/2+r*math.sin(a*(1 if r!=38 else -1)+k*math.pi/2+math.pi/4)*1.4) for k in range(4)]
            d.polygon(pts, fill=col)
        out.append(im)
    return out
save('syn_rotate24', rot(),60)
def scroll(n=40,W=200,H=100):
    out=[]
    for i in range(n):
        im=Image.new('RGB',(W,H),(20,20,30)); d=ImageDraw.Draw(im)
        for k in range(-2,12): 
            x=(k*24 - i*6)%(W+24)-12; d.rectangle([x,10+(k%3)*25,x+14,30+(k%3)*25], fill=((k*40)%255,200-(k*20)%160,120+(k*17)%100))
        d.text((10,H-20),'SCROLL %d'%(i%10),fill=(255,255,255)); out.append(im)
    return out
save('syn_scroll40', scroll(),50)
def colorcycle(n=20,S=96):
    out=[]
    import colorsys
    for i in range(n):
        im=Image.new('RGB',(S,S),(10,10,10)); d=ImageDraw.Draw(im)
        for k,(x0,y0,x1,y1) in enumerate(((8,8,44,44),(52,8,88,44),(8,52,44,88),(52,52,88,88),(30,30,66,66))):
            r,g,b=colorsys.hsv_to_rgb(((i/n)+k*0.2)%1,0.9,0.5+0.5*abs(math.sin(i*0.5+k)));
            d.rectangle([x0,y0,x1,y1], fill=(int(r*255),int(g*255),int(b*255)))
        out.append(im)
    return out
save('syn_colorcycle20', colorcycle(),70)
def fadephoto(n=25,S=128):
    rnd=random.Random(3); bg=Image.effect_noise((S,S),60).convert('RGB').filter(ImageFilter.GaussianBlur(3))
    fg=frames[3].resize((S,S)); return [Image.blend(bg,fg,0.5-0.5*math.cos(2*math.pi*i/n)) for i in range(n)]
save('syn_fade25', fadephoto(),60)
def noise(n=12,S=64): return [Image.effect_noise((S,S),80).convert('RGB') for _ in range(n)]
save('syn_noise12', noise(),80)
save('syn_static10', [frames[5]]*10, 100)  # PIL fusionará duplicados; se prueba igual
# transparente con disposal 2
def transp(n=20,S=96):
    out=[]
    for i in range(n):
        im=Image.new('RGBA',(S,S),(0,0,0,0)); d=ImageDraw.Draw(im); x=10+i*3
        d.ellipse([x,30,x+30,60], fill=(240,80,80,255)); d.rectangle([x+8,38,x+22,52], fill=(255,255,255,255)); out.append(im)
    return out
tr=transp(); tr[0].save(OUT+'syn_transp20.gif', save_all=True, append_images=tr[1:], duration=60, loop=0, disposal=2, transparency=0)
def tiny(n=8,S=16):
    out=[]
    for i in range(n):
        im=Image.new('RGB',(S,S),(30,30,60)); px=im.load()
        for k in range(S): px[(k+i)%S,k]=(255,200,0); px[k,(S//2+i)%S]=(0,200,255)
        out.append(im.resize((S,S)))
    return out
save('syn_tiny8', tiny(),100)
save('real_long100', [lerp(frames[(i//4)%16], frames[(i//4+1)%16], (i%4)/4) for i in range(100)], 30)
print('ok')

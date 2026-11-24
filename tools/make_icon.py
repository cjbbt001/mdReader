from PIL import Image, ImageDraw, ImageFont


def make(size):
    s = size * 8
    im = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    ink = (0x5A, 0x5A, 0x5A, 255)
    lw = max(2, int(round(s * 0.042)))

    # page with a folded top-right corner (one closed polygon -> clean joins)
    L, R = s * 0.225, s * 0.775
    T, B = s * 0.155, s * 0.845
    fold = s * 0.175
    rx = R - fold
    body = [(L, T), (rx, T), (R, T + fold), (R, B), (L, B)]
    d.polygon(body, outline=ink, width=lw)
    # the fold line
    d.line([(rx, T), (rx, T + fold), (R, T + fold)], fill=ink, width=lw, joint='curve')

    # "md" inside the page
    font = None
    for name in ('seguisb.ttf', 'segoeuib.ttf', 'arialbd.ttf'):
        try:
            font = ImageFont.truetype('C:/Windows/Fonts/' + name, int(s * 0.215))
            break
        except OSError:
            continue
    if font:
        text = 'md'
        box = d.textbbox((0, 0), text, font=font)
        tw, th = box[2] - box[0], box[3] - box[1]
        d.text((s * 0.5 - tw / 2 - box[0], s * 0.615 - th / 2 - box[1]), text, font=font, fill=ink)

    return im.resize((size, size), Image.LANCZOS)


sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
imgs = [make(x) for x in sizes]
imgs[-1].save('res/app.ico', format='ICO', sizes=[(x, x) for x in sizes])
imgs[-1].save('docs/icon.png')
imgs[3].resize((128, 128), Image.LANCZOS).save('build/icon32.png')
print('icon written')

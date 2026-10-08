#!/usr/bin/env python3
"""Rebuild the original UB icon. Optional artwork tool; requires Pillow.

The build uses the checked-in ub.ico directly. No game artwork or font is used.
"""
from pathlib import Path
from PIL import Image, ImageDraw


def icon():
    scale = 4
    size = 256 * scale
    background = Image.new('RGBA', (size, size))
    pixels = background.load()
    for y in range(size):
        t = y / (size - 1)
        color = (round(43 - 18*t), round(123 - 47*t), round(246 - 41*t), 255)
        for x in range(size):
            pixels[x, y] = color
    outline = Image.new('L', (size, size))
    ImageDraw.Draw(outline).rounded_rectangle((0, 0, size-1, size-1), radius=56*scale, fill=255)
    background.putalpha(outline)
    letters = Image.new('L', (size, size))
    draw = ImageDraw.Draw(letters)
    def box(coords):
        return tuple(round(x * scale) for x in coords)
    # U and B are simple original geometric outlines, with no font dependency.
    draw.rounded_rectangle(box((43, 76, 115, 179)), radius=30*scale, fill=255)
    draw.rectangle(box((61, 68, 97, 147)), fill=0)
    draw.rounded_rectangle(box((136, 76, 215, 134)), radius=27*scale, fill=255)
    draw.rounded_rectangle(box((136, 119, 218, 179)), radius=28*scale, fill=255)
    draw.rectangle(box((132, 76, 153, 179)), fill=255)
    draw.rounded_rectangle(box((153, 92, 195, 116)), radius=12*scale, fill=0)
    draw.rounded_rectangle(box((153, 136, 198, 162)), radius=13*scale, fill=0)
    background.alpha_composite(Image.composite(Image.new('RGBA', (size, size), 'white'),
                                              Image.new('RGBA', (size, size)), letters))
    return background.resize((256, 256), Image.Resampling.LANCZOS)


if __name__ == '__main__':
    icon().save(Path(__file__).with_name('ub.ico'), sizes=[(n, n) for n in (16, 20, 24, 32, 40, 48, 64, 128, 256)])

#!/usr/bin/env python3
"""Generate PKG artwork (icon0 512x512, pic1 1920x1080) into packaging/sce_sys."""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "packaging", "sce_sys")
BLURPLE = (88, 101, 242)
CHAT_BG = (49, 51, 56)
WHITE = (255, 255, 255)
DIM = (148, 155, 164)
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

os.makedirs(OUT, exist_ok=True)

icon = Image.new("RGB", (512, 512), BLURPLE)
d = ImageDraw.Draw(icon)
f = ImageFont.truetype(FONT, 340)
box = d.textbbox((0, 0), "D", font=f)
d.text(((512 - box[2] + box[0]) / 2 - box[0],
        (512 - box[3] + box[1]) / 2 - box[1]), "D", font=f, fill=WHITE)
icon.save(os.path.join(OUT, "icon0.png"))

pic = Image.new("RGB", (1920, 1080), CHAT_BG)
d = ImageDraw.Draw(pic)
d.rounded_rectangle((360, 380, 1560, 640), radius=36, fill=BLURPLE)
f1 = ImageFont.truetype(FONT, 120)
b1 = d.textbbox((0, 0), "orbis-discord", font=f1)
d.text(((1920 - b1[2] + b1[0]) / 2 - b1[0], (510 - b1[3] + b1[1]) / 2 - b1[1]),
       "orbis-discord", font=f1, fill=WHITE)
f2 = ImageFont.truetype(FONT, 48)
b2 = d.textbbox((0, 0), "Discord for PlayStation 4", font=f2)
d.text(((1920 - b2[2] + b2[0]) / 2 - b2[0], 700), "Discord for PlayStation 4",
       font=f2, fill=DIM)
pic.save(os.path.join(OUT, "pic1.png"))
print("wrote", OUT)

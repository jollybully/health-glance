#!/usr/bin/env python3
"""Build the 720x320 appstore banner.

Reproducible on purpose: it is composed from the real screenshots, the 144x144 icon
and the app's own palette, so it cannot drift away from what the watch draws.

Design constraints learned the hard way:
  - the four cards are 200x228, so they will not fit at 1:1 in a 320 tall banner.
    At 720 wide they do not fit in a row either: scale them down and lay out a clean
    2x2 grid, fully visible. Cropping them looked careless.
  - no giant watermark numeral. It read as accidental noise behind the cards.
  - no legend row of colour chips. The cards already carry their own colour, and a
    legend that says WORKOUTS next to a card that says WALK contradicts itself.
  - store copy, not engineering copy: "native C" and "read-only" belong in the repo.
  - the cards' "synced 09:41" footer repeats four times and turns into noise at this
    size, so the bottom 22px of each screenshot is cropped for the banner. Everything
    above it is untouched; the full frames live on in shots/ for the listing.
  - the left type block is anchored to the card grid's own top and bottom. Floated in
    the middle it left a dead corner and the whole banner leant right.

Run from the repo root:  python3 docs/store/make-banner.py
"""
from PIL import Image, ImageDraw, ImageFont

W, H = 720, 320
GREEN, BLUE, PURPLE, ORANGE = (0, 170, 85), (0, 85, 170), (85, 0, 85), (255, 85, 0)
BG = (16, 16, 20)
INK = (255, 255, 255)
SUB = (203, 203, 212)
FAINT = (138, 138, 150)
EDGE = (70, 70, 80)
SHOTS = ["emery_steps.png", "emery_sleep.png", "emery_heart.png", "emery_workouts.png"]


def font(size, bold=False):
    for path in (["/System/Library/Fonts/SFNSDisplay-Bold.otf"] if bold else
                 ["/System/Library/Fonts/SFNS.ttf"]) + ["/System/Library/Fonts/Helvetica.ttc"]:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def main(out="docs/store/banner-720x320.png"):
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)

    # 1. Cards: a clean 2x2, right aligned, every card whole.
    S = 0.62
    CROP_BOTTOM = 22                          # drops the repeated "synced" footer
    cw, ch = int(200 * S), int((228 - CROP_BOTTOM) * S)   # 124 x 128
    gap = 16
    grid_w = cw * 2 + gap
    grid_h = ch * 2 + gap
    gx = W - 34 - grid_w
    gy = (H - grid_h) // 2
    for i, shot in enumerate(SHOTS):
        col, row = i % 2, i // 2
        x = gx + col * (cw + gap)
        y = gy + row * (ch + gap)
        src = Image.open("docs/store/shots/" + shot).convert("RGB")
        src = src.crop((0, 0, src.width, src.height - CROP_BOTTOM))
        card = src.resize((cw, ch), Image.LANCZOS)
        d.rectangle([x - 2, y - 2, x + cw + 1, y + ch + 1], fill=EDGE)
        im.paste(card, (x, y))

    # 2. Brand anchor: the store icon, aligned to the grid's top edge.
    icon = Image.open("docs/store/icon-large-144.png").convert("RGB").resize((60, 60), Image.LANCZOS)
    im.paste(icon, (44, gy))

    # 3. Type, anchored top and bottom to the card grid so the two halves agree.
    d.text((120, gy - 2), "Health Glance", font=font(44, True), fill=INK)
    d.text((44, gy + 82), "Steps, sleep, heart and workouts,", font=font(21), fill=SUB)
    d.text((44, gy + 110), "straight from Health Connect.", font=font(21), fill=SUB)
    # One legible line, not two faint ones: at thumbnail size the privacy point was
    # turning to mush in the emptiest corner of the banner.
    d.text((44, gy + grid_h - 30), "Private by design: nothing leaves your phone.",
           font=font(17), fill=(206, 206, 216))

    # 4. Palette rail down the left edge: the four cap colours, one per card. On the
    # top edge it read as a generic stripe that did not line up with anything.
    for i, c in enumerate((GREEN, BLUE, PURPLE, ORANGE)):
        y0 = i * H // 4
        d.rectangle([0, y0, 7, (i + 1) * H // 4 - 1], fill=c)

    im.save(out)
    print("wrote", out, im.size)


if __name__ == "__main__":
    main()

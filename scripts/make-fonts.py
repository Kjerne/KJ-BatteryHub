# Makes res/fonts/NotoSans-Regular.ttf and NotoSans-Bold.ttf from the variable Noto Sans (SIL OFL 1.1):
# static 400 and 700 weights, no hinting, only the characters the panel's languages need (Latin with
# Latin Extended-A, Romanian, Cyrillic, punctuation), so the exe grows by about 200 KB.
# Other scripts (Arabic, CJK, Hebrew, Thai, Vietnamese) use Segoe UI instead, see src/panel.cpp.
# Needs fontTools:  python scripts/make-fonts.py <NotoSans[wdth,wght].ttf> res/fonts
import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

src, outdir = sys.argv[1], sys.argv[2]
unicodes = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x180)) + [0x218, 0x219, 0x21A, 0x21B] +
            list(range(0x400, 0x460)) + [0x490, 0x491] + list(range(0x2010, 0x2027)) +
            [0x2030, 0x2039, 0x203A, 0x20AC, 0x2116, 0x2122, 0x2190, 0x2192, 0x2212, 0x2248, 0x2264, 0x2265])

for weight in (400, 700):
    bold = weight == 700
    font = instancer.instantiateVariableFont(TTFont(src), {"wght": weight, "wdth": 100}, updateFontNames=True)
    opts = subset.Options()
    opts.hinting = False
    opts.desubroutinize = True
    opts.layout_features = ["kern", "liga", "ccmp", "locl", "mark", "mkmk", "tnum", "lnum", "pnum"]
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=unicodes)
    sub.subset(font)
    # GDI picks a face by family name and weight: one family "Noto Sans", regular and bold.
    for rec in font["name"].names:
        if rec.nameID in (1, 16):
            rec.string = "Noto Sans"
        elif rec.nameID in (2, 17):
            rec.string = "Bold" if bold else "Regular"
        elif rec.nameID == 4:
            rec.string = "Noto Sans Bold" if bold else "Noto Sans"
        elif rec.nameID == 6:
            rec.string = "NotoSans-Bold" if bold else "NotoSans-Regular"
    font["OS/2"].usWeightClass = weight
    font["OS/2"].fsSelection = (font["OS/2"].fsSelection & ~0x61) | (0x20 if bold else 0x40)
    font["head"].macStyle = 1 if bold else 0
    # GDI lays lines out by the win metrics, Qt and DirectWrite by the typo / hhea ones: the same line height
    # (1.362 em) everywhere. The kept characters all fit inside it.
    font["OS/2"].usWinAscent = font["hhea"].ascent
    font["OS/2"].usWinDescent = -font["hhea"].descent
    path = os.path.join(outdir, "NotoSans-Bold.ttf" if bold else "NotoSans-Regular.ttf")
    font.save(path)
    print(path, os.path.getsize(path))

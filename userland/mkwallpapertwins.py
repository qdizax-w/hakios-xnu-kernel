#!/usr/bin/env python3
"""The wallpapers at 2x. iOS 16.5 has the iPhone's only at 3x, and WallpaperKit takes the one whose
Wallpaper.plist names the display's screen class ("375w-812h@2x~iphone" with fbscale=2): none, and
PosterBoard draws black. Each twin is a 3x wallpaper with its plist saying 2x and links to the rest.

usage: mkwallpapertwins.py COLLECTIONS DIR          a directory per collection under DIR, for src/init.c's union
       mkwallpapertwins.py --in-place COLLECTIONS   beside the 3x ones, for an image that holds them itself
"""
import os, plistlib, sys

in_place = sys.argv[1] == '--in-place'
src = sys.argv[2 if in_place else 1]
dst = None if in_place else sys.argv[2]
n = 0
for col in sorted(os.listdir(src)):
    walls = os.path.join(src, col, 'Wallpapers')
    if not os.path.isdir(walls):
        continue
    for w in sorted(os.listdir(walls)):
        if '@3x~' not in w or not os.path.isfile(os.path.join(walls, w, 'Wallpaper.plist')):
            continue
        name = w.replace('@3x~', '@2x~')
        twin = os.path.join(walls, name) if in_place else os.path.join(dst, col, name)
        os.makedirs(twin)
        on_phone = '/Library/Wallpaper/Collections/%s/Wallpapers/%s/' % (col, w)
        for f in os.listdir(os.path.join(walls, w)):
            if f != 'Wallpaper.plist':
                os.symlink(on_phone + f, os.path.join(twin, f))
        with open(os.path.join(walls, w, 'Wallpaper.plist'), 'rb') as f:
            p = plistlib.load(f)
        p['logicalScreenClass'] = p.get('logicalScreenClass', '').replace('@3x~', '@2x~')
        with open(os.path.join(twin, 'Wallpaper.plist'), 'wb') as f:
            plistlib.dump(p, f)
        n += 1
print('  wallpapers at 2x: %d twins of the 3x ones' % n)

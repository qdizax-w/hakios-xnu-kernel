#!/usr/bin/env python3
"""The wallpapers at 2x: iOS 16.5 has the iPhone's only at 3x, and WallpaperKit takes the one whose Wallpaper.plist
names the display's screen class ("375w-812h@2x~iphone" with fbscale=2) -- none, and PosterBoard draws black.

usage: mkwallpaper2x.py [--in-collections] COLLECTIONS DESTINATION

For each 3x wallpaper in COLLECTIONS (iOS's /Library/Wallpaper/Collections), a twin under DESTINATION/<collection>/
(the layout init lays over each collection's Wallpapers), or with --in-collections under
DESTINATION/<collection>/Wallpapers/, beside the 3x one, where DESTINATION is a copy of the collections themselves:
its Wallpaper.plist saying 2x, every other file a link to the 3x one's on the phone (/Library/Wallpaper/...).
build.sh uses it twice: into flashboot/wallpaper, which init puts over each collection's Wallpapers as a union
(src/init.c, flash_root), and straight into the system image's own collections (a boot from RAM).
"""
import os
import plistlib
import sys


def main():
    args = sys.argv[1:]
    in_collections = args[:1] == ['--in-collections']
    if in_collections:
        args = args[1:]
    src, dst = args
    n = 0
    for col in sorted(os.listdir(src)):
        walls = os.path.join(src, col, 'Wallpapers')
        if not os.path.isdir(walls):
            continue
        for w in sorted(os.listdir(walls)):
            if '@3x~' not in w or not os.path.isfile(os.path.join(walls, w, 'Wallpaper.plist')):
                continue
            twin = os.path.join(dst, col, *(['Wallpapers'] if in_collections else []), w.replace('@3x~', '@2x~'))
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


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Generates the one-second probe clips (ffmpeg needed). Usage:  gen_probe_set.py OUTDIR  ARTIST_LEN ALBUM_LEN STEM_LEN
The base path is all 'a'; for every letter position two copies are made with the letter changed to 'b' and to 'c' (the layout of export4 is 48 48 44,
export10 is 48 48 20, export3 is 8 8 8). Artist and album are written as tags, the file name is the stem, so rekordbox exports /Contents/<artist>/<album>/<stem>.mp3."""
import subprocess, sys, os
root, A, B, C = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
def write(art, alb, stem):
    d = f'{root}/{art}/{alb}'; os.makedirs(d, exist_ok=True); f = f'{d}/{stem}.mp3'
    if os.path.exists(f): return
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i', 'sine=frequency=440:duration=1', '-ac', '1', '-ar', '22050', '-b:a', '32k',
                    '-id3v2_version', '3', '-metadata', f'artist={art}', '-metadata', f'album={alb}', '-metadata', f'title={stem}', f], check=True)
base = ['a' * A, 'a' * B, 'a' * C]
write(*base)
for blk, n in enumerate((A, B, C)):
    for j in range(n):
        for ch in 'bc':
            parts = [list(p) for p in base]; parts[blk][j] = ch
            write(*[''.join(p) for p in parts])
print('done')

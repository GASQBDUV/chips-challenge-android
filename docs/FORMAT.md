# Data formats (as decoded for this port)

## CHIPS.DAT
dword magic 0x0002AAAC, word level count (149). Per level: word recLen; record: word level number, word time limit (0 = none), word chips needed,
word detail flag, then `word len + RLE` UPPER layer, `word len + RLE` LOWER layer, `word len` + optional fields.
RLE: `FF count value` = count copies, else literal; 1024 cells (32x32 row-major). Fields (type, len, data): 3 title, 4 trap links (10 B: button x,y, trap x,y, flag),
5 clone links (8 B: red button x,y, machine x,y), 6 password (XOR 0x99, NUL-terminated), 7 hint, 10 creature list (x,y bytes, order = activation order).
Verified: 149 levels parse, passwords match known ones (BDHP, JXMJ, ...). Tile codes: see `src/cc_engine.h` and `re/RULES.md` section 1. Tools: `tools/dat_dump.py`, `tools/ascii_map.py`.

## CHIPS.EXE resources (tools/ne_res.py, and the C reader in cc_data.c)
NE resource table: alignment shift, then per type (type id, count) entries `offset, length, flags, id` in alignment units. Bitmaps are DIBs
(BITMAPINFOHEADER, 4bpp RLE4 compressed or 1bpp):
- 308 (416x512, 16 colours): tile sheet, 32x32 cells. Columns 0-6 opaque tiles; columns 7-9 = same sprites of cols 4-6 on white; columns 10-12 = 1-bit masks (white = sprite). 316 = alternate palette, 325 = monochrome.
- 344 (154x300): side panel (digit boxes at x=45, y=34/96/186, 52x29). 200 (17x552): 24 digit frames of 23 px: frames 0-11 yellow, 12-23 green; frame 11-d = digit d, 0 = '-', 1 = blank.
- 352 (288x288) splash picture, 333 (237x196) circuit background.
Tile sheet layout (row,col): terrain per `TERMAP` in cc_app.c, e.g. wall (1,0), IC chip (2,0), water (3,0), fire (4,0), force floors (13,0)/(2..4,1), exit (5,1), doors (6..9,1),
ice corners (10..13,1) = codes 1A..1D (verified: wall highlight centroid per cell), thief/socket/buttons/teleport/bomb/trap/hint (1..15,2), block + clone machine share (1,3),
chip drowned/burned/bombed (3..5,3), swimming Chip (12..15,3). Sprites (col,rows N,W,S,E): bug (4,0) fireball (4,4) ball (4,8) tank (4,12) glider (5,0) teeth (5,4) walker (5,8) blob (5,12) paramecium (6,0),
keys (6,4..7), boots (6,8..11), Chip (6,12..15); transparent versions at col+3.
The Windows block is drawn with the same art as the clone machine tile (the sheet has no separate block image; olive cells are unused placeholders).

/* The icons (icons.c): Phosphor Icons 2.1.1 (MIT, LICENSES/PHOSPHOR.txt) in
 * three sets, data/icons.bin (art/make_icons.py), an archive member that only
 * icons.c references: an app links both, and uses their 0.75 MB of RSX memory,
 * only when it calls icons_init. Every pkg that links them ships
 * PHOSPHOR.txt (as USRDIR/PHOSPHOR.txt beside OFL.txt).
 *
 * The ids are in icons_ids.h: IC_S_* (32 px, bold) for chips, rows and the
 * header; IC_M_* (64 px) and IC_L_* (168 px) duotone, two layers per icon,
 * *_FILL (tint it, for example at alpha 0x55) and *_LINE (the outline): draw
 * the fill first, at the same centre. The glyphs are white, so the colour of
 * a call tints them. The S and M sets bind as the fonts do (nearest at
 * 1080p); the L set always binds bilinear, because its layers turn and
 * scale. */
#ifndef ICONS_H
#define ICONS_H

#include <ppu-types.h>
#include "icons_ids.h"

/* The atlases into RSX memory, after ui_init; again: nothing. 0, or -1 when
 * icons.bin is bad (or not the one of icons_ids.h) or RSX memory is short;
 * then the calls below draw nothing and icon_size gives 0. */
int icons_init(void);

/* An icon centred at cx, cy: the middle of the glyph's em box, which the two
 * layers of an icon share. The centre snaps to a whole pixel, so an S or M
 * icon is pixel exact at 1080p: round a moving offset before the call. */
void icon(int id, float cx, float cy, u32 rgba);
/* The same turned by angle (radians, clockwise on the screen) and scaled
 * about that centre, with no snap: for the L set, whose filter is bilinear. */
void icon_rot(int id, float cx, float cy, float angle, float scale, u32 rgba);
int icon_size(int id);                       /* the set's pixel size: 32, 64 or 168; 0 for no icon */

#endif

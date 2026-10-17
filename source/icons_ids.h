/* The icon ids of data/icons.bin, in its order: written by art/make_icons.py
 * from Phosphor Icons 2.1.1 (MIT, LICENSES/PHOSPHOR.txt). Do not edit:
 * run the script. IC_S_*: 32 px, bold. IC_M_*: 64 px, IC_L_*: 168 px,
 * duotone: _FILL is the fill layer (tint it, for example at alpha 0x55),
 * _LINE the outline; draw the fill first. The L snowflake has only its
 * outline. */
#ifndef ICONS_IDS_H
#define ICONS_IDS_H

enum {
    IC_S_CARET_UP,                  /* U+E13C */
    IC_S_CARET_DOWN,                /* U+E136 */
    IC_S_DROP,                      /* U+E210 */
    IC_S_CLOCK,                     /* U+E19A */
    IC_S_WIFI_HIGH,                 /* U+E4EA */
    IC_S_WIFI_SLASH,                /* U+E4F2 */
    IC_S_CURRENCY_DOLLAR,           /* U+E550 */
    IC_S_NEWSPAPER,                 /* U+E344 */
    IC_S_INFO,                      /* U+E2CE */
    IC_S_ARROWS_CLOCKWISE,          /* U+E094 */
    IC_S_SUN_HORIZON,               /* U+E5B6 */
    IC_S_UMBRELLA,                  /* U+E684 */
    IC_M_SUN_FILL,                  /* U+E472 */
    IC_M_SUN_LINE,                  /* U+E473 */
    IC_M_MOON_STARS_FILL,           /* U+E58E */
    IC_M_MOON_STARS_LINE,           /* U+E58F */
    IC_M_CLOUD_SUN_FILL,            /* U+E540 */
    IC_M_CLOUD_SUN_LINE,            /* U+E541 */
    IC_M_CLOUD_MOON_FILL,           /* U+E53E */
    IC_M_CLOUD_MOON_LINE,           /* U+E53F */
    IC_M_CLOUD_FILL,                /* U+E1AA */
    IC_M_CLOUD_LINE,                /* U+E1AB */
    IC_M_CLOUD_FOG_FILL,            /* U+E53C */
    IC_M_CLOUD_FOG_LINE,            /* U+E53D */
    IC_M_CLOUD_RAIN_FILL,           /* U+E1B4 */
    IC_M_CLOUD_RAIN_LINE,           /* U+E1B5 */
    IC_M_CLOUD_SNOW_FILL,           /* U+E1B8 */
    IC_M_CLOUD_SNOW_LINE,           /* U+E1B9 */
    IC_M_CLOUD_LIGHTNING_FILL,      /* U+E1B2 */
    IC_M_CLOUD_LIGHTNING_LINE,      /* U+E1B3 */
    IC_L_SUN_FILL,                  /* U+E472 */
    IC_L_SUN_LINE,                  /* U+E473 */
    IC_L_MOON_FILL,                 /* U+E330 */
    IC_L_MOON_LINE,                 /* U+E331 */
    IC_L_CLOUD_FILL,                /* U+E1AA */
    IC_L_CLOUD_LINE,                /* U+E1AB */
    IC_L_LIGHTNING_FILL,            /* U+E2DE */
    IC_L_LIGHTNING_LINE,            /* U+E2DF */
    IC_L_SNOWFLAKE_LINE,            /* U+E5AB */
    IC_COUNT
};

#endif

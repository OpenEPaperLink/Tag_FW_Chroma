"""UI screens for ui_preview.py, mirroring OEPL/userinterface.c (by hand).

The DISPLAY_HEIGHT < 128 (UI_SMALL) layouts, for the Chroma21 212x104.
"""
import os
from ui_preview import BLACK, YELLOW, FONT_H

MAC = '4467090610014233'
AP_MAC = '0000C8C2ECAE3D98'
VER = 'chroma21 v0016'
HERE = os.path.dirname(os.path.abspath(__file__))
SMALL = open(os.path.join(HERE, '../common/bitmaps_small.h')).read()
LOGO_W = 9 + 20 + 9          # width of the small side logo
TEXT_W = 212 - LOGO_W - 2    # TEXT_WIDTH: room left of the logo


def logo_side(s, y=0):
    """DisplayLogo() with SPLASH_LOGO_X = DISPLAY_WIDTH"""
    x = s.w
    s.color = YELLOW
    x -= s.bitmap('CloudTop')[1]
    s.raw_bitmap('CloudTop', x, y)
    s.color = BLACK
    x -= s.bitmap('oepli')[1]
    s.raw_bitmap('oepli', x, y)
    s.color = YELLOW
    x -= s.bitmap('CloudBottom')[1]
    s.raw_bitmap('CloudBottom', x, y)
    s.color = BLACK


def small(s):
    s.bmp_src = SMALL + s.bmp_src     # the first match wins: the small logo
    s.text_w = TEXT_W
    logo_side(s, (s.h - 96) // 2)


def pr_c(s, text):
    """\\f centering within TEXT_WIDTH"""
    w = s.w
    s.w = s.text_w
    s.pr(text)
    s.w = w


def splash(s):
    small(s)
    s.y = 4
    s.pr('Starting . . .\n')
    s.pr(VER)
    s.pr('\nVBat: 3372 mV\n')
    s.pr('Temp: 25C\n\n')
    s.pr('MAC ' + MAC)


def apfound(s):
    small(s)
    s.y = 4
    s.pr('Waiting for data . . .\n\n')
    s.pr('AP ' + AP_MAC)
    s.pr('\nCh:202 RSSI:-60 LQI:50\n')
    s.pr('VBat: 3364 mV\n')
    s.pr(VER)


def noap(s):
    small(s)
    s.y = 4
    s.pr('No AP found :(\n')
    s.pr('Will retry in a\n')
    s.pr('little while . . .')
    bx, by = (TEXT_W - 56) // 2, 48
    s.raw_bitmap('receive', bx, by)
    s.color = YELLOW
    s.raw_bitmap('failed', bx, by + 8)
    s.color = BLACK


def sleep(s):
    small(s)
    s.y = FONT_H
    s.pr('\bzzZZZ . . .\n\b\n')
    s.pr('VBat: 3364 mV\n')


def afterflash(s):
    s.pr('\bOpenEPaperLink\n\b')
    s.pr('Asleep. To wake me remove\n')
    s.pr('the batteries, short the\n')
    s.pr('contacts and reinsert them.\n')
    s.pr('MAC ' + MAC)


def applyupdate(s):
    small(s)
    s.y = (s.h - FONT_H) // 2
    pr_c(s, '\fFlashing v0017 . . .')


def failedupdate(s):
    small(s)
    s.y = 20
    pr_c(s, '\fOTA FAILED :(\n\n')
    s.color = YELLOW
    pr_c(s, '\fWrong OTA image')
    s.color = BLACK


def noeeprom(s):
    small(s)
    s.y = 4
    pr_c(s, '\fEEPROM FAILED :(\n')
    pr_c(s, '\fSleeping forever . . .\n')
    s.color = YELLOW
    s.raw_bitmap('failed', (TEXT_W - 48) // 2, 44)
    s.color = BLACK


SCREENS = {
    'splash': splash,
    'apfound': apfound,
    'noap': noap,
    'sleep': sleep,
    'afterflash': afterflash,
    'applyupdate': applyupdate,
    'failedupdate': failedupdate,
    'noeeprom': noeeprom,
}

/* Copyright (C) 2020 SovietPony
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License ONLY.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "kos32.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h> // srand exit
#include <string.h>
#include <assert.h>
#include "system.h"
#include "input.h"

#include "my.h" // fexists
#include "player.h" // pl1 pl2
#include "menu.h" // G_keyf
#include "error.h" // logo
#include "monster.h" // nomon

#include "files.h" // F_startup F_addwad F_initwads F_allocres
#include "config.h" // CFG_args CFG_load CFG_save
#include "args.h" // ARG_parse
#include "memory.h" // M_startup
#include "game.h" // G_init G_act
#include "sound.h" // S_init S_done
#include "music.h" // S_initmusic S_updatemusic S_donemusic
#include "render.h" // R_init R_draw R_done

static int quit = 0;
static videomode_size_t wlist[3] = {
 { 320, 200, 0 },
 { 640, 400, 0 },
 { 800, 600, 0 },
};
static videomode_t vlist = {
  3,
  wlist
};

static byte *buf = NULL;
static int buf_w = 0;
static int buf_h = 0;
static struct rgb_pal {
  byte b, g, r, a;
} rgbpal[256];

static const cfg_t arg[] = {
  {"file", NULL, Y_FILES},
  {"cheat", &cheat, Y_SW_ON},
//  {"vga", &shot_vga, Y_SW_ON},
//  {"musvol", &mus_vol, Y_WORD},
  {"mon", &nomon, Y_SW_OFF},
  {"warp", &_warp, Y_BYTE},
//  {"config", NULL, cfg_file, Y_STRING},
  {NULL, NULL, 0} // end
};

static const cfg_t cfg[] = {
//  {"screenshot", &shot_vga, Y_SW_ON},
//  {"music_volume", &mus_vol, Y_WORD},
//  {"music_random", &music_random, Y_SW_ON},
//  {"music_time", &music_time, Y_DWORD},
//  {"music_fade", &music_fade, Y_DWORD},
  {"pl1_left", &pl1.kl, Y_KEY},
  {"pl1_right",&pl1.kr, Y_KEY},
  {"pl1_up", &pl1.ku, Y_KEY},
  {"pl1_down", &pl1.kd, Y_KEY},
  {"pl1_jump", &pl1.kj, Y_KEY},
  {"pl1_fire", &pl1.kf, Y_KEY},
  {"pl1_next", &pl1.kwr, Y_KEY},
  {"pl1_prev", &pl1.kwl, Y_KEY},
  {"pl1_use", &pl1.kp, Y_KEY},
  {"pl2_left", &pl2.kl, Y_KEY},
  {"pl2_right", &pl2.kr, Y_KEY},
  {"pl2_up", &pl2.ku, Y_KEY},
  {"pl2_down", &pl2.kd, Y_KEY},
  {"pl2_jump", &pl2.kj, Y_KEY},
  {"pl2_fire", &pl2.kf, Y_KEY},
  {"pl2_next", &pl2.kwr, Y_KEY},
  {"pl2_prev", &pl2.kwl, Y_KEY},
  {"pl2_use", &pl2.kp, Y_KEY},
  {NULL, NULL, 0} // end
};

static void CFG_args (int argc, char **argv) {
  const cfg_t *list[] = { arg, R_args(), S_args(), MUS_args() };
  ARG_parse(argc, argv, 4, list);
}

static void CFG_load (void) {
  const cfg_t *list[] = { cfg, R_conf(), S_conf(), MUS_conf() };
  CFG_read_config("default.cfg", 4, list);
  CFG_read_config("doom2d.cfg", 4, list);
}

static void CFG_save (void) {
  const cfg_t *list[] = { cfg, R_conf(), S_conf(), MUS_conf() };
  CFG_update_config("doom2d.cfg", "doom2d.cfg", 4, list, "generated by doom2d, do not modify");
}

/* --- error.h --- */

void logo (const char *s, ...) {
  va_list ap;
  va_start(ap, s);
  vprintf(s, ap);
  va_end(ap);
//  fflush(stdout);
}

void logo_gas (int cur, int all) {
  // stub
}

void ERR_failinit (char *s, ...) {
  va_list ap;
  va_start(ap, s);
  vprintf(s, ap);
  va_end(ap);
  puts("");
  while (1) ;
//  exit(1);
}

void ERR_fatal (char *s, ...) {
  va_list ap;
  R_done();
  MUS_done();
  S_done();
  M_shutdown();
  puts("\nCRITICAL ERROR:");
  va_start(ap, s);
  vprintf(s, ap);
  va_end(ap);
  puts("");
  while (1) ;
//  exit(1);
}

void ERR_quit (void) {
  quit = 1;
}

/* --- system.h --- */

static int Y_resize_window (int w, int h, int fullscreen) {
  assert(w > 0);
  assert(h > 0);
  if (buf != NULL) {
    return Y_set_videomode_software(w, h, fullscreen);
  }
  return 0;
}

int Y_set_videomode_opengl (int w, int h, int fullscreen) {
  assert(w > 0);
  assert(h > 0);
  // TODO
  return 0;
}

static void SetupWindow (int w, int h, const char *title) {
  int flags = KOS32_WIN_FLAG_CAPTION | KOS32_WIN_FLAG_RELATIVE | KOS32_WIN_FLAG_NOFILL;
  int skin_h = GetSkinHeight();
  CreateWindow(0, 0, w + 5*2, h + 5 + skin_h, KOS32_WIN_STYLE_FIXED, flags, 0x000000, 0x000000, title);
}

int Y_set_videomode_software (int w, int h, int fullscreen) {
  assert(w > 0);
  assert(h > 0);
  int size = w * h;
  byte *new_buf = malloc(size);
  if (new_buf != NULL) {
    Y_unset_videomode();
    memset(new_buf, 0, size);
    buf = new_buf;
    buf_w = w;
    buf_h = h;
    BeginDraw();
    SetupWindow(w, h, "Doom2D (software render)");
    EndDraw();
  }
  return buf != NULL;
}

void Y_get_videomode (int *w, int *h) {
  *w = buf_w;
  *h = buf_h;
}

int Y_videomode_setted (void) {
  return buf != NULL;
}

void Y_unset_videomode (void) {
  if (buf != NULL) {
    free(buf);
    buf = NULL;
    buf_w = 0;
    buf_h = 0;
  }
}

const videomode_t *Y_get_videomode_list_opengl (int fullscreen) {
  return &vlist;
}

const videomode_t *Y_get_videomode_list_software (int fullscreen) {
  return &vlist;
}

void Y_set_fullscreen (int yes) {
  // TODO
}

int Y_get_fullscreen (void) {
  // TODO
  return 0;
}

void Y_swap_buffers (void) {
  // TODO
}

void Y_get_buffer (byte **buf_ref, int *w, int *h, int *pitch) {
  assert(buf_ref != NULL);
  assert(w != NULL);
  assert(h != NULL);
  assert(pitch != NULL);
  *buf_ref = buf;
  *w = buf_w;
  *h = buf_h;
  *pitch = buf_w;
}

void Y_set_vga_palette (byte *vgapal) {
  int i;
  byte *p = vgapal;
  assert(buf != NULL);
  assert(vgapal != NULL);
  for (i = 0; i < 256; i++) {
    rgbpal[i].r = p[0] * 255 / 63;
    rgbpal[i].g = p[1] * 255 / 63;
    rgbpal[i].b = p[2] * 255 / 63;
    p += 3;
  }
}

void Y_repaint_rect (int x, int y, int w, int h) {
  Y_repaint();
}

void Y_repaint (void) {
  assert(buf != NULL);
  BeginDraw();
  SetupWindow(buf_w, buf_h, NULL);
  PutImageExt(buf, buf_w, buf_h, 0, 0, 8, rgbpal, 0);
  EndDraw();
}

void Y_enable_text_input (void) {
  SetInputMode(KOS32_INPUT_MODE_ASCII);
}

void Y_disable_text_input (void) {
  SetInputMode(KOS32_INPUT_MODE_SCANCODE);
}

/* --- main --- */

static int scancode_to_key (int scancode) {
  switch (scancode) {
    case KOS32_SC_0: return KEY_0;
    case KOS32_SC_1: return KEY_1;
    case KOS32_SC_2: return KEY_2;
    case KOS32_SC_3: return KEY_3;
    case KOS32_SC_4: return KEY_4;
    case KOS32_SC_5: return KEY_5;
    case KOS32_SC_6: return KEY_6;
    case KOS32_SC_7: return KEY_7;
    case KOS32_SC_8: return KEY_8;
    case KOS32_SC_9: return KEY_9;
    case KOS32_SC_A: return KEY_A;
    case KOS32_SC_B: return KEY_B;
    case KOS32_SC_C: return KEY_C;
    case KOS32_SC_D: return KEY_D;
    case KOS32_SC_E: return KEY_E;
    case KOS32_SC_F: return KEY_F;
    case KOS32_SC_G: return KEY_G;
    case KOS32_SC_H: return KEY_H;
    case KOS32_SC_I: return KEY_I;
    case KOS32_SC_J: return KEY_J;
    case KOS32_SC_K: return KEY_K;
    case KOS32_SC_L: return KEY_L;
    case KOS32_SC_M: return KEY_M;
    case KOS32_SC_N: return KEY_N;
    case KOS32_SC_O: return KEY_O;
    case KOS32_SC_P: return KEY_P;
    case KOS32_SC_Q: return KEY_Q;
    case KOS32_SC_R: return KEY_R;
    case KOS32_SC_S: return KEY_S;
    case KOS32_SC_T: return KEY_T;
    case KOS32_SC_U: return KEY_U;
    case KOS32_SC_V: return KEY_V;
    case KOS32_SC_W: return KEY_W;
    case KOS32_SC_X: return KEY_X;
    case KOS32_SC_Y: return KEY_Y;
    case KOS32_SC_Z: return KEY_Z;
    case KOS32_SC_RETURN: return KEY_RETURN;
    case KOS32_SC_ESCAPE: return KEY_ESCAPE;
    case KOS32_SC_BACKSPACE: return KEY_BACKSPACE;
    case KOS32_SC_TAB: return KEY_TAB;
    case KOS32_SC_SPACE: return KEY_SPACE;
    case KOS32_SC_MINUS: return KEY_MINUS;
    case KOS32_SC_EQUALS: return KEY_EQUALS;
    case KOS32_SC_LEFTBRACKET: return KEY_LEFTBRACKET;
    case KOS32_SC_RIGHTBRACKET: return KEY_RIGHTBRACKET;
    case KOS32_SC_BACKSLASH: return KEY_BACKSLASH;
    case KOS32_SC_SEMICOLON: return KEY_SEMICOLON;
    case KOS32_SC_APOSTROPHE: return KEY_APOSTROPHE;
    case KOS32_SC_GRAVE: return KEY_GRAVE;
    case KOS32_SC_COMMA: return KEY_COMMA;
    case KOS32_SC_PERIOD: return KEY_PERIOD;
    case KOS32_SC_SLASH: return KEY_SLASH;
    case KOS32_SC_CAPSLOCK: return KEY_CAPSLOCK;
    case KOS32_SC_F1: return KEY_F1;
    case KOS32_SC_F2: return KEY_F2;
    case KOS32_SC_F3: return KEY_F3;
    case KOS32_SC_F4: return KEY_F4;
    case KOS32_SC_F5: return KEY_F5;
    case KOS32_SC_F6: return KEY_F6;
    case KOS32_SC_F7: return KEY_F7;
    case KOS32_SC_F8: return KEY_F8;
    case KOS32_SC_F9: return KEY_F9;
    case KOS32_SC_F10: return KEY_F10;
    case KOS32_SC_F11: return KEY_F11;
    case KOS32_SC_F12: return KEY_F12;
    case KOS32_SC_SCROLLLOCK: return KEY_SCROLLLOCK;
    case KOS32_SC_NUMLOCK: return KEY_NUMLOCK;
    case KOS32_SC_KP_MULTIPLY: return KEY_KP_MULTIPLY;
    case KOS32_SC_KP_MINUS: return KEY_KP_MINUS;
    case KOS32_SC_KP_PLUS: return KEY_KP_PLUS;
    case KOS32_SC_KP_0: return KEY_KP_0;
    case KOS32_SC_KP_1: return KEY_KP_1;
    case KOS32_SC_KP_2: return KEY_KP_2;
    case KOS32_SC_KP_3: return KEY_KP_3;
    case KOS32_SC_KP_4: return KEY_KP_4;
    case KOS32_SC_KP_5: return KEY_KP_5;
    case KOS32_SC_KP_6: return KEY_KP_6;
    case KOS32_SC_KP_7: return KEY_KP_7;
    case KOS32_SC_KP_8: return KEY_KP_8;
    case KOS32_SC_KP_9: return KEY_KP_9;
    case KOS32_SC_KP_PERIOD: return KEY_KP_PERIOD;
    case KOS32_SC_LCTRL: return KEY_LCTRL;
    case KOS32_SC_LSHIFT: return KEY_LSHIFT;
    case KOS32_SC_LALT: return KEY_LALT;
    default: return KEY_UNKNOWN;
  }
}

static int ext_scancode_to_key (int scancode) {
  switch (scancode) {
    case KOS32_SC_INSERT: return KEY_INSERT;
    case KOS32_SC_HOME: return KEY_HOME;
    case KOS32_SC_PAGEUP: return KEY_PAGEUP;
    case KOS32_SC_DELETE: return KEY_DELETE;
    case KOS32_SC_END: return KEY_END;
    case KOS32_SC_PAGEDOWN: return KEY_PAGEDOWN;
    case KOS32_SC_RIGHT: return KEY_RIGHT;
    case KOS32_SC_LEFT: return KEY_LEFT;
    case KOS32_SC_DOWN: return KEY_DOWN;
    case KOS32_SC_UP: return KEY_UP;
    case KOS32_SC_KP_DIVIDE: return KEY_KP_DIVIDE;
    case KOS32_SC_KP_ENTER: return KEY_KP_ENTER;
    case KOS32_SC_LSUPER: return KEY_LSUPER;
    case KOS32_SC_RCTRL: return KEY_RCTRL;
    case KOS32_SC_RSHIFT: return KEY_RSHIFT;
    case KOS32_SC_RALT: return KEY_RALT;
    case KOS32_SC_RSUPER: return KEY_RSUPER;
    default: return KEY_UNKNOWN;
  }
}

static void handle_scancode (int code) {
  static enum {
    ST_std, ST_ext,
    ST_print_down_1, ST_print_down_2,
    ST_print_up_1, ST_print_up_2,
    ST_pause_1, ST_pause_2, ST_pause_3, ST_pause_4, ST_pause_5,
    ST_ok
  } state = ST_std;
  int k, down;
  // logo("scancode >> 0x%x\n", code);
  switch (state) {
    case ST_std:
      if (code == KOS32_SC_EXTENDED) {
        state = ST_ext;
      } else if (code == KOS32_SC_EXTENDED_PAUSE) {
        state = ST_pause_1;
      } else {
        state = ST_ok;
        k = scancode_to_key(code & 0x7f);
        down = !((code >> 7) & 1);
      }
      break;
    case ST_ext:
      if (code == 0x2A) {
        state = ST_print_down_1;
      } else if (code == 0xB7) {
        state = ST_print_up_1;
      } else {
        state = ST_ok;
        k = ext_scancode_to_key(code & 0x7f);
        down = !((code >> 7) & 1);
      }
      break;
    case ST_print_down_1:
      assert(code == 0xE0);
      state = ST_print_down_2;
      break;
    case ST_print_down_2:
      assert(code == 0x37);
      state = ST_ok;
      k = KEY_PRINTSCREEN;
      down = 1;
      break;
    case ST_print_up_1:
      assert(code == 0xE0);
      state = ST_print_up_2;
      break;
    case ST_print_up_2:
      assert(code == 0xAA);
      state = ST_ok;
      k = KEY_PRINTSCREEN;
      down = 0;
      break;
    case ST_pause_1:
      assert(code == 0x1D);
      state = ST_pause_2;
      break;
    case ST_pause_2:
      assert(code == 0x45);
      state = ST_pause_3;
      break;
    case ST_pause_3:
      assert(code == 0xE1);
      state = ST_pause_4;
      break;
    case ST_pause_4:
      assert(code == 0x9D);
      state = ST_pause_5;
      break;
    case ST_pause_5:
      assert(code == 0xC5);
      state = ST_ok;
      k = KEY_PAUSE;
      down = 1;
      break;
    default:
      ERR_fatal("handle_scancode: invalid state: %i\n", state);
  }
  if (state == ST_ok) {
    state = ST_std;
    // logo("key: %s (%i, %s)\n", I_key_to_string(k), k, down ? "down" : "up");
    I_press(k, down);
    GM_key(k, down);
    if (k = KEY_PAUSE) {
      I_press(k, 0);
      GM_key(k, 0);
    }
  }
}

static void poll_events (void) {
  int ev, key, button, code, ch, k;
  while((ev = CheckEvent()) != KOS32_EVENT_NONE) {
    switch (ev) {
      case KOS32_EVENT_REDRAW:
        if (buf != NULL) {
          Y_repaint(); /* redraw window */
        }
        break;
      case KOS32_EVENT_KEYBOARD:
        key = GetKey();
        if ((key & 0xff) == 0) {
          switch (GetInputMode()) {
            case KOS32_INPUT_MODE_ASCII:
              ch = (key >> 8) & 0xff;
              code = (key >> 16) & 0x7f;
              k = scancode_to_key(code);
              I_press(k, 1);
              GM_key(k, 1);
              GM_input(ch);
              I_press(k, 0);
              GM_key(k, 0);
              break;
            case KOS32_INPUT_MODE_SCANCODE:
              code = key >> 8;
              handle_scancode(code);
              break;
          }
        }
        break;
      case KOS32_EVENT_BUTTON:
        button = GetButton();
        quit = button == 256; /* close button + left click */
        break;
      default:
        ERR_fatal("poll_events: unhandled event: %i\n", ev);
    }
  }
}

static void game_loop (void) {
  static long ticks; /* ns */
  ticks = GetTimeCountPro();
  while (!quit) {
    poll_events();
    MUS_update();
    long t = GetTimeCountPro(); /* ns */
    int n = (t - ticks) / ((DELAY + 1) * 1000000);
    ticks = ticks + n * ((DELAY + 1) * 1000000);
    if (n > 0) {
      while (n) {
        G_act();
        n -= 1;
      }
      R_draw();
    }
    Delay(1);
  }
}

int main (int argc, char **argv) {
  CFG_args(argc, argv);
  logo("system: initialize engine\n");
  SetEventsMask(KOS32_EVENT_FLAG_REDRAW | KOS32_EVENT_FLAG_KEYBOARD | KOS32_EVENT_FLAG_BUTTON);
  Y_disable_text_input();
  // Player 1 defaults
  pl1.ku = KEY_KP_8;
  pl1.kd = KEY_KP_5;
  pl1.kl = KEY_KP_4;
  pl1.kr = KEY_KP_6;
  pl1.kf = KEY_PAGEDOWN;
  pl1.kj = KEY_DELETE;
  pl1.kwl = KEY_HOME;
  pl1.kwr = KEY_END;
  pl1.kp = KEY_KP_8;
  // Player 2 defaults
  pl2.ku = KEY_E;
  pl2.kd = KEY_D;
  pl2.kl = KEY_S;
  pl2.kr = KEY_F;
  pl2.kf = KEY_A;
  pl2.kj = KEY_Q;
  pl2.kwl = KEY_1;
  pl2.kwr = KEY_2;
  pl2.kp = KEY_E;
  srand(GetIdleCount());
  F_startup();
  CFG_load();
  F_addwad("doom2d.wad");
  F_initwads();
  M_startup();
  F_allocres();
  S_init();
  MUS_init();
  R_init();
  G_init();
  logo("system: game loop\n");
  game_loop();
  logo("system: finalize engine\n");
  CFG_save();
  R_done();
  MUS_done();
  S_done();
  M_shutdown();
  logo("system: halt\n");
  return 0;
}
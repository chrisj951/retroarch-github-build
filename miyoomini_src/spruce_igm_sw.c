/*
 * Spruce IGM (Software) - In-game menu for Miyoo Mini
 *
 * Software-rendered IGM for devices without a GPU.
 * Draws directly to an ARGB8888 pixel buffer using RetroArch's
 * built-in bitmap font.  Same menu structure as the GL-based IGM.
 */

#include "spruce_igm_sw.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "retroarch.h"
#include "configuration.h"
#include "command.h"
#include "runloop.h"
#include "input/input_driver.h"
#include "menu/menu_driver.h"

/* ── Menu item definitions ─────────────────────────────────── */

enum spruce_igm_sw_item
{
   IGM_RESUME = 0,
   IGM_SAVE_STATE,
   IGM_LOAD_STATE,
   IGM_RESET,
   IGM_RETROARCH,
   IGM_EXIT,
   IGM_ITEM_COUNT
};

static const char *igm_labels[IGM_ITEM_COUNT] = {
   "Resume",
   "Save",
   "Load",
   "Reset",
   "RetroArch Menu",
   "Exit Game"
};

/* ── Colours (ARGB8888) ────────────────────────────────────── */

#define COL_TEXT        0xFFBDAD91u
#define COL_TEXT_SEL    0xFFD5C4A1u
#define COL_TEXT_TITLE  0xFF689D6Au
#define COL_SHADOW      0xC0000000u
#define COL_SELECTED    0x1FC9A227u
#define COL_ACCENT      0xD9C9A227u
#define COL_TITLE_LINE  0x66665C54u

#define IGM_NO_PENDING   -1
#define IGM_FLAG_PATH    "/mnt/SDCARD/RetroArch/IGM.txt"

/* ── State ─────────────────────────────────────────────────── */

static struct
{
   bool        active;
   bool        was_paused;
   bool        needs_bg_capture;
   int         selected;
   int         pending_action;
   int         deferred_close;
   uint16_t    prev_buttons;
   uint32_t   *bg_capture;
} igm;

/* ── Helpers ───────────────────────────────────────────────── */

static uint16_t igm_read_buttons(void)
{
   unsigned i;
   uint16_t bits = 0;
   for (i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
   {
      if (input_driver_state_wrapper(0, RETRO_DEVICE_JOYPAD, 0, i))
         bits |= (1 << i);
   }
   return bits;
}

#define IGM_PRESSED(cur, prev, btn) \
   (((cur) & (1 << (btn))) && !((prev) & (1 << (btn))))

/* ── Pixel drawing helpers ─────────────────────────────────── */

static inline uint32_t blend_argb(uint32_t dst, uint32_t src)
{
   unsigned sa = (src >> 24) & 0xFF;
   unsigned sr = (src >> 16) & 0xFF;
   unsigned sg = (src >>  8) & 0xFF;
   unsigned sb =  src        & 0xFF;
   unsigned dr = (dst >> 16) & 0xFF;
   unsigned dg = (dst >>  8) & 0xFF;
   unsigned db =  dst        & 0xFF;
   unsigned ia = 255 - sa;
   unsigned or_ = (sr * sa + dr * ia) / 255;
   unsigned og  = (sg * sa + dg * ia) / 255;
   unsigned ob  = (sb * sa + db * ia) / 255;
   return 0xFF000000u | (or_ << 16) | (og << 8) | ob;
}

static void fill_rect_blend(uint32_t *buf, unsigned pitch,
      int x, int y, int w, int h,
      unsigned scr_w, unsigned scr_h, uint32_t color)
{
   int ix, iy;
   for (iy = y; iy < y + h && iy < (int)scr_h; iy++)
   {
      if (iy < 0) continue;
      for (ix = x; ix < x + w && ix < (int)scr_w; ix++)
      {
         if (ix < 0) continue;
         buf[iy * pitch + ix] = blend_argb(buf[iy * pitch + ix], color);
      }
   }
}

static void fill_rect_solid(uint32_t *buf, unsigned pitch,
      int x, int y, int w, int h,
      unsigned scr_w, unsigned scr_h, uint32_t color)
{
   int ix, iy;
   for (iy = y; iy < y + h && iy < (int)scr_h; iy++)
   {
      if (iy < 0) continue;
      for (ix = x; ix < x + w && ix < (int)scr_w; ix++)
      {
         if (ix < 0) continue;
         buf[iy * pitch + ix] = color;
      }
   }
}

/* Draw a single character at 2x scale.  Returns advance in pixels. */
static int draw_char(uint32_t *buf, unsigned pitch,
      unsigned scr_w, unsigned scr_h,
      int x, int y, char ch, uint32_t color,
      bitmapfont_lut_t *font)
{
   int i, j;
   unsigned symbol = (unsigned char)ch;
   bool *lut;

   if (!font || symbol >= font->glyph_max)
      return FONT_WIDTH_STRIDE * 2;

   lut = font->lut[symbol];
   if (!lut)
      return FONT_WIDTH_STRIDE * 2;

   for (j = 0; j < FONT_HEIGHT; j++)
   {
      for (i = 0; i < FONT_WIDTH; i++)
      {
         if (lut[i + j * FONT_WIDTH])
         {
            int px = x + i * 2;
            int py = y + j * 2;
            if (px >= 0 && px + 1 < (int)scr_w &&
                py >= 0 && py + 1 < (int)scr_h)
            {
               buf[(py    ) * pitch + px    ] = color;
               buf[(py    ) * pitch + px + 1] = color;
               buf[(py + 1) * pitch + px    ] = color;
               buf[(py + 1) * pitch + px + 1] = color;
            }
         }
      }
   }
   return FONT_WIDTH_STRIDE * 2;
}

static void draw_text(uint32_t *buf, unsigned pitch,
      unsigned scr_w, unsigned scr_h,
      int x, int y, const char *text, uint32_t color,
      bitmapfont_lut_t *font)
{
   while (*text)
   {
      /* Shadow */
      draw_char(buf, pitch, scr_w, scr_h,
            x + 1, y + 1, *text, COL_SHADOW, font);
      /* Main */
      x += draw_char(buf, pitch, scr_w, scr_h,
            x, y, *text, color, font);
      text++;
   }
}

static int text_width(const char *text)
{
   return (int)strlen(text) * FONT_WIDTH_STRIDE * 2;
}

/* ── Enable check ──────────────────────────────────────────── */

bool spruce_igm_sw_is_enabled(void)
{
   return access(IGM_FLAG_PATH, F_OK) == 0;
}

/* ── Toggle ────────────────────────────────────────────────── */

void spruce_igm_sw_toggle(void)
{
   if (igm.active)
   {
      igm.active         = false;
      igm.pending_action = IGM_NO_PENDING;

      if (igm.bg_capture)
      {
         free(igm.bg_capture);
         igm.bg_capture = NULL;
      }

      if (!igm.was_paused)
         command_event(CMD_EVENT_UNPAUSE, NULL);

      menu_state_get_ptr()->input_driver_flushing_input = 2;
   }
   else
   {
      runloop_state_t *runloop_st = runloop_state_get_ptr();
      igm.was_paused       = (runloop_st->flags & RUNLOOP_FLAG_PAUSED) != 0;
      igm.active           = true;
      igm.selected         = 0;
      igm.pending_action   = IGM_NO_PENDING;
      igm.deferred_close   = IGM_NO_PENDING;
      igm.prev_buttons     = 0xFFFF;
      igm.needs_bg_capture = true;

      if (!igm.was_paused)
         command_event(CMD_EVENT_PAUSE, NULL);
   }
}

bool spruce_igm_sw_is_active(void)
{
   return igm.active;
}

/* ── Deferred action processing ────────────────────────────── */

void spruce_igm_sw_process_pending(void)
{
   int action;

   if (igm.pending_action == IGM_NO_PENDING)
      return;

   action             = igm.pending_action;
   igm.pending_action = IGM_NO_PENDING;
   igm.active         = false;

   if (igm.bg_capture)
   {
      free(igm.bg_capture);
      igm.bg_capture = NULL;
   }

   if (!igm.was_paused)
      command_event(CMD_EVENT_UNPAUSE, NULL);

   menu_state_get_ptr()->input_driver_flushing_input = 2;

   switch (action)
   {
      case IGM_RESUME:
         break;
      case IGM_LOAD_STATE:
         command_event(CMD_EVENT_LOAD_STATE, NULL);
         runloop_state_get_ptr()->run_frames_and_pause = 0;
         break;
      case IGM_RESET:
         command_event(CMD_EVENT_RESET, NULL);
         break;
      case IGM_RETROARCH:
         command_event(CMD_EVENT_MENU_TOGGLE, NULL);
         break;
      case IGM_EXIT:
         command_event(CMD_EVENT_QUIT, NULL);
         break;
   }
}

/* ── Input handling ────────────────────────────────────────── */

static void igm_handle_input(void)
{
   uint16_t cur  = igm_read_buttons();
   uint16_t prev = igm.prev_buttons;
   igm.prev_buttons = cur;

   if (igm.deferred_close != IGM_NO_PENDING)
   {
      if (!(cur & ((1 << RETRO_DEVICE_ID_JOYPAD_A)
                  | (1 << RETRO_DEVICE_ID_JOYPAD_B))))
      {
         igm.pending_action = igm.deferred_close;
         igm.deferred_close = IGM_NO_PENDING;
      }
      return;
   }

   if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_UP))
   {
      igm.selected--;
      if (igm.selected < 0)
         igm.selected = IGM_ITEM_COUNT - 1;
   }

   if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_DOWN))
   {
      igm.selected++;
      if (igm.selected >= IGM_ITEM_COUNT)
         igm.selected = 0;
   }

   if (igm.selected == IGM_SAVE_STATE || igm.selected == IGM_LOAD_STATE)
   {
      if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_LEFT))
         command_event(CMD_EVENT_SAVE_STATE_DECREMENT, NULL);
      if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_RIGHT))
         command_event(CMD_EVENT_SAVE_STATE_INCREMENT, NULL);
   }

   if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_B))
   {
      igm.deferred_close = IGM_RESUME;
      return;
   }

   if (IGM_PRESSED(cur, prev, RETRO_DEVICE_ID_JOYPAD_A))
   {
      switch (igm.selected)
      {
         case IGM_SAVE_STATE:
            command_event(CMD_EVENT_SAVE_STATE, NULL);
            break;
         default:
            igm.deferred_close = igm.selected;
            return;
      }
   }
}

/* ── Rendering ─────────────────────────────────────────────── */

void spruce_igm_sw_frame(uint32_t *draw_buf, const uint32_t *front_buf,
      unsigned width, unsigned height,
      unsigned pitch, bitmapfont_lut_t *font)
{
   int i;
   char slot_buf[64];
   settings_t *settings;

   if (!igm.active)
      return;

   /* Capture background on first frame */
   if (igm.needs_bg_capture)
   {
      size_t sz = width * height * sizeof(uint32_t);
      if (!igm.bg_capture)
         igm.bg_capture = (uint32_t *)malloc(sz);
      if (igm.bg_capture && front_buf)
         memcpy(igm.bg_capture, front_buf, sz);
      igm.needs_bg_capture = false;
   }

   /* Handle input */
   igm_handle_input();

   settings = config_get_ptr();

   /* ── Layout constants ────────────────────────────── */
   int margin   = width * 2 / 100;
   int panel_w  = width * 38 / 100;
   int item_h   = height * 8 / 100;
   int title_h  = item_h;
   int panel_h  = item_h * IGM_ITEM_COUNT + title_h;
   int panel_x  = margin;
   int panel_y  = (height - panel_h) / 2;
   int text_cx  = panel_x + panel_w / 2;
   int accent_w = panel_w / 80;
   int glyph_h  = FONT_HEIGHT * 2;
   if (accent_w < 2) accent_w = 2;

   /* ── Draw dimmed background ──────────────────────── */
   if (igm.bg_capture)
   {
      unsigned px;
      for (px = 0; px < width * height; px++)
      {
         uint32_t c = igm.bg_capture[px];
         unsigned r = ((c >> 16) & 0xFF) * 90 / 256;
         unsigned g = ((c >>  8) & 0xFF) * 90 / 256;
         unsigned b = ( c        & 0xFF) * 90 / 256;
         draw_buf[px] = 0xFF000000u | (r << 16) | (g << 8) | b;
      }
   }
   else
      memset(draw_buf, 0, width * height * sizeof(uint32_t));

   /* ── Title ───────────────────────────────────────── */
   {
      const char *title = "spruceOS Menu";
      int tw = text_width(title);
      int tx = text_cx - tw / 2;
      int ty = panel_y + (title_h - glyph_h) / 2;
      draw_text(draw_buf, pitch, width, height,
            tx, ty, title, COL_TEXT_TITLE, font);
   }

   /* Thin line under title */
   {
      int lx = panel_x + panel_w / 10;
      int lw = panel_w - panel_w / 5;
      int ly = panel_y + title_h - 1;
      fill_rect_blend(draw_buf, pitch, lx, ly, lw, 1,
            width, height, COL_TITLE_LINE);
   }

   /* ── Menu items ──────────────────────────────────── */
   for (i = 0; i < IGM_ITEM_COUNT; i++)
   {
      int iy        = panel_y + title_h + i * item_h;
      bool selected = (i == igm.selected);
      const char *label;
      uint32_t text_col;
      int tw, tx, ty;

      /* Selection highlight + accent bar */
      if (selected)
      {
         fill_rect_blend(draw_buf, pitch,
               panel_x, iy, panel_w, item_h,
               width, height, COL_SELECTED);
         fill_rect_blend(draw_buf, pitch,
               panel_x, iy, accent_w, item_h,
               width, height, COL_ACCENT);
      }

      /* Label text */
      if (i == IGM_SAVE_STATE || i == IGM_LOAD_STATE)
      {
         int slot = settings->ints.state_slot;
         if (slot < 0)
            snprintf(slot_buf, sizeof(slot_buf), "%s Auto",
                  igm_labels[i]);
         else
            snprintf(slot_buf, sizeof(slot_buf), "%s Slot %d",
                  igm_labels[i], slot);
         label = slot_buf;
      }
      else
         label = igm_labels[i];

      text_col = selected ? COL_TEXT_SEL : COL_TEXT;
      tw = text_width(label);
      tx = text_cx - tw / 2;
      ty = iy + (item_h - glyph_h) / 2;

      draw_text(draw_buf, pitch, width, height,
            tx, ty, label, text_col, font);

      /* Arrows for Save/Load rows */
      if (i == IGM_SAVE_STATE || i == IGM_LOAD_STATE)
      {
         int arrow_y = iy + (item_h - glyph_h) / 2;
         draw_text(draw_buf, pitch, width, height,
               panel_x + panel_w / 10, arrow_y, "<",
               selected ? COL_TEXT_SEL : COL_TEXT, font);
         draw_text(draw_buf, pitch, width, height,
               panel_x + panel_w - panel_w / 10 - text_width(">"),
               arrow_y, ">",
               selected ? COL_TEXT_SEL : COL_TEXT, font);
      }
   }
}

#ifndef SPRUCE_IGM_SW_H
#define SPRUCE_IGM_SW_H

#include <boolean.h>
#include <stdint.h>
#include "gfx/drivers_font_renderer/bitmap.h"

bool spruce_igm_sw_is_enabled(void);
void spruce_igm_sw_toggle(void);
bool spruce_igm_sw_is_active(void);
void spruce_igm_sw_process_pending(void);

/* Called by the miyoomini video driver each frame while active.
 * draw_buf  : destination pixel buffer (menuscreen, ARGB8888)
 * front_buf : current framebuffer front buffer (for bg capture)
 * width/height : screen dimensions (640x480)
 * pitch     : draw_buf stride in pixels
 * font      : bitmap font LUT */
void spruce_igm_sw_frame(uint32_t *draw_buf, const uint32_t *front_buf,
      unsigned width, unsigned height,
      unsigned pitch, bitmapfont_lut_t *font);

#endif /* SPRUCE_IGM_SW_H */

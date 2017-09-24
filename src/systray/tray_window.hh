#ifndef TINT3_SYSTRAYBAR_TRAY_WINDOW_HH
#define TINT3_SYSTRAYBAR_TRAY_WINDOW_HH

#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>

#include "util/timer.hh"

class TrayWindow {
 public:
  TrayWindow(Window parent_id, Window child_id);

  Window id;
  Window child_id;
  int x, y;
  int width, height;
  // TODO: manage icon's show/hide
  bool hide;
  int depth;
  Damage damage;
  Interval::Id render_timeout;
};

#endif  // TINT3_SYSTRAYBAR_TRAY_WINDOW_HH

/**************************************************************************
 *
 * Tint3 : clock
 *
 * Copyright (C) 2008 thierry lorthiois (lorthiois@bbsoft.fr) from Omega
 *distribution
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 *USA.
 **************************************************************************/

#include <cairo-xlib.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "absl/time/time.h"

#include "clock/clock.hh"
#include "clock/time_utils.hh"
#include "panel.hh"
#include "server.hh"
#include "subprocess.hh"
#include "util/common.hh"
#include "util/timer.hh"
#include "util/window.hh"

std::string time1_format;
std::string time1_timezone;
std::string time2_format;
std::string time2_timezone;
std::string time_tooltip_format;
std::string time_tooltip_timezone;
std::string clock_lclick_command;
std::string clock_rclick_command;
time_t time_clock;
util::pango::FontDescriptionPtr time1_font_desc;
util::pango::FontDescriptionPtr time2_font_desc;
bool clock_enabled;
static Interval::Id clock_timeout;

void DefaultClock() {
  clock_enabled = false;
  clock_timeout.reset();
  time1_format.clear();
  time1_timezone.clear();
  time2_format.clear();
  time2_timezone.clear();
  time_tooltip_format.clear();
  time_tooltip_timezone.clear();
  clock_lclick_command.clear();
  clock_rclick_command.clear();
  time1_font_desc = nullptr;
  time2_font_desc = nullptr;
}

void CleanupClock(Timer& timer) {
  if (clock_timeout) {
    timer.ClearInterval(clock_timeout);
  }

  time1_format.clear();
  time1_timezone.clear();
  time2_format.clear();
  time2_timezone.clear();
  time_tooltip_format.clear();
  time_tooltip_timezone.clear();
  clock_lclick_command.clear();
  clock_rclick_command.clear();
}

bool UpdateClockSeconds() {
  time(&time_clock);

  if (!time1_format.empty()) {
    for (Panel& p : panels) {
      p.clock()->need_resize_ = true;
    }
  }

  panel_refresh = true;
  return true;
}

bool UpdateClockMinutes() {
  // remember old_time because after suspend/hibernate the clock should be
  // updated directly, and not on the next minute change
  time_t old_time = time_clock;
  time(&time_clock);

  if (time_clock % 60 == 0 || time_clock - old_time > 60) {
    if (!time1_format.empty()) {
      for (Panel& p : panels) {
        p.clock()->need_resize_ = true;
      }
    }
    panel_refresh = true;
  }

  return true;
}

std::string Clock::GetTooltipText() {
  return FormatTime(time_tooltip_format,
                    ClockGetTimeForTimezone(time_tooltip_timezone, time_clock));
}

void InitClock(Timer& timer) {
  if (time1_format.empty() || clock_timeout) {
    return;
  }

  bool has_seconds_format = time1_format.find('S') != std::string::npos ||
                            time1_format.find('T') != std::string::npos ||
                            time1_format.find('r') != std::string::npos;

  auto& update_func =
      (has_seconds_format ? UpdateClockSeconds : UpdateClockMinutes);
  clock_timeout = timer.SetInterval(absl::Seconds(1), update_func);
  update_func();
}

void Clock::InitPanel(Panel* panel) {
  Clock* clock = panel->clock();
  clock->parent_ = panel;
  clock->panel_ = panel;
  clock->size_mode_ = SizeMode::kByContent;

  // check consistency
  if (time1_format.empty()) {
    return;
  }

  clock->need_resize_ = true;
  clock->on_screen_ = true;
}

void Clock::DrawForeground(cairo_t* c) {
  util::GObjectPtr<PangoLayout> layout(pango_cairo_create_layout(c));

  // draw layout
  pango_layout_set_font_description(layout.get(), time1_font_desc());
  pango_layout_set_width(layout.get(), width_ * PANGO_SCALE);
  pango_layout_set_alignment(layout.get(), PANGO_ALIGN_CENTER);
  pango_layout_set_text(layout.get(), time1_.c_str(), time1_.size());

  cairo_set_source_rgba(c, font_[0], font_[1], font_[2], font_.alpha());
  pango_cairo_update_layout(c, layout.get());
  cairo_move_to(c, 0, time1_posy_);
  pango_cairo_show_layout(c, layout.get());

  if (panel_->g_task.font_shadow) {
    cairo_set_source_rgba(c, font_[0], font_[1], font_[2], 0.5 * font_.alpha());
    pango_cairo_update_layout(c, layout.get());
    cairo_move_to(c, 0, time1_posy_ + 1);
    pango_cairo_show_layout(c, layout.get());
  }

  if (!time2_format.empty()) {
    pango_layout_set_font_description(layout.get(), time2_font_desc());
    pango_layout_set_indent(layout.get(), 0);
    pango_layout_set_text(layout.get(), time2_.c_str(), time2_.size());
    pango_layout_set_width(layout.get(), width_ * PANGO_SCALE);

    cairo_set_source_rgba(c, font_[0], font_[1], font_[2], font_.alpha());
    pango_cairo_update_layout(c, layout.get());
    cairo_move_to(c, 0, time2_posy_);
    pango_cairo_show_layout(c, layout.get());

    if (panel_->g_task.font_shadow) {
      cairo_set_source_rgba(c, font_[0], font_[1], font_[2],
                            0.5 * font_.alpha());
      pango_cairo_update_layout(c, layout.get());
      cairo_move_to(c, 0, time2_posy_ + 1);
      pango_cairo_show_layout(c, layout.get());
    }
  }
}

bool Clock::Resize() {
  need_redraw_ = true;

  int time_width = 0, time_height = 0;
  int date_width = 0, date_height = 0;

  time1_ = FormatTime(time1_format,
                      ClockGetTimeForTimezone(time1_timezone, time_clock));
  GetTextSize(time1_font_desc, time1_, MarkupTag::kNoMarkup, &time_width,
              &time_height);

  if (!time2_format.empty()) {
    time2_ = FormatTime(time2_format,
                        ClockGetTimeForTimezone(time2_timezone, time_clock));
    GetTextSize(time2_font_desc, time2_, MarkupTag::kNoMarkup, &date_width,
                &date_height);
  }

  if (panel_->horizontal()) {
    int new_size = std::max(time_width, date_width) + (2 * padding_x_lr_) +
                   (2 * bg_.border().width());

    if (new_size > static_cast<int>(width_) ||
        new_size < static_cast<int>(width_) - 6) {
      // we try to limit the number of resize
      width_ = new_size + 1;
      time1_posy_ = (height_ - time_height) / 2;

      if (!time2_format.empty()) {
        time1_posy_ -= (date_height) / 2;
        time2_posy_ = time1_posy_ + time_height;
      }

      return true;
    }
  } else {
    int new_size = time_height + date_height +
                   (2 * (padding_x_lr_ + bg_.border().width()));

    if (new_size != static_cast<int>(height_)) {
      // we try to limit the number of resize
      height_ = new_size;
      time1_posy_ = (height_ - time_height) / 2;

      if (!time2_format.empty()) {
        time1_posy_ -= (date_height) / 2;
        time2_posy_ = time1_posy_ + time_height;
      }

      return true;
    }
  }

  return false;
}

bool Clock::HandlesClick(XEvent* event) {
  if (!Area::HandlesClick(event)) {
    return false;
  }

  int button = event->xbutton.button;
  return ((button == 1 && !clock_lclick_command.empty()) ||
          (button == 3 && !clock_rclick_command.empty()));
}

bool Clock::OnClick(XEvent* event) {
  if (!Area::OnClick(event)) {
    return false;
  }

  int button = event->xbutton.button;
  pid_t child_pid = -1;
  if (button == 1) {
    child_pid = ShellExec(clock_lclick_command);
  } else if (button == 2) {
    child_pid = ShellExec(clock_rclick_command);
  }
  return (child_pid > 0);
}

#ifdef _TINT3_DEBUG

std::string Clock::GetFriendlyName() const { return "Clock"; }

#endif  // _TINT3_DEBUG

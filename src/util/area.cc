/**************************************************************************
 *
 * Tint3 : area
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

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "panel.hh"
#include "server.hh"
#include "util/area.hh"
#include "util/common.hh"
#include "util/geometry.hh"
#include "util/log.hh"

Area::Area()
    : panel_x_(0),
      panel_y_(0),
      width_(0),
      height_(0),
      on_screen_(false),
      size_mode_(SizeMode::kByLayout),
      need_resize_(false),
      need_redraw_(false),
      padding_x_lr_(0),
      padding_x_(0),
      padding_y_(0),
      parent_(nullptr),
      panel_(nullptr),
      on_changed_(false),
      has_mouse_effects_(false),
      mouse_state_(MouseState::kMouseNormal) {}

Area::~Area() {}

Area& Area::CloneArea(Area const& other) {
  panel_x_ = other.panel_x_;
  panel_y_ = other.panel_y_;
  width_ = other.width_;
  height_ = other.height_;
  pix_ = other.pix_;
  bg_ = other.bg_;

  for (auto& child : children_) {
    delete child;
  }

  children_ = other.children_;

  on_screen_ = other.on_screen_;
  size_mode_ = other.size_mode_;
  need_resize_ = other.need_resize_;
  need_redraw_ = other.need_redraw_;
  padding_x_lr_ = other.padding_x_lr_;
  padding_x_ = other.padding_x_;
  padding_y_ = other.padding_y_;
  parent_ = other.parent_;
  panel_ = other.panel_;
  on_changed_ = other.on_changed_;
  return (*this);
}

void Area::set_background(Background const& background) { bg_ = background; }

/************************************************************
 * !!! This design is experimental and not yet fully implemented !!!!!!!!!!!!!
 *
 * DATA ORGANISATION :
 * Areas in tint3 are similar to widgets in a GUI.
 * All graphical objects (panel, taskbar, task, systray, clock, ...) 'inherit'
 *an abstract class 'Area'.
 * This class 'Area' manage the background, border, size, position and padding.
 * Area is at the begining of each object (&object == &area).
 *
 * tint3 define one panel per monitor. And each panel have a tree of Area.
 * The root of the tree is Panel.Area. And task, clock, systray, taskbar,... are
 *nodes.
 *
 * The tree give the localisation of each object :
 * - tree's root is in the background while tree's leafe are foreground objects
 * - position of a node/Area depend on the layout : parent's position (posx,
 *posy), size of previous brothers and parent's padding
 * - size of a node/Area depend on the content (kByContent objects) or on
 *the layout (kByLayout objects)
 *
 * DRAWING AND LAYERING ENGINE :
 * Redrawing an object (like the clock) could come from an 'external event'
 *(date change)
 * or from a 'layering event' (position change).
 * The following 'drawing engine' take care of :
 * - posx/posy of all Area
 * - 'layering event' propagation between object
 * 1) browse tree kByContent
 *  - resize kByContent node : children are resized before parent
 *  - if 'size' changed then 'need_resize = true' on the parent
 * 2) browse tree kByLayout and POSITION
 *  - resize kByLayout node : parent is resized before children
 *  - calculate position (posx,posy) : parent is calculated before children
 *  - if 'position' changed then 'need_redraw = 1'
 * 3) browse tree REDRAW
 *  - redraw needed objects : parent is drawn before children
 *
 * CONFIGURE PANEL'S LAYOUT :
 * 'panel_items' parameter (in config) define the list and the order of nodes in
 *tree's panel.
 * 'panel_items = SC' define a panel with just Systray and Clock.
 * So the tree 'Panel.Area' will have 2 children (Systray and Clock).
 *
 ************************************************************/

void Area::InitRendering(int pos) {
  Border const& b = bg_.border();
  // initialize fixed position/size
  for (auto& child : children_) {
    if (panel_->horizontal()) {
      child->panel_y_ = pos + b.width_for_side(BORDER_TOP) + padding_y_;
      child->height_ = height_ - b.width_for_side(BORDER_TOP) -
                       b.width_for_side(BORDER_BOTTOM) - (2 * padding_y_);
      child->InitRendering(child->panel_y_);
    } else {
      child->panel_x_ = pos + b.width_for_side(BORDER_LEFT) + padding_x_;
      child->width_ = width_ - b.width_for_side(BORDER_LEFT) -
                      b.width_for_side(BORDER_RIGHT) - (2 * padding_x_);
      child->InitRendering(child->panel_x_);
    }
  }
}

void Area::SizeByContent() {
  // don't resize hidden objects
  if (!on_screen_) {
    return;
  }

  // children node are resized before its parent
  for (auto& child : children_) {
    child->SizeByContent();
  }

  // calculate area's size
  on_changed_ = false;

  if (need_resize_ && size_mode_ == SizeMode::kByContent) {
    need_resize_ = false;

    if (Resize()) {
      // 'size' changed => 'need_resize = true' on the parent
      parent_->need_resize_ = true;
      on_changed_ = true;
    }
  }
}

void Area::SizeByLayout(int pos, int level) {
  // don't resize hidden objects
  if (!on_screen_) {
    return;
  }

  // parent node is resized before its children
  // calculate area's size
  if (need_resize_ && size_mode_ == SizeMode::kByLayout) {
    need_resize_ = false;

    Resize();

    // resize children with kByLayout
    for (auto& child : children_) {
      if (child->size_mode_ == SizeMode::kByLayout &&
          child->children_.size() != 0) {
        child->need_resize_ = true;
      }
    }
  }

  // update position of children
  pos += padding_x_lr_ + bg_.border().width();

  for (auto& child : children_) {
    if (!child->on_screen_) {
      continue;
    }

    if (panel_->horizontal()) {
      if (pos != child->panel_x_) {
        child->panel_x_ = pos;
        child->on_changed_ = true;
      }
    } else {
      if (pos != child->panel_y_) {
        child->panel_y_ = pos;
        child->on_changed_ = true;
      }
    }

    child->SizeByLayout(pos, level + 1);

    if (panel_->horizontal()) {
      pos += child->width_ + padding_x_;
    } else {
      pos += child->height_ + padding_x_;
    }
  }

  if (on_changed_) {
    // pos/size changed
    need_redraw_ = true;
    OnChangeLayout();
  }
}

void Area::Refresh() {
  // don't draw and resize invisible objects
  if (!on_screen_ || width_ == 0 || height_ == 0) {
    return;
  }

  // don't draw transparent objects (without foreground and without background)
  if (need_redraw_) {
    need_redraw_ = false;
    Draw();
  }

  // draw current Area
  if (pix_ == None) {
    util::log::Debug() << "Empty area at panel_x_ = " << panel_x_
                       << ", width = " << width_ << '\n';
  } else {
    XCopyArea(server.dsp, pix_, panel_->temp_pmap, server.gc, 0, 0, width_,
              height_, panel_x_, panel_y_);
  }

  // and then refresh child object
  for (auto& child : children_) {
    child->Refresh();
  }
}

int Area::ResizeByLayout(int maximum_size) {
  int size, nb_by_content = 0, nb_by_layout = 0;

  if (panel_->horizontal()) {
    // detect free size for kByLayout's Area
    size = width_ - (2 * (padding_x_lr_ + bg_.border().width()));

    for (auto& child : children_) {
      if (child->on_screen_) {
        if (child->size_mode_ == SizeMode::kByContent) {
          size -= child->width_;
          nb_by_content++;
        } else if (child->size_mode_ == SizeMode::kByLayout) {
          nb_by_layout++;
        }
      }
    }

    if (nb_by_content + nb_by_layout != 0) {
      size -= ((nb_by_content + nb_by_layout - 1) * padding_x_);
    }

    int width = 0, modulo = 0;

    if (nb_by_layout) {
      width = size / nb_by_layout;
      modulo = size % nb_by_layout;

      if (width > maximum_size && maximum_size != 0) {
        width = maximum_size;
        modulo = 0;
      }
    }

    // resize kByLayout objects
    for (auto& child : children_) {
      if (child->on_screen_ && child->size_mode_ == SizeMode::kByLayout) {
        unsigned int old_width = child->width_;
        child->width_ = width;

        if (modulo != 0) {
          child->width_++;
          modulo--;
        }

        if (child->width_ != old_width) {
          child->on_changed_ = true;
        }
      }
    }
  } else {
    // detect free size for kByLayout's Area
    size = height_ - (2 * (padding_x_lr_ + bg_.border().width()));

    for (auto& child : children_) {
      if (child->on_screen_) {
        if (child->size_mode_ == SizeMode::kByContent) {
          size -= child->height_;
          nb_by_content++;
        } else if (child->size_mode_ == SizeMode::kByLayout) {
          nb_by_layout++;
        }
      }
    }

    if (nb_by_content + nb_by_layout != 0) {
      size -= ((nb_by_content + nb_by_layout - 1) * padding_x_);
    }

    int height = 0, modulo = 0;

    if (nb_by_layout) {
      height = size / nb_by_layout;
      modulo = size % nb_by_layout;

      if (height > maximum_size && maximum_size != 0) {
        height = maximum_size;
        modulo = 0;
      }
    }

    // resize kByLayout objects
    for (auto& child : children_) {
      if (child->on_screen_ && child->size_mode_ == SizeMode::kByLayout) {
        unsigned int old_height = child->height_;
        child->height_ = height;

        if (modulo != 0) {
          child->height_++;
          modulo--;
        }

        if (child->height_ != old_height) {
          child->on_changed_ = true;
        }
      }
    }
  }

  return 0;
}

void Area::SetRedraw() {
  need_redraw_ = true;

  for (auto& child : children_) {
    child->SetRedraw();
  }
}

void Area::Hide() {
  on_screen_ = false;
  parent_->need_resize_ = true;

  if (panel_->horizontal()) {
    width_ = 0;
  } else {
    height_ = 0;
  }
}

void Area::Show() {
  on_screen_ = true;
  need_resize_ = true;
  parent_->need_resize_ = true;
}

void Area::Draw() {
  if (width_ == 0 || height_ == 0) {
    // Don't attempt drawing on zero-sized areas.
    // Xlib routines such as XCreatePixmap would fail with BadValue in such a
    // case, and there's no point to waste time drawing a non-visible area.
    return;
  }

  pix_ = server.CreatePixmap(width_, height_);
  XCopyArea(server.dsp, panel_->temp_pmap, pix_, server.gc, panel_x_, panel_y_,
            width_, height_, 0, 0);

  cairo_surface_t* cs = cairo_xlib_surface_create(
      server.dsp, pix_, server.visual, width_, height_);
  cairo_t* c = cairo_create(cs);

  DrawBackground(c);
  DrawForeground(c);

  cairo_destroy(c);
  cairo_surface_destroy(cs);
}

void Area::DrawBackground(cairo_t* c) {
  Border const& b = bg_.border();
  util::Rect extents = b.GetInnerAreaRect(width_, height_);

  Color fill_color = bg_.fill_color_for(mouse_state_);
  if (fill_color.alpha() > 0.0) {
    DrawRect(c, extents.top_left().first, extents.top_left().second,
             extents.bottom_right().first, extents.bottom_right().second,
             bg_.border().rounded() - b.width() / 1.571);
    cairo_set_source_rgba(c, fill_color[0], fill_color[1], fill_color[2],
                          fill_color.alpha());
    cairo_fill(c);
  }

  int gradient_id = bg_.gradient_id_for(mouse_state_);
  if (gradient_id >= 0 && gradient_id < static_cast<int>(gradients.size())) {
    gradients[gradient_id].Draw(c, extents);
  }

  if (b.width() > 0) {
    cairo_set_line_width(c, b.width());

    const int border_width =
        (b.width_for_side(BORDER_LEFT) + b.width_for_side(BORDER_RIGHT)) / 2.0;
    const int border_height =
        (b.width_for_side(BORDER_TOP) + b.width_for_side(BORDER_BOTTOM)) / 2.0;
    DrawRectOnSides(c, b.width_for_side(BORDER_LEFT) / 2.0,
                    b.width_for_side(BORDER_TOP) / 2.0, width_ - border_width,
                    height_ - border_height, b.rounded(), b.mask());

    Color border_color = bg_.border_color_for(mouse_state_);
    cairo_set_source_rgba(c, border_color[0], border_color[1], border_color[2],
                          border_color.alpha());
    cairo_stroke(c);
  }
}

bool Area::RemoveChild(Area* child) {
  auto const& it = std::find(children_.begin(), children_.end(), child);

  if (it != children_.end()) {
    children_.erase(it);
    SetRedraw();
    return true;
  }

  return false;
}

void Area::AddChild(Area* child) {
  children_.push_back(child);
  SetRedraw();
}

void Area::FreeArea() {
  for (auto& child : children_) {
    child->FreeArea();
  }

  children_.clear();
  pix_ = {};
}

void Area::DrawForeground(cairo_t*) { /* defaults to a no-op */
}

std::string Area::GetTooltipText() {
  /* defaults to a no-op */
  return std::string();
}

bool Area::Resize() {
  /* defaults to a no-op */
  return false;
}

void Area::OnChangeLayout() { /* defaults to a no-op */
}

bool Area::HandlesClick(XEvent* event) {
  return IsPointInside(event->xbutton.x, event->xbutton.y);
}

bool Area::OnClick(XEvent* event) {
  if (event->type != ButtonPress && event->type != ButtonRelease) {
    util::log::Error() << "Unexpected X event: " << event->type << '\n';
    return false;
  }
  return true;
}

bool Area::IsPointInside(int x, int y) const {
  bool inside_x = (x >= panel_x_ && x <= panel_x_ + static_cast<int>(width_));
  bool inside_y = (y >= panel_y_ && y <= panel_y_ + static_cast<int>(height_));
  return on_screen_ && inside_x && inside_y;
}

Area* Area::InnermostAreaUnderPoint(int x, int y) {
  if (!IsPointInside(x, y)) {
    return nullptr;
  }

  // Try looking for the innermost child that contains the given point.
  for (auto& child : children_) {
    Area* result = child->InnermostAreaUnderPoint(x, y);
    if (result != nullptr) {
      return result;
    }
  }

  // If no child has it, it has to be contained in this Area object itself.
  return this;
}

Area* Area::MouseOver(Area* previous_area, bool button_pressed) {
  if (previous_area != nullptr && previous_area != this) {
    previous_area->MouseLeave();
  }
  if (!new_panel_config.mouse_effects || !has_mouse_effects_) {
    return nullptr;
  }
  set_mouse_state(button_pressed ? MouseState::kMousePressed
                                 : MouseState::kMouseOver);
  return this;
}

void Area::MouseLeave() { set_mouse_state(MouseState::kMouseNormal); }

void Area::set_has_mouse_effects(bool has_mouse_effects) {
  has_mouse_effects_ = has_mouse_effects;
}

MouseState Area::mouse_state() const { return mouse_state_; }

void Area::set_mouse_state(MouseState new_state) {
  if (new_state != mouse_state_) {
    panel_refresh = true;
  }
  mouse_state_ = new_state;
  need_redraw_ = true;
}

#ifdef _TINT3_DEBUG

std::string Area::GetFriendlyName() const { return "Area"; }

void Area::PrintTreeLevel(unsigned int level) const {
  for (unsigned int i = 0; i < level; ++i) {
    util::log::Debug() << "  ";
  }

  util::log::Debug() << GetFriendlyName() << '('
                     << static_cast<void const*>(this) << ", "
                     << (size_mode_ == SizeMode::kByContent ? "kByContent"
                                                            : "kByLayout")
                     << ")\n";

  for (auto& child : children_) {
    child->PrintTreeLevel(level + 1);
  }
}

void Area::PrintTree() const { PrintTreeLevel(0); }

#endif  // _TINT3_DEBUG

void DrawRect(cairo_t* c, double x, double y, double w, double h, double r) {
  DrawRectOnSides(c, x, y, w, h, r, BORDER_ALL);
}

void DrawRectOnSides(cairo_t* c, double x, double y, double w, double h,
                     double r, unsigned int border_mask) {
  double c1 = 0.55228475 * r;
  cairo_move_to(c, x + r, y);

  // Top line
  if (border_mask & BORDER_TOP) {
    cairo_rel_line_to(c, w - 2 * r, 0);
  } else {
    cairo_rel_move_to(c, w - 2 * r, y);
  }
  // Top right corner
  if (r > 0) {
    if ((border_mask & BORDER_TOP) && (border_mask & BORDER_RIGHT)) {
      cairo_rel_curve_to(c, c1, 0.0, r, c1, r, r);
    } else {
      cairo_rel_move_to(c, r, r);
    }
  }

  // Right line
  if (border_mask & BORDER_RIGHT) {
    cairo_rel_line_to(c, 0, h - 2 * r);
  } else {
    cairo_rel_move_to(c, 0, h - 2 * r);
  }
  // Bottom right corner
  if (r > 0) {
    if ((border_mask & BORDER_RIGHT) && (border_mask & BORDER_BOTTOM)) {
      cairo_rel_curve_to(c, 0.0, c1, c1 - r, r, -r, r);
    } else {
      cairo_rel_move_to(c, -r, r);
    }
  }

  // Bottom line
  if (border_mask & BORDER_BOTTOM) {
    cairo_rel_line_to(c, -w + 2 * r, 0);
  } else {
    cairo_rel_move_to(c, -w + 2 * r, 0);
  }
  // Bottom left corner
  if (r > 0) {
    if ((border_mask & BORDER_LEFT) && (border_mask & BORDER_BOTTOM)) {
      cairo_rel_curve_to(c, -c1, 0, -r, -c1, -r, -r);
    } else {
      cairo_rel_move_to(c, -r, -r);
    }
  }

  // Left line
  if (border_mask & BORDER_LEFT) {
    cairo_rel_line_to(c, 0, -h + 2 * r);
  } else {
    cairo_rel_move_to(c, 0, -h + 2 * r);
  }
  // Top left corner
  if (r > 0) {
    if ((border_mask & BORDER_LEFT) && (border_mask & BORDER_TOP)) {
      cairo_rel_curve_to(c, 0, -c1, r - c1, -r, r, -r);
    } else {
      cairo_rel_move_to(c, r, -r);
    }
  }
}

void ClearPixmap(Pixmap p, int x, int y, int w, int h) {
  Picture pict = XRenderCreatePicture(
      server.dsp, p, XRenderFindVisualFormat(server.dsp, server.visual), 0, 0);
  XRenderColor col = {.red = 0, .green = 0, .blue = 0, .alpha = 0};
  XRenderFillRectangle(server.dsp, PictOpSrc, pict, &col, x, y, w, h);
  XRenderFreePicture(server.dsp, pict);
}

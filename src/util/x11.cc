#include "server.h"
#include "panel.h"
#include "util/x11.h"
#include "util/select.h"

namespace util {
namespace x11 {

ScopedErrorHandler::ScopedErrorHandler(XErrorHandler new_handler)
    : old_handler_(XSetErrorHandler(new_handler)) {}

ScopedErrorHandler::~ScopedErrorHandler() { XSetErrorHandler(old_handler_); }

void XFreeDeleter::operator()(void* data) const { XFree(data); }

EventLoop::EventLoop(Server const* const server)
    : server_(server), x11_file_descriptor_(ConnectionNumber(server_->dsp)) {}

bool EventLoop::RunLoop() {
  bool hidden_dnd = true;

  while (true) {
    if (panel_refresh) {
      panel_refresh = false;

      for (int i = 0; i < nb_panel; i++) {
        Panel& panel = panel1[i];

        if (panel.is_hidden_) {
          XCopyArea(server_->dsp, panel.hidden_pixmap_, panel.main_win_,
                    server_->gc, 0, 0, panel.hidden_width_,
                    panel.hidden_height_, 0, 0);
          XSetWindowBackgroundPixmap(server_->dsp, panel.main_win_,
                                     panel.hidden_pixmap_);
        } else {
          if (panel.temp_pmap) {
            XFreePixmap(server_->dsp, panel.temp_pmap);
          }

          panel.temp_pmap =
              XCreatePixmap(server_->dsp, server_->root_win, panel.width_,
                            panel.height_, server_->depth);
          panel.Render();
          XCopyArea(server_->dsp, panel.temp_pmap, panel.main_win_, server_->gc,
                    0, 0, panel.width_, panel.height_, 0, 0);
        }
      }

      XFlush(server_->dsp);

      Panel* panel = systray.panel_;

      if (refresh_systray && panel != nullptr && !panel->is_hidden_) {
        refresh_systray = 0;
        // tint3 doesn't draw systray icons. it just redraw background.
        XSetWindowBackgroundPixmap(server_->dsp, panel->main_win_,
                                   panel->temp_pmap);
        // force refresh of the icons
        RefreshSystrayIcon();
      }
    }

    util::SyncIoMux mux;
    mux.read_fdset().Add(x11_file_descriptor_);

    if (mux.Select(UpdateNextTimeout())) {
      while (XPending(server_->dsp)) {
        XEvent e;
        XNextEvent(server_->dsp, &e);

#if HAVE_SN
        sn_display_process_event(server_->sn_dsp, &e);
#endif  // HAVE_SN

        Panel* panel = GetPanel(e.xany.window);

        if (panel != nullptr && panel_autohide) {
          if (e.type == EnterNotify) {
            AutohideTriggerShow(panel);
          } else if (e.type == LeaveNotify) {
            AutohideTriggerHide(panel);
          }

          auto XdndPosition = server_->atoms_.at("XdndPosition");
          auto XdndLeave = server_->atoms_.at("XdndLeave");

          if (panel->is_hidden_) {
            if (e.type == ClientMessage &&
                e.xclient.message_type == XdndPosition) {
              hidden_dnd = true;
              AutohideShow(panel);
            } else {
              // discard further processing of this event because
              // the panel is not visible yet
              continue;
            }
          } else if (hidden_dnd && e.type == ClientMessage &&
                     e.xclient.message_type == XdndLeave) {
            hidden_dnd = false;
            AutohideHide(panel);
          }
        }

        auto it = handler_map_.find(e.type);

        if (it != handler_map_.end()) {
          if (it->second != nullptr) {
            it->second(e);
          }
        } else {
          if (default_handler_ != nullptr) {
            default_handler_(e);
          }
        }
      }
    }

    CallbackTimeoutExpired();

    if (signal_pending) {
      // FIXME: why is this even needed here?
      // Cleanup();

      if (signal_pending != SIGUSR1) {
        return false;
      }

      // We're asked to restart tint3.
      // SIGUSR1 is used in case of user-sent signal, composite manager
      // stop/start or XRandR
      return true;
    }
  }

  return false;
}

EventLoop& EventLoop::RegisterHandler(int event,
                                      EventLoop::EventHandler handler) {
  handler_map_[event] = handler;
  return (*this);
}

EventLoop& EventLoop::RegisterHandler(std::initializer_list<int> event_list,
                                      EventLoop::EventHandler handler) {
  for (auto event : event_list) {
    RegisterHandler(event, handler);
  }

  return (*this);
}

EventLoop& EventLoop::RegisterDefaultHandler(EventLoop::EventHandler handler) {
  default_handler_ = handler;
  return (*this);
}

}  // namespace x11
}  // namespace util

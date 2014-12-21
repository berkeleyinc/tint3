/**************************************************************************
*
* Tint3 : taskbar
*
* Copyright (C) 2008 thierry lorthiois (lorthiois@bbsoft.fr) from Omega distribution
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
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**************************************************************************/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <Imlib2.h>

#include <algorithm>

#include "task.h"
#include "taskbar.h"
#include "server.h"
#include "window.h"
#include "panel.h"


/* win_to_task_table holds for every Window an array of tasks. Usually the array contains only one
   element. However for omnipresent windows (windows which are visible in every taskbar) the array
   contains to every Task* on each panel a pointer (i.e. GPtrArray.len == server.nb_desktop)
*/
GHashTable* win_to_task_table;

Task* task_active;
Task* task_drag;
int taskbar_enabled;

Taskbar& Taskbar::set_state(size_t state) {
    bg_ = panel1[0].g_taskbar.background[state];
    pix_ = state_pixmap(state);

    if (taskbarname_enabled) {
        bar_name.bg_ = panel1[0].g_taskbar.background_name[state];
        bar_name.pix_ = bar_name.state_pixmap(state);
    }

    if (panel_mode != MULTI_DESKTOP) {
        on_screen_ = (state != TASKBAR_NORMAL);
    }

    if (on_screen_ == true) {
        if (state_pixmap(state) == 0) {
            need_redraw_ = true;
        }

        if (taskbarname_enabled && bar_name.state_pixmap(state) == 0) {
            bar_name.need_redraw_ = true;
        }

        auto normal_bg = panel1[0].g_taskbar.background[TASKBAR_NORMAL];
        auto active_bg = panel1[0].g_taskbar.background[TASKBAR_ACTIVE];

        if (panel_mode == MULTI_DESKTOP && normal_bg != active_bg) {
            auto it = children_.begin();

            if (taskbarname_enabled) {
                ++it;
            }

            for (; it != children_.end(); ++it) {
                set_task_redraw(static_cast<Task*>(*it));
            }
        }
    }

    panel_refresh = 1;
    return (*this);
}

guint win_hash(gconstpointer key) {
    return (guint) * ((Window*)key);
}
gboolean win_compare(gconstpointer a, gconstpointer b) {
    return (*((Window*)a) == *((Window*)b));
}
void free_ptr_array(gpointer data) {
    g_ptr_array_free(static_cast<GPtrArray*>(data), 1);
}


void DefaultTaskbar() {
    win_to_task_table = 0;
    urgent_timeout = 0;
    urgent_list.clear();
    taskbar_enabled = 0;
    Taskbarname::Default();
}

void CleanupTaskbar() {
    Taskbarname::Cleanup();

    if (win_to_task_table) {
        g_hash_table_foreach(win_to_task_table, TaskbarRemoveTask, 0);
    }

    for (int i = 0 ; i < nb_panel; ++i) {
        Panel& panel = panel1[i];

        for (int j = 0 ; j < panel.nb_desktop_ ; ++j) {
            Taskbar* tskbar = &panel.taskbar_[j];

            for (int k = 0; k < TASKBAR_STATE_COUNT; ++k) {
                tskbar->reset_state_pixmap(k);
            }

            tskbar->FreeArea();
            // remove taskbar from the panel
            auto it = std::find(panel.children_.begin(),
                                panel.children_.end(),
                                tskbar);

            if (it != panel.children_.end()) {
                panel.children_.erase(it);
            }
        }

        if (panel.taskbar_ != nullptr) {
            delete[] panel.taskbar_;
            panel.taskbar_ = nullptr;
        }
    }

    if (win_to_task_table) {
        g_hash_table_destroy(win_to_task_table);
        win_to_task_table = 0;
    }
}


void InitTaskbar() {
    if (win_to_task_table == 0) {
        win_to_task_table = g_hash_table_new_full(win_hash, win_compare, free,
                            free_ptr_array);
    }

    task_active = 0;
    task_drag = 0;
}


void InitTaskbarPanel(Panel* panel) {
    if (panel->g_taskbar.background[TASKBAR_NORMAL] == nullptr) {
        panel->g_taskbar.background[TASKBAR_NORMAL] = backgrounds.front();
        panel->g_taskbar.background[TASKBAR_ACTIVE] = backgrounds.front();
    }

    if (panel->g_taskbar.background_name[TASKBAR_NORMAL] == nullptr) {
        panel->g_taskbar.background_name[TASKBAR_NORMAL] = backgrounds.front();
        panel->g_taskbar.background_name[TASKBAR_ACTIVE] = backgrounds.front();
    }

    if (panel->g_task.bg_ == nullptr) {
        panel->g_task.bg_ = backgrounds.front();
    }

    // taskbar name
    panel->g_taskbar.bar_name_.panel_ = panel;
    panel->g_taskbar.bar_name_.size_mode_ = SIZE_BY_CONTENT;
    panel->g_taskbar.bar_name_.need_resize_ = true;
    panel->g_taskbar.bar_name_.on_screen_ = 1;

    // taskbar
    panel->g_taskbar.parent_ = panel;
    panel->g_taskbar.panel_ = panel;
    panel->g_taskbar.size_mode_ = SIZE_BY_LAYOUT;
    panel->g_taskbar.need_resize_ = true;
    panel->g_taskbar.on_screen_ = 1;

    if (panel_horizontal) {
        panel->g_taskbar.panel_y_ = (panel->bg_->border.width + panel->padding_y_);
        panel->g_taskbar.height_ = panel->height_ - (2 * panel->g_taskbar.panel_y_);
        panel->g_taskbar.bar_name_.panel_y_ = panel->g_taskbar.panel_y_;
        panel->g_taskbar.bar_name_.height_ = panel->g_taskbar.height_;
    } else {
        panel->g_taskbar.panel_x_ = panel->bg_->border.width + panel->padding_y_;
        panel->g_taskbar.width_ = panel->width_ - (2 * panel->g_taskbar.panel_x_);
        panel->g_taskbar.bar_name_.panel_x_ = panel->g_taskbar.panel_x_;
        panel->g_taskbar.bar_name_.width_ = panel->g_taskbar.width_;
    }

    // task
    panel->g_task.panel_ = panel;
    panel->g_task.size_mode_ = SIZE_BY_LAYOUT;
    panel->g_task.need_resize_ = true;
    panel->g_task.on_screen_ = 1;

    if ((panel->g_task.config_asb_mask & (1 << TASK_NORMAL)) == 0) {
        panel->g_task.alpha[TASK_NORMAL] = 100;
        panel->g_task.saturation[TASK_NORMAL] = 0;
        panel->g_task.brightness[TASK_NORMAL] = 0;
    }

    if ((panel->g_task.config_asb_mask & (1 << TASK_ACTIVE)) == 0) {
        panel->g_task.alpha[TASK_ACTIVE] = panel->g_task.alpha[TASK_NORMAL];
        panel->g_task.saturation[TASK_ACTIVE] = panel->g_task.saturation[TASK_NORMAL];
        panel->g_task.brightness[TASK_ACTIVE] = panel->g_task.brightness[TASK_NORMAL];
    }

    if ((panel->g_task.config_asb_mask & (1 << TASK_ICONIFIED)) == 0) {
        panel->g_task.alpha[TASK_ICONIFIED] = panel->g_task.alpha[TASK_NORMAL];
        panel->g_task.saturation[TASK_ICONIFIED] =
            panel->g_task.saturation[TASK_NORMAL];
        panel->g_task.brightness[TASK_ICONIFIED] =
            panel->g_task.brightness[TASK_NORMAL];
    }

    if ((panel->g_task.config_asb_mask & (1 << TASK_URGENT)) == 0) {
        panel->g_task.alpha[TASK_URGENT] = panel->g_task.alpha[TASK_ACTIVE];
        panel->g_task.saturation[TASK_URGENT] = panel->g_task.saturation[TASK_ACTIVE];
        panel->g_task.brightness[TASK_URGENT] = panel->g_task.brightness[TASK_ACTIVE];
    }

    if ((panel->g_task.config_font_mask & (1 << TASK_NORMAL)) == 0)
        panel->g_task.font[TASK_NORMAL] = (Color) {
        {0, 0, 0}, 0
    };

    if ((panel->g_task.config_font_mask & (1 << TASK_ACTIVE)) == 0) {
        panel->g_task.font[TASK_ACTIVE] = panel->g_task.font[TASK_NORMAL];
    }

    if ((panel->g_task.config_font_mask & (1 << TASK_ICONIFIED)) == 0) {
        panel->g_task.font[TASK_ICONIFIED] = panel->g_task.font[TASK_NORMAL];
    }

    if ((panel->g_task.config_font_mask & (1 << TASK_URGENT)) == 0) {
        panel->g_task.font[TASK_URGENT] = panel->g_task.font[TASK_ACTIVE];
    }

    if ((panel->g_task.config_background_mask & (1 << TASK_NORMAL)) == 0) {
        panel->g_task.background[TASK_NORMAL] = backgrounds.front();
    }

    if ((panel->g_task.config_background_mask & (1 << TASK_ACTIVE)) == 0) {
        panel->g_task.background[TASK_ACTIVE] = panel->g_task.background[TASK_NORMAL];
    }

    if ((panel->g_task.config_background_mask & (1 << TASK_ICONIFIED)) == 0) {
        panel->g_task.background[TASK_ICONIFIED] =
            panel->g_task.background[TASK_NORMAL];
    }

    if ((panel->g_task.config_background_mask & (1 << TASK_URGENT)) == 0) {
        panel->g_task.background[TASK_URGENT] = panel->g_task.background[TASK_ACTIVE];
    }

    if (panel_horizontal) {
        panel->g_task.panel_y_ = panel->g_taskbar.panel_y_ +
                                 panel->g_taskbar.background[TASKBAR_NORMAL]->border.width +
                                 panel->g_taskbar.padding_y_;
        panel->g_task.height_ = panel->height_ - (2 * panel->g_task.panel_y_);
    } else {
        panel->g_task.panel_x_ = panel->g_taskbar.panel_x_ +
                                 panel->g_taskbar.background[TASKBAR_NORMAL]->border.width +
                                 panel->g_taskbar.padding_y_;
        panel->g_task.width_ = panel->width_ - (2 * panel->g_task.panel_x_);
        panel->g_task.height_ = panel->g_task.maximum_height;
    }

    for (int j = 0; j < TASK_STATE_COUNT; ++j) {
        if (panel->g_task.background[j] == nullptr) {
            panel->g_task.background[j] = backgrounds.front();
        }

        if (panel->g_task.background[j]->border.rounded > panel->g_task.height_ /
            2) {
            printf("task%sbackground_id has a too large rounded value. Please fix your tint3rc\n",
                   j == 0 ? "_" : j == 1 ? "_active_" : j == 2 ? "_iconified_" : "_urgent_");
            /* backgrounds.push_back(*panel->g_task.background[j]); */
            /* panel->g_task.background[j] = backgrounds.back(); */
            panel->g_task.background[j]->border.rounded = panel->g_task.height_ / 2;
        }
    }

    // compute vertical position : text and icon
    int height_ink, height;
    GetTextSize(panel->g_task.font_desc, &height_ink, &height, panel->height_,
                "TAjpg", 5);

    if (!panel->g_task.maximum_width && panel_horizontal) {
        panel->g_task.maximum_width = server.monitor[panel->monitor_].width;
    }

    panel->g_task.text_posx = panel->g_task.background[0]->border.width +
                              panel->g_task.padding_x_lr_;
    panel->g_task.text_height = panel->g_task.height_ -
                                (2 * panel->g_task.padding_y_);

    if (panel->g_task.icon) {
        panel->g_task.icon_size1 = panel->g_task.height_ - (2 *
                                   panel->g_task.padding_y_);
        panel->g_task.text_posx += panel->g_task.icon_size1;
        panel->g_task.icon_posy = (panel->g_task.height_ -
                                   panel->g_task.icon_size1) / 2;
    }

    //printf("monitor %d, task_maximum_width %d\n", panel->monitor, panel->g_task.maximum_width);

    panel->nb_desktop_ = server.nb_desktop;
    panel->taskbar_ = new Taskbar[server.nb_desktop];

    for (int j = 0 ; j < panel->nb_desktop_ ; j++) {
        Taskbar* tskbar = &panel->taskbar_[j];

        // TODO: nuke this from planet Earth ASAP - horrible hack to mimick the
        // original memcpy() call
        tskbar->CloneArea(panel->g_taskbar);

        tskbar->desktop = j;

        if (j == server.desktop) {
            tskbar->bg_ = panel->g_taskbar.background[TASKBAR_ACTIVE];
        } else {
            tskbar->bg_ = panel->g_taskbar.background[TASKBAR_NORMAL];
        }
    }

    Taskbarname::InitPanel(panel);
}


void TaskbarRemoveTask(gpointer key, gpointer value, gpointer user_data) {
    RemoveTask(TaskGetTask(*static_cast<Window*>(key)));
}


Task* TaskGetTask(Window win) {
    GPtrArray* task_group = TaskGetTasks(win);

    if (task_group) {
        return static_cast<Task*>(g_ptr_array_index(task_group, 0));
    }

    return 0;
}


GPtrArray* TaskGetTasks(Window win) {
    if (win_to_task_table && taskbar_enabled) {
        return static_cast<GPtrArray*>(g_hash_table_lookup(win_to_task_table, &win));
    }

    return 0;
}


void TaskRefreshTasklist() {
    if (!taskbar_enabled) {
        return;
    }

    int num_results;
    Window* win = static_cast<Window*>(ServerGetProperty(
                                           server.root_win,
                                           server.atoms_["_NET_CLIENT_LIST"],
                                           XA_WINDOW,
                                           &num_results));

    if (!win) {
        return;
    }

    GList* win_list = g_hash_table_get_keys(win_to_task_table);
    int i;

    for (GList* it = win_list; it; it = it->next) {
        for (i = 0; i < num_results; i++) {
            if (*static_cast<Window*>(it->data) == win[i]) {
                break;
            }
        }

        if (i == num_results) {
            TaskbarRemoveTask(it->data, 0, 0);
        }
    }

    g_list_free(win_list);

    // Add any new
    for (i = 0; i < num_results; i++) {
        if (!TaskGetTask(win[i])) {
            AddTask(win[i]);
        }
    }

    XFree(win);
}


void Taskbar::DrawForeground(cairo_t* /* c */) {
    size_t state = (desktop == server.desktop ? TASKBAR_ACTIVE : TASKBAR_NORMAL);
    set_state_pixmap(state, pix_);
}


bool Taskbar::Resize() {
    int horizontal_size = (panel_->g_task.text_posx +
                           panel_->g_task.bg_->border.width
                           + panel_->g_task.padding_x_);

    if (panel_horizontal) {
        ResizeByLayout(panel_->g_task.maximum_width);

        int text_width_ = panel_->g_task.maximum_width;
        auto it = children_.begin();

        if (taskbarname_enabled) {
            ++it;
        }

        if (it != children_.end()) {
            text_width_ = static_cast<Task*>(*it)->width_;
        }

        text_width = (text_width_ - horizontal_size);
    } else {
        ResizeByLayout(panel_->g_task.maximum_height);
        text_width = (width_ - (2 * panel_->g_taskbar.padding_y_) - horizontal_size);
    }

    return false;
}


void Taskbar::OnChangeLayout() {
    // reset Pixmap when position/size changed
    for (int k = 0; k < TASKBAR_STATE_COUNT; ++k) {
        reset_state_pixmap(k);
    }

    pix_ = 0;
    need_redraw_ = true;
}


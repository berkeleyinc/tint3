/**************************************************************************
 * Tint3 : launcher
 *
 * Copyright (C) 2010       (mrovi@interfete-web-club.com)
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
#include <signal.h>
#include <unistd.h>
#include <xsettings-client.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <list>
#include <memory>
#include <set>
#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"

#include "launcher.hh"
#include "launcher/desktop_entry.hh"
#include "panel.hh"
#include "server.hh"
#include "startup_notification.hh"
#include "subprocess.hh"
#include "taskbar/taskbar.hh"
#include "util/fs.hh"
#include "util/log.hh"
#include "util/xdg.hh"

bool launcher_enabled = false;
int launcher_max_icon_size;
bool launcher_tooltip_enabled;
int launcher_alpha;
int launcher_saturation;
int launcher_brightness;
std::string icon_theme_name;
XSettingsClient* xsettings_client;

namespace {

const char kIconFallback[] = "application-x-executable";

void XSettingsNotifyCallback(const char* name, XSettingsAction action,
                             XSettingsSetting* setting, void* data) {
  static std::string kIconThemeNameSetting = "Net/IconThemeName";

  if (name == nullptr || setting == nullptr) return;

  // Only watch for new settings values or their changes.
  if (action != XSETTINGS_ACTION_NEW && action != XSETTINGS_ACTION_CHANGED)
    return;

  // If an unrelated setting changed, or it's of the wrong type, abort.
  if (kIconThemeNameSetting != name || setting->type != XSETTINGS_TYPE_STRING)
    return;

  // Avoid refreshing everything if we have already loaded the same icon theme.
  if (!icon_theme_name.empty() && icon_theme_name == setting->data.v_string)
    return;

  icon_theme_name = setting->data.v_string;
  for (Panel& p : panels) {
    Launcher& launcher = p.launcher_;
    launcher.CleanupTheme();
    launcher.LoadThemes();
    launcher.LoadIcons();
    launcher.need_resize_ = true;
  }
}

}  // namespace

bool LauncherReadDesktopFile(std::string const& path, DesktopEntry* entry);
util::imlib2::Image ScaleIcon(Imlib_Image original, int icon_size);
void FreeIconTheme(IconTheme* theme);

void DefaultLauncher() {
  launcher_enabled = false;
  launcher_max_icon_size = 0;
  launcher_tooltip_enabled = 0;
  launcher_alpha = 100;
  launcher_saturation = 0;
  launcher_brightness = 0;
  icon_theme_name.clear();
  xsettings_client = nullptr;
}

void InitLauncher() {
  if (launcher_enabled) {
    // if XSETTINGS manager running, tint3 read the icon_theme_name.
    xsettings_client =
        xsettings_client_new(server.dsp, server.root_window(),
                             XSettingsNotifyCallback, nullptr, nullptr);
  }
}

void Launcher::InitPanel(Panel* panel) {
  Launcher& launcher = panel->launcher_;
  launcher.parent_ = panel;
  launcher.panel_ = panel;
  launcher.size_mode_ = SizeMode::kByContent;
  launcher.need_resize_ = true;
  launcher.need_redraw_ = true;

  // These will be overridden by Launcher::Resize() below, but need to have
  // a default non-zero value in case the configuration doesn't specify any
  // dimensions, or we might end up having a division by zero.
  unsigned int short_side =
      std::max(1U, panel->horizontal() ? panel->height_ : panel->width_);
  launcher.width_ = short_side;
  launcher.height_ = short_side;

  // check consistency
  if (launcher.list_apps_.empty()) {
    return;
  }

  launcher.on_screen_ = true;
  panel_refresh = true;

  launcher.LoadThemes();
  launcher.LoadIcons();
}

void CleanupLauncher() {
  if (xsettings_client) xsettings_client_destroy(xsettings_client);

  for (Panel& p : panels) {
    p.launcher_.CleanupTheme();
  }

  panel_config.launcher_.list_apps_.clear();
  icon_theme_name.clear();
  launcher_enabled = false;
}

void Launcher::CleanupTheme() {
  FreeArea();

  for (auto const& icon : list_icons_) {
    if (icon) delete icon;
  }
  list_icons_.clear();

  for (auto const& theme : list_themes_) {
    if (theme) delete theme;
  }
  list_themes_.clear();
}

int Launcher::GetIconSize() const {
  Border const& b = bg_.border();
  const int left_right_border =
      b.width_for_side(BORDER_LEFT) + b.width_for_side(BORDER_RIGHT);
  const int top_bottom_border =
      b.width_for_side(BORDER_TOP) + b.width_for_side(BORDER_BOTTOM);

  int icon_size = panel_->horizontal() ? height_ : width_;
  icon_size -= std::max(left_right_border, top_bottom_border);
  icon_size -= (2 * padding_y_);

  if (launcher_max_icon_size > 0) {
    icon_size = std::max(icon_size, launcher_max_icon_size);
  }
  return icon_size;
}

bool Launcher::Resize() {
  int icons_per_column = 1, icons_per_row = 1, margin = 0;
  int icon_size = GetIconSize();

  // Resize icons if necessary
  for (auto& launcher_icon : list_icons_) {
    if (launcher_icon->icon_size_ != icon_size ||
        !launcher_icon->icon_original_) {
      launcher_icon->icon_size_ = icon_size;
      launcher_icon->width_ = launcher_icon->icon_size_;
      launcher_icon->height_ = launcher_icon->icon_size_;

      // Get the path for an icon file with the new size
      std::string new_icon_path =
          GetIconPath(launcher_icon->icon_name_, launcher_icon->icon_size_);

      if (new_icon_path.empty()) {
        // Draw a blank icon
        launcher_icon->icon_original_.Free();
        launcher_icon->icon_scaled_.Free();
        launcher_icon->icon_hover_.Free();
        launcher_icon->icon_pressed_.Free();
        new_icon_path = GetIconPath(kIconFallback, launcher_icon->icon_size_);

        if (!new_icon_path.empty()) {
          launcher_icon->icon_original_ =
              imlib_load_image(new_icon_path.c_str());

          util::log::Error() << __FILE__ << ':' << __LINE__ << ": Using icon "
                             << new_icon_path.c_str() << '\n';
        }

        launcher_icon->icon_scaled_ =
            ScaleIcon(launcher_icon->icon_original_, icon_size);
        launcher_icon->icon_hover_ = launcher_icon->icon_scaled_;
        if (new_panel_config.mouse_effects) {
          launcher_icon->icon_hover_.AdjustASB(
              new_panel_config.mouse_hover_alpha,
              new_panel_config.mouse_hover_saturation / 100.0f,
              new_panel_config.mouse_hover_brightness / 100.0f);
        }
        launcher_icon->icon_pressed_ = launcher_icon->icon_scaled_;
        if (new_panel_config.mouse_effects) {
          launcher_icon->icon_pressed_.AdjustASB(
              new_panel_config.mouse_pressed_alpha,
              new_panel_config.mouse_pressed_saturation / 100.0f,
              new_panel_config.mouse_pressed_brightness / 100.0f);
        }
        continue;
      }

      if (!launcher_icon->icon_path_.empty() &&
          new_icon_path == launcher_icon->icon_path_) {
        // If it's the same file just rescale
        launcher_icon->icon_scaled_ =
            ScaleIcon(launcher_icon->icon_original_, icon_size);
        launcher_icon->icon_hover_ = launcher_icon->icon_scaled_;
        launcher_icon->icon_hover_.AdjustASB(100, 0.0, +0.1);
        launcher_icon->icon_pressed_ = launcher_icon->icon_scaled_;
        launcher_icon->icon_pressed_.AdjustASB(100, 0.0, -0.1);

        util::log::Error() << __FILE__ << ':' << __LINE__ << ": Using icon "
                           << launcher_icon->icon_path_ << '\n';
      } else {
        // Load the new file and scale
        launcher_icon->icon_original_ = imlib_load_image(new_icon_path.c_str());
        launcher_icon->icon_scaled_ =
            ScaleIcon(launcher_icon->icon_original_, launcher_icon->icon_size_);
        launcher_icon->icon_hover_ = launcher_icon->icon_scaled_;
        launcher_icon->icon_hover_.AdjustASB(100, 0.0, +0.1);
        launcher_icon->icon_pressed_ = launcher_icon->icon_scaled_;
        launcher_icon->icon_pressed_.AdjustASB(100, 0.0, -0.1);
        launcher_icon->icon_path_ = new_icon_path;

        util::log::Error() << __FILE__ << ':' << __LINE__ << ": Using icon "
                           << launcher_icon->icon_path_ << '\n';
      }
    }
  }

  Border const& b = bg_.border();
  size_t count = list_icons_.size();

  if (panel_->horizontal()) {
    width_ = 0;

    if (count != 0) {
      int height = height_ - 2 * b.width() - 2 * padding_y_;
      // here icons_per_column always higher than 0
      icons_per_column =
          std::max(1, (height + padding_x_) / (icon_size + padding_x_));
      margin = height - (icons_per_column - 1) * (icon_size + padding_x_) -
               icon_size;
      icons_per_row =
          count / icons_per_column + (count % icons_per_column != 0);
      width_ = (2 * b.width()) + (2 * padding_x_lr_) +
               (icon_size * icons_per_row) + ((icons_per_row - 1) * padding_x_);
    }
  } else {
    height_ = 0;

    if (count != 0) {
      int width = width_ - 2 * b.width() - 2 * padding_y_;
      // here icons_per_row always higher than 0
      icons_per_row =
          std::max(1, (width + padding_x_) / (icon_size + padding_x_));
      margin =
          width - (icons_per_row - 1) * (icon_size + padding_x_) - icon_size;
      icons_per_column = count / icons_per_row + (count % icons_per_row != 0);
      height_ = (2 * b.width()) + (2 * padding_x_lr_) +
                (icon_size * icons_per_column) +
                ((icons_per_column - 1) * padding_x_);
    }
  }

  int posx, posy, start;
  if (panel_->horizontal()) {
    posy = start = b.width_for_side(BORDER_TOP) + padding_y_ + margin / 2;
    posx = b.width_for_side(BORDER_LEFT) + padding_x_lr_;
  } else {
    posx = start = b.width_for_side(BORDER_LEFT) + padding_y_ + margin / 2;
    posy = b.width_for_side(BORDER_TOP) + padding_x_lr_;
  }

  int i = 1;

  for (auto& launcher_icon : list_icons_) {
    launcher_icon->y_ = posy;
    launcher_icon->x_ = posx;

    if (panel_->horizontal()) {
      if (i % icons_per_column) {
        posy += (icon_size + padding_x_);
      } else {
        posy = start;
        posx += (icon_size + padding_x_);
      }
    } else {
      if (i % icons_per_row) {
        posx += (icon_size + padding_x_);
      } else {
        posx = start;
        posy += (icon_size + padding_x_);
      }
    }

    ++i;
  }

  return true;
}

LauncherIcon::LauncherIcon() : Area() { set_has_mouse_effects(true); }

// Here we override the default layout of the icons; normally Area layouts its
// children
// in a stack; we need to layout them in a kind of table
void LauncherIcon::OnChangeLayout() {
  panel_y_ = (parent_->panel_y_ + y_);
  panel_x_ = (parent_->panel_x_ + x_);
}

std::string LauncherIcon::GetTooltipText() {
  return launcher_tooltip_enabled ? icon_tooltip_ : std::string();
}

void LauncherIcon::DrawForeground(cairo_t* c) {
  Imlib_Image image = icon_scaled_;
  if (new_panel_config.mouse_effects) {
    if (mouse_state() == MouseState::kMouseOver)
      image = icon_hover_;
    else if (mouse_state() == MouseState::kMousePressed)
      image = icon_pressed_;
  }
  if (image) RenderImage(&server, pix_, image, 0, 0);
}

bool LauncherIcon::OnClick(XEvent* event) {
  if (!Area::OnClick(event)) {
    return false;
  }

  StartupNotification sn{server.sn_dsp, server.screen};
  sn.set_description("Application launched from tint3");
  sn.set_name(icon_tooltip_);
  sn.Initiate(cmd_, event->xbutton.time);

  pid_t child_pid = ShellExec(cmd_, child_callback{[&sn] {
                                sn.IncrementRef();
                                sn.SetupChildProcess();
                              }});

#if HAVE_SN
  if (child_pid > 0) {
    server.pids[child_pid] = sn;
  }
#endif  // HAVE_SN

  return (child_pid > 0);
}

#ifdef _TINT3_DEBUG

std::string LauncherIcon::GetFriendlyName() const { return "LauncherIcon"; }

#endif  // _TINT3_DEBUG

util::imlib2::Image ScaleIcon(Imlib_Image original, int icon_size) {
  Imlib_Image icon_scaled;

  if (original) {
    imlib_context_set_image(original);
    icon_scaled = imlib_create_cropped_scaled_image(
        0, 0, imlib_image_get_width(), imlib_image_get_height(), icon_size,
        icon_size);
    imlib_context_set_image(icon_scaled);
    imlib_image_set_has_alpha(1);
    DATA32* data = imlib_image_get_data();
    AdjustASB(data, icon_size, icon_size, launcher_alpha,
              (float)launcher_saturation / 100,
              (float)launcher_brightness / 100);
    imlib_image_put_back_data(data);
  } else {
    icon_scaled = imlib_create_image(icon_size, icon_size);
    imlib_context_set_image(icon_scaled);
    imlib_context_set_color(255, 255, 255, 255);
    imlib_image_fill_rectangle(0, 0, icon_size, icon_size);
  }

  return icon_scaled;
}

// Splits line at first '=' and returns 1 if successful, and parts are not
// empty
// key and value point to the parts
// http://standards.freedesktop.org/icon-theme-spec/
bool ParseThemeLine(std::string const& line, std::string& key,
                    std::string& value) {
  bool found = false;

  for (auto it = line.cbegin(); it != line.cend(); ++it) {
    if (*it == '=') {
      key.assign(line.cbegin(), it);
      value.assign(it + 1, line.cend());
      found = true;
      break;
    }
  }

  return (found && !key.empty() && !value.empty());
}

IconTheme::~IconTheme() {
  for (auto const& dir : list_directories) {
    delete dir;
  }
}

// TODO Use UTF8 when parsing the file
IconTheme* LoadTheme(std::string const& name) {
  if (name.empty()) {
    return nullptr;
  }

  std::vector<std::string> candidates = {
      util::fs::HomeDirectory() / ".local" / "share" / "icons" / name /
          "index.theme",
      util::fs::HomeDirectory() / ".icons" / name / "index.theme",
      util::fs::Path("/usr/share/icons") / name / "index.theme",
      util::fs::Path("/usr/share/pixmaps") / name / "index.theme"};

  auto it = std::find_if(
      candidates.begin(), candidates.end(),
      [](util::fs::Path const& item) { return util::fs::FileExists(item); });
  if (it == candidates.end()) {
    return nullptr;
  }

  std::string file_name = (*it);

  auto theme = new IconTheme();
  theme->name = name;
  theme->list_inherits.clear();
  theme->list_directories.clear();

  IconThemeDir* current_dir = nullptr;
  bool inside_header = true;

  bool read = util::fs::ReadFileByLine(file_name, [&](std::string const& data) {
    std::string line(data);
    absl::StripAsciiWhitespace(&line);

    if (line.empty()) {
      return true;
    }

    std::string key, value;

    if (inside_header) {
      if (ParseThemeLine(line, key, value)) {
        if (key == "Inherits") {
          // value is like oxygen,wood,default
          absl::StripAsciiWhitespace(&value);
          std::vector<std::string> names = absl::StrSplit(value, ',');
          std::copy(names.begin(), names.end(),
                    std::back_inserter(theme->list_inherits));
        } else if (key == "Directories") {
          // value is like
          // 48x48/apps,48x48/mimetypes,32x32/apps,scalable/apps,scalable/mimetypes
          absl::StripAsciiWhitespace(&value);
          for (auto name : absl::StrSplit(value, ',')) {
            auto dir = new IconThemeDir();
            dir->name = std::string(name);
            dir->max_size = dir->min_size = dir->size = -1;
            dir->type = IconType::kThreshold;
            dir->threshold = 2;
            theme->list_directories.push_back(dir);
          }
        }
      }
    } else if (current_dir != nullptr) {
      if (ParseThemeLine(line, key, value)) {
        if (key == "Size") {
          // value is like 24
          if (!util::string::ToNumber(value, &current_dir->size)) {
            util::log::Error() << "invalid number: \"" << value << "\"\n";
            return false;
          }

          if (current_dir->max_size == -1) {
            current_dir->max_size = current_dir->size;
          }

          if (current_dir->min_size == -1) {
            current_dir->min_size = current_dir->size;
          }
        } else if (key == "MaxSize") {
          // value is like 24
          if (!util::string::ToNumber(value, &current_dir->max_size)) {
            util::log::Error() << "invalid number: \"" << value << "\"\n";
            return false;
          }
        } else if (key == "MinSize") {
          // value is like 24
          if (!util::string::ToNumber(value, &current_dir->min_size)) {
            util::log::Error() << "invalid number: \"" << value << "\"\n";
            return false;
          }
        } else if (key == "Threshold") {
          // value is like 2
          if (!util::string::ToNumber(value, &current_dir->threshold)) {
            util::log::Error() << "invalid number: \"" << value << "\"\n";
            return false;
          }
        } else if (key == "Type") {
          // value is Fixed, Scalable or Threshold : default to scalable
          // for
          // unknown Type.
          if (value == "Fixed") {
            current_dir->type = IconType::kFixed;
          } else if (value == "Threshold") {
            current_dir->type = IconType::kThreshold;
          } else {
            current_dir->type = IconType::kScalable;
          }
        } else if (key == "Context") {
          // usual values: Actions, Applications, Devices, FileSystems,
          // MimeTypes
          current_dir->context = value;
        }
      }
    }

    if (line[0] == '[' && line[line.length() - 1] == ']' &&
        line != "[Icon Theme]") {
      inside_header = false;
      current_dir = nullptr;
      std::string dir_name = line.substr(1, line.length() - 2);

      for (auto const& dir : theme->list_directories) {
        if (dir_name == dir->name) {
          current_dir = dir;
          break;
        }
      }
    }

    return true;
  });

  if (!read) {
    delete theme;
    util::log::Error() << "Could not open theme \"" << file_name << "\"\n";
    return nullptr;
  }

  return theme;
}

namespace {

void ExpandExec(launcher::desktop_entry::Group* group,
                std::string const& path) {
  if (!group->HasEntry("Exec")) {
    return;
  }

  std::string exec{group->GetEntry<std::string>("Exec")};
  std::string expanded;

  for (auto c = exec.begin(); c != exec.end(); ++c) {
    if (*c == '\\') {
      ++c;

      if (*c == '\0') {
        break;
      }

      // Copy the escaped char
      if (*c != '%') {
        expanded.push_back('\\');
      }

      expanded.push_back(*c);
    } else if (*c == '%') {
      ++c;

      if (*c == '\0') {
        break;
      }

      if (*c == 'i' && group->HasEntry("Icon")) {
        // TODO: this should really be a vector of args, otherwise we need to
        // introduce proper escaping too...
        absl::StrAppendFormat(&expanded, "--icon '%s'",
                              group->GetEntry<std::string>("Icon"));
      } else if (*c == 'c' && group->HasEntry("Name")) {
        absl::StrAppendFormat(&expanded, "'%s'",
                              group->GetEntry<std::string>("Name"));
      } else if (*c == 'f' || *c == 'F') {
        // Ignore the expansions in this case, we have no files to pass to the
        // executable.
        // TODO: this could not be true in cases of Drag and Drop.
      } else if (*c == 'u' || *c == 'U') {
        // Ignore the expansions in this case, we have no URLs to pass to the
        // executable.
        // TODO: this could not be true in cases of Drag and Drop.
      }
    } else {
      expanded.push_back(*c);
    }
  }

  group->AddEntry("Exec", expanded);
}

bool ParseDesktopFile(std::string const& contents,
                      launcher::desktop_entry::DesktopEntry* output) {
  launcher::desktop_entry::Parser desktop_entry_parser;
  parser::Parser p{launcher::desktop_entry::kLexer, &desktop_entry_parser};
  if (!p.Parse(contents)) {
    return false;
  }

  launcher::desktop_entry::DesktopEntry desktop_entry{
      desktop_entry_parser.GetDesktopEntry()};
  output->assign(desktop_entry.begin(), desktop_entry.end());
  return true;
}

}  // namespace

bool FindDesktopEntry(std::string const& name, std::string* output_path) {
  // First, check if the given parameter is already a valid path
  if (util::fs::FileExists(name)) {
    output_path->assign(name);
    return true;
  }

  // Second, try and find the given .desktop entry name in a standard location
  std::vector<std::string> candidates = {
      util::xdg::basedir::DataHome() / "applications" / name,
      util::fs::HomeDirectory() / ".local" / "share" / "applications" / name,
      util::fs::Path("/usr/local/share/applications") / name,
      util::fs::Path("/usr/share/applications") / name,
      util::fs::Path("/opt/share/applications") / name,
  };

  auto it = std::find_if(
      candidates.begin(), candidates.end(),
      [](util::fs::Path const& item) { return util::fs::FileExists(item); });
  if (it != candidates.end()) {
    output_path->assign(*it);
    return true;
  }

  for (auto const& dir : util::xdg::basedir::DataDirs()) {
    std::string resolved_path = util::fs::Path{dir} / "applications" / name;
    if (util::fs::FileExists(resolved_path)) {
      output_path->assign(resolved_path);
      return true;
    }
  }

  // Give up and fail
  return false;
}

// Populates the list_icons list
void Launcher::LoadIcons() {
  // Load apps (.desktop style launcher items)
  for (auto const& path : list_apps_) {
    std::string resolved_path;
    if (!FindDesktopEntry(path, &resolved_path)) {
      util::log::Error() << "File \"" << path << "\" not found, skipping\n";
      continue;
    }

    util::fs::ReadFile(resolved_path, [&](std::string const& contents) {
      launcher::desktop_entry::DesktopEntry entry;
      if (!ParseDesktopFile(contents, &entry)) {
        util::log::Error() << "Failed parsing \"" << path << "\", skipping.\n";
        return false;
      }

      // Reference to the first group, "Desktop Entry".
      auto& de = entry[0];

      if (!de.IsEntry<std::string>("Type") ||
          de.GetEntry<std::string>("Type") != "Application") {
        util::log::Error() << "Desktop entry \"" << path << "\" not of type "
                           << "\"Application\", skipping.\n";
        return false;
      }

      if (de.IsEntry<std::string>("Exec")) {
        ExpandExec(&de, path);

        auto launcher_icon = new LauncherIcon();
        launcher_icon->parent_ = this;
        launcher_icon->panel_ = panel_;
        launcher_icon->size_mode_ = SizeMode::kByContent;
        launcher_icon->need_resize_ = false;
        launcher_icon->need_redraw_ = true;
        launcher_icon->bg_ = backgrounds.front();
        launcher_icon->on_screen_ = true;

        launcher_icon->is_app_desktop_ = true;
        launcher_icon->cmd_ = de.GetEntry<std::string>("Exec");
        launcher_icon->icon_name_ = de.HasEntry("Icon")
                                        ? de.GetEntry<std::string>("Icon")
                                        : kIconFallback;
        launcher_icon->icon_size_ = 1;
        if (de.HasEntry("Comment")) {
          launcher_icon->icon_tooltip_ =
              launcher::desktop_entry::BestLocalizedEntry(de, "Comment");
        } else if (de.HasEntry("Name")) {
          launcher_icon->icon_tooltip_ =
              launcher::desktop_entry::BestLocalizedEntry(de, "Name");
        } else {
          launcher_icon->icon_tooltip_ = de.GetEntry<std::string>("Exec");
        }

        list_icons_.push_back(launcher_icon);
        AddChild(launcher_icon);
      }

      return true;
    });
  }
}

// Populates the list_themes list
bool Launcher::LoadThemes() {
  // load the user theme, all the inherited themes recursively (DFS), and the
  // hicolor theme
  // avoid inheritance loops
  if (icon_theme_name.empty()) {
    util::log::Error() << "Missing launcher theme, defaulting to 'hicolor'.\n";
    icon_theme_name = "hicolor";
  } else {
    util::log::Error() << "Loading \"" << icon_theme_name << "\". Icon theme:";
  }

  std::list<std::string> queue{icon_theme_name};
  std::set<std::string> queued{icon_theme_name};
  bool icon_theme_name_loaded = false;

  while (!queue.empty()) {
    if (queue.empty()) {
      if (queued.count("hicolor") != 0) {
        // hicolor loaded
        break;
      }

      queue.push_back("hicolor");
      queued.insert("hicolor");
    }

    std::string name(queue.front());
    queue.pop_front();

    util::log::Error() << " '" << name << "',";
    auto theme = LoadTheme(name);
    if (theme == nullptr) {
      continue;
    }

    list_themes_.push_back(theme);
    if (name == icon_theme_name) {
      icon_theme_name_loaded = true;
    }

    auto position = queue.begin();
    for (auto const& item : theme->list_inherits) {
      if (queued.count(item) == 0) {
        queue.insert(position++, item);
        queued.insert(item);
      }
    }
  }

  util::log::Error() << '\n';
  return icon_theme_name_loaded;
}

int DirectoryMatchesSize(IconThemeDir* dir, int size) {
  if (dir->type == IconType::kFixed) {
    return dir->size == size;
  } else if (dir->type == IconType::kScalable) {
    return dir->min_size <= size && size <= dir->max_size;
  } else { /*if (dir->type == IconType::kThreshold)*/
    return dir->size - dir->threshold <= size &&
           size <= dir->size + dir->threshold;
  }
}

int DirectorySizeDistance(IconThemeDir* dir, int size) {
  if (dir->type == IconType::kFixed) {
    return abs(dir->size - size);
  } else if (dir->type == IconType::kScalable) {
    if (size < dir->min_size) {
      return dir->min_size - size;
    } else if (size > dir->max_size) {
      return size - dir->max_size;
    }

    return 0;
  } else { /*if (dir->type == IconType::kThreshold)*/
    if (size < dir->size - dir->threshold) {
      return dir->min_size - size;
    } else if (size > dir->size + dir->threshold) {
      return size - dir->max_size;
    }

    return 0;
  }
}

// Returns the full path to an icon file (or nullptr) given the icon name
std::string Launcher::GetIconPath(std::string const& icon_name, int size) {
  if (icon_name.empty()) {
    return std::string();
  }

  // If the icon_name is already a path and the file exists, return it
  if (icon_name[0] == '/') {
    if (util::fs::FileExists(icon_name)) {
      return icon_name;
    }

    return std::string();
  }

  std::vector<std::string> basenames{
      util::fs::HomeDirectory() / ".icons",
      util::fs::HomeDirectory() / ".local" / "share" / "icons",
      "/usr/local/share/icons",
      "/usr/local/share/pixmaps",
      "/usr/share/icons",
      "/usr/share/pixmaps",
  };

  std::vector<std::string> extensions{".png", ".xpm"};

  // if the icon name already contains one of the extensions (e.g. vlc.png
  // instead of vlc) add a special entry
  for (auto const& extension : extensions) {
    if (icon_name.length() > extension.length() &&
        icon_name.substr(icon_name.length() - extension.length()) ==
            extension) {
      extensions.push_back("");
      break;
    }
  }

  // Stage 1: best size match
  // Contrary to the freedesktop spec, we are not choosing the closest icon in
  // size, but the next larger icon
  // otherwise the quality is usually crap (for size 22, if you can choose 16
  // or
  // 32, you're better with 32)
  // We do fallback to the closest size if we cannot find a larger or equal
  // icon

  // These 3 variables are used for keeping the closest size match
  int minimal_size = INT_MAX;
  std::string best_file_name;
  IconTheme* best_file_theme = nullptr;

  // These 3 variables are used for keeping the next larger match
  int next_larger_size = -1;
  std::string next_larger;
  IconTheme* next_larger_theme = nullptr;

  for (auto const& theme : list_themes_) {
    for (auto const& dir : theme->list_directories) {
      for (auto const& base_name : basenames) {
        for (auto const& extension : extensions) {
          std::string icon_file_name(icon_name + extension);
          std::string file_name = util::fs::BuildPath(
              {base_name, theme->name, dir->name, icon_file_name});

          if (util::fs::FileExists(file_name)) {
            // Closest match
            if (DirectorySizeDistance(dir, size) < minimal_size &&
                (!best_file_theme || theme == best_file_theme)) {
              best_file_name = file_name;
              minimal_size = DirectorySizeDistance(dir, size);
              best_file_theme = theme;
            }

            // Next larger match
            if (dir->size >= size &&
                (next_larger_size == -1 || dir->size < next_larger_size) &&
                (!next_larger_theme || theme == next_larger_theme)) {
              next_larger = file_name;
              next_larger_size = dir->size;
              next_larger_theme = theme;
            }
          }
        }
      }
    }
  }

  if (!next_larger.empty()) {
    return next_larger;
  }

  if (!best_file_name.empty()) {
    return best_file_name;
  }

  // Stage 2: look in unthemed icons
  for (auto const& base_name : basenames) {
    for (auto const& extension : extensions) {
      std::string icon_file_name(icon_name + extension);
      std::string file_name = util::fs::BuildPath({base_name, icon_file_name});

      if (util::fs::FileExists(file_name)) {
        return file_name;
      }
    }
  }

  util::log::Error() << "Could not find icon " << icon_name.c_str() << '\n';
  return std::string();
}

#ifdef _TINT3_DEBUG

std::string Launcher::GetFriendlyName() const { return "Launcher"; }

#endif  // _TINT3_DEBUG

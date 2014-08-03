/**************************************************************************
 * Copyright (C) 2010       (mrovi@interfete-web-club.com)
 *
 *
 **************************************************************************/

#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <vector>

#include "common.h"
#include "area.h"
#include "xsettings-client.h"

class LauncherIcon : public Area {
  public:
    Imlib_Image icon_scaled;
    Imlib_Image icon_original;
    char* cmd;
    char* icon_name;
    char* icon_path;
    char* icon_tooltip;
    int icon_size;
    int is_app_desktop;
    int x, y;
};

struct DesktopEntry {
    char* name;
    char* exec;
    char* icon;
};

#define ICON_DIR_TYPE_SCALABLE 0
#define ICON_DIR_TYPE_FIXED 1
#define ICON_DIR_TYPE_THRESHOLD 2
struct IconThemeDir {
    char* name;
    int size;
    int type;
    int max_size;
    int min_size;
    int threshold;
    char* context;
};

struct IconTheme {
    char* name;
    GSList* list_inherits; // each item is a char* (theme name)
    GSList* list_directories; // each item is an IconThemeDir*
};

class Launcher : public Area {
  public:
    std::vector<char*> list_apps;          // paths to .desktop files
    std::vector<LauncherIcon*> list_icons;
    std::vector<IconTheme*> list_themes;
};

extern int launcher_enabled;
extern int launcher_max_icon_size;
extern int launcher_tooltip_enabled;
extern int launcher_alpha;
extern int launcher_saturation;
extern int launcher_brightness;
extern char* icon_theme_name;   // theme name
extern XSettingsClient* xsettings_client;

// default global data
void default_launcher();

// initialize launcher : y position, precision, ...
void init_launcher();
void init_launcher_panel(void* panel);
void cleanup_launcher();
void cleanup_launcher_theme(Launcher* launcher);

int  resize_launcher(void* obj);
void draw_launcher(void* obj, cairo_t* c);

// Populates the list_themes list
void launcher_load_themes(Launcher* launcher);
// Populates the list_icons list
void launcher_load_icons(Launcher* launcher);
void launcher_action(LauncherIcon* icon, XEvent* e);

void test_launcher_read_desktop_file();
void test_launcher_read_theme_file();

#endif

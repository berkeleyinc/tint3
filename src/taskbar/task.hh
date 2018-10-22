#ifndef TINT3_TASKBAR_TASK_HH
#define TINT3_TASKBAR_TASK_HH

#include <Imlib2.h>
#include <X11/Xlib.h>

#include <list>

#include "util/area.hh"
#include "util/common.hh"
#include "util/pango.hh"
#include "util/timer.hh"
#include "util/x11.hh"

enum TaskState {
  kTaskNormal,
  kTaskActive,
  kTaskIconified,
  kTaskUrgent,
  kTaskStateCount
};

// --------------------------------------------------
// global task parameter
class Global_task : public Area {
 public:
  Global_task();

  bool text;
  bool icon;
  bool centered;

  int icon_posy;
  int icon_size1;
  int maximum_width;
  int maximum_height;
  int alpha[kTaskStateCount];
  int saturation[kTaskStateCount];
  int brightness[kTaskStateCount];
  int config_asb_mask;
  Background background[kTaskStateCount];
  int config_background_mask;
  // starting position for text ~ task_padding + task_border + icon_size
  double text_posx, text_height;

  bool font_shadow;
  util::pango::FontDescriptionPtr font_desc;
  Color font[kTaskStateCount];
  int config_font_mask;
  bool tooltip_enabled;
};

// TODO: make this inherit from a common base class that exposes state_pixmap
class Task : public Area {
 public:
  explicit Task(Timer& timer);

  // TODO: group task with list of windows here
  Window win;
  unsigned int desktop;
  int current_state;
  util::imlib2::Image icon[kTaskStateCount];
  util::imlib2::Image icon_hover[kTaskStateCount];
  util::imlib2::Image icon_pressed[kTaskStateCount];
  util::x11::Pixmap state_pix[kTaskStateCount];
  unsigned int icon_width;
  unsigned int icon_height;
  int urgent_tick;

  void DrawForeground(cairo_t* c) override;
  std::string GetTooltipText() override;
  bool UpdateTitle();  // TODO: find a more descriptive name
  std::string GetTitle() const;
  void SetTitle(std::string const& title);
  void SetState(int state);
  void OnChangeLayout() override;
  Task& SetTooltipEnabled(bool);

  void AddUrgent();
  void DelUrgent();

#ifdef _TINT3_DEBUG

  std::string GetFriendlyName() const override;

#endif  // _TINT3_DEBUG

 private:
  bool tooltip_enabled_;
  std::string title_;
  Timer& timer_;

  void DrawIcon(int);
};

extern Interval::Id urgent_timeout;
extern std::list<Task*> urgent_list;

Task* AddTask(Window win, Timer& timer);
void RemoveTask(Task* tsk);

void GetIcon(Task* tsk);
void ActiveTask();
void SetTaskRedraw(Task* tsk);

Task* FindActiveTask(Task* current_task, Task* active_task);
Task* NextTask(Task* tsk);
Task* PreviousTask(Task* tsk);

#endif  // TINT3_TASKBAR_TASK_HH

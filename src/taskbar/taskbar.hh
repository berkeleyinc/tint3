#ifndef TINT3_TASKBAR_TASKBAR_HH
#define TINT3_TASKBAR_TASKBAR_HH

#include <unordered_map>
#include <vector>

#include "task.hh"
#include "taskbarbase.hh"
#include "taskbarname.hh"

using TaskPtrArray = std::vector<Task*>;
using WindowToTaskMap = std::unordered_map<Window, TaskPtrArray>;
extern WindowToTaskMap win_to_task_map;

extern Task* task_active;
extern Task* task_drag;
extern bool taskbar_enabled;

// tint3 uses one taskbar per desktop.
class Taskbar : public TaskbarBase {
 public:
  unsigned int desktop;
  int text_width_;
  Taskbarname bar_name;

  Taskbar& SetState(size_t state);
  void DrawForeground(cairo_t*) override;
  void OnChangeLayout() override;
  bool Resize() override;

  util::iterator_range<std::vector<Area*>::iterator> filtered_children();
  bool RemoveChild(Area* child) override;

  static void InitPanel(Panel* panel);

#ifdef _TINT3_DEBUG

  std::string GetFriendlyName() const override;

#endif  // _TINT3_DEBUG
};

class Global_taskbar : public Taskbar {
 public:
  Global_taskbar();

  std::vector<Background> background;
  std::vector<Background> background_name;
};

// default global data
void DefaultTaskbar();

// free memory
void CleanupTaskbar();

void InitTaskbar();

void TaskbarRemoveTask(Window win);
Task* TaskGetTask(Window win);
TaskPtrArray TaskGetTasks(Window win);
void TaskRefreshTasklist(Timer& timer);

#endif  // TINT3_TASKBAR_TASKBAR_HH

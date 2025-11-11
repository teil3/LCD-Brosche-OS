#ifndef CORE_APPMANAGER_H
#define CORE_APPMANAGER_H
#include <vector>
#include "App.h"

class AppManager {
public:
  void add(App* a);
  void begin();
  void next();
  void prev();
  void setActive(int i);
  bool activate(App* app);
  App* activeApp() const;
  void dispatchBtn(uint8_t idx, BtnEvent e);
  void tick(uint32_t dt);
  void draw();
private:
  std::vector<App*> apps_;
  int active_ = -1;
};

#endif // CORE_APPMANAGER_H

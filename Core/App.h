#ifndef CORE_APP_H
#define CORE_APP_H

#include <stdint.h>

enum class BtnEvent : uint8_t { None, Single, Double, Triple, Long };

class App {
public:
  virtual ~App() {}
  virtual const char* name() const = 0;
  virtual void init() = 0;
  virtual void tick(uint32_t delta_ms) = 0;
  virtual void onButton(uint8_t index, BtnEvent e) = 0; // 1=BTN1, 2=BTN2
  virtual void draw() = 0;
  virtual void shutdown() = 0;
  virtual void resume() {}  // Called when app resumes after overlay (e.g. setup menu)
};

#endif // CORE_APP_H


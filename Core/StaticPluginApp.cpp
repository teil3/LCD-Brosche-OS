#include "StaticPluginApp.h"
#include "AppAPIImpl.h"

// Reference to shared core API
AppAPI& StaticPluginApp::coreAPI_ = AppAPIImpl::coreAPI;

StaticPluginApp::StaticPluginApp(const PluginAppVTable* vtable)
  : vtable_(vtable) {
  if (vtable_ && vtable_->init) {
    vtable_->init(&coreAPI_);
  }
  if (vtable_ && vtable_->getName) {
    appName_ = String(vtable_->getName());
  } else {
    appName_ = "Static Plugin";
  }
  Serial.printf("[StaticPluginApp] Initialized: %s\n", appName_.c_str());
}

const char* StaticPluginApp::name() const {
  return appName_.c_str();
}

void StaticPluginApp::init() {
  if (vtable_ && vtable_->onActivate) {
    vtable_->onActivate();
  }
}

void StaticPluginApp::tick(uint32_t delta_ms) {
  if (vtable_ && vtable_->tick) {
    vtable_->tick(delta_ms);
  }
}

void StaticPluginApp::onButton(uint8_t index, BtnEvent e) {
  if (vtable_ && vtable_->onButton) {
    vtable_->onButton(index, static_cast<uint8_t>(e));
  }
}

void StaticPluginApp::draw() {
  if (vtable_ && vtable_->draw) {
    vtable_->draw();
  }
}

void StaticPluginApp::shutdown() {
  if (vtable_ && vtable_->shutdown) {
    vtable_->shutdown();
  }
}

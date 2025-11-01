#pragma once
#ifndef APP_API_IMPL_H_
#define APP_API_IMPL_H_

#include "AppAPI.h"

/**
 * Shared AppAPI implementation namespace
 * Used by both StaticPluginApp and PluginApp to avoid duplicate symbols
 */
namespace AppAPIImpl {
  extern AppAPI coreAPI;
}

#endif // APP_API_IMPL_H_

#pragma once

/* Stand-in for the header the plugin build generates, for tests that link
 * plugin sources against the real libobs. The functions are defined by the
 * test itself. */

#include <obs-module.h>

#define PLUGIN_NAME "void-radio"
#define PLUGIN_VERSION "test"

#ifdef __cplusplus
extern "C" {
#endif

void obs_log(int log_level, const char *format, ...);

#ifdef __cplusplus
}
#endif

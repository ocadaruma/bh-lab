// The entry points live in the static impl library; each plugin format re-compiles a
// tiny translation unit (bhl_entry.cpp) that exports a clap_entry pointing here.
#pragma once

#include <clap/clap.h>

extern "C"
{
  bool bhl_entry_init(const char *plugin_path);
  void bhl_entry_deinit();
  const void *bhl_entry_get_factory(const char *factory_id);
}

// Exported CLAP entry point. Compiled once per plugin format by make_clapfirst_plugins.
#include <clap/clap.h>

#include "bhl_entry.h"

extern "C"
{
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

  const CLAP_EXPORT struct clap_plugin_entry clap_entry = {CLAP_VERSION, bhl_entry_init,
                                                           bhl_entry_deinit,
                                                           bhl_entry_get_factory};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}

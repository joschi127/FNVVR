#include <algorithm>
#include <cctype>

#include "vr_pipboy_icons.h"

namespace dxvk {

  namespace {

    struct Replacement {
      const char* from;
      const char* to;
      bool        loose;
    };

    constexpr Replacement Replacements[] = {
      { "general_button_lt.dds",      "fnvvr_button_lgrip.dds",      true  },
      { "general_button_rt.dds",      "fnvvr_button_rgrip.dds",      true  },
      { "glow_general_button_lt.dds", "glow_fnvvr_button_lgrip.dds", true  },
      { "glow_general_button_rt.dds", "glow_fnvvr_button_rgrip.dds", true  },
      { "general_button_lb.dds",      "general_button_lt.dds",       false },
      { "general_button_rb.dds",      "general_button_rt.dds",       false },
      { "glow_general_button_lb.dds", "glow_general_button_lt.dds",  false },
      { "glow_general_button_rb.dds", "glow_general_button_rt.dds",  false },
    };

  }


  bool vrPipBoyIconReplacement(
    const std::string&          path,
          std::string&          replaced,
          bool&                 loose) {
    size_t separator = path.find_last_of("\\/");
    size_t start     = separator == std::string::npos ? 0 : separator + 1;

    std::string name = path.substr(start);
    std::transform(name.begin(), name.end(), name.begin(),
      [] (char c) { return char(std::tolower(static_cast<unsigned char>(c))); });

    for (const Replacement& entry : Replacements) {
      if (name == entry.from) {
        replaced = path.substr(0, start) + entry.to;
        loose    = entry.loose;
        return true;
      }
    }

    return false;
  }

}

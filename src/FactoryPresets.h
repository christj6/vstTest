#pragma once

#include <utility>
#include <vector>

namespace boringsynth
{

// A preset built into the plug-in: its name, and the parameters it sets (by ID, in each parameter's
// own units). Every parameter it doesn't list is at its default.
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> settings;
};

// The built-in presets, in the order the preset menu (and the host's program list) shows them. The
// first is Init: every parameter at its default, the plug-in's first sound. Hosts save a project's
// program by its position, so new presets go at the end.
const std::vector<FactoryPreset>& getFactoryPresets();

} // namespace boringsynth

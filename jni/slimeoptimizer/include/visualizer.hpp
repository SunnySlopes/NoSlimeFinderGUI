#pragma once

#include "types.hpp"
#include <string>

// Renders a PNG for one result: a chunk grid (slime chunks in green), the best
// AFK point in red, two black spawn-radius circles, and a text panel.
// Uses a self-contained PNG encoder (no libpng/zlib dependency).
bool renderMap(const Params& params, const Result& result, const std::string& path);

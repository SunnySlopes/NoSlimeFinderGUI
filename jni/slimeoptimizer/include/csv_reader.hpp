#pragma once

#include "types.hpp"
#include <string>
#include <vector>

// Reads a Slimy CSV of the form:  chunkX,chunkZ,count
// - a leading header row (non-numeric first field) is skipped
// - duplicate (chunkX, chunkZ) rows are merged, keeping the largest count
std::vector<Candidate> readCandidates(const std::string& path);

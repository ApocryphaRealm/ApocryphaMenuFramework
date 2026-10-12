#pragma once

// Precompiled for every file of the bridge, CommonLibSSE-NG's generated plugin declaration included: its headers expect the
// standard library to be in already (as AMF's own PCH.h has it).
#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

using namespace std::literals;   // the generated plugin declaration writes its name and author as "..."sv
using namespace REL::literals;

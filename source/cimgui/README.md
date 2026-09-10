# cimgui, vendored

`cimgui.cpp` and `cimgui.h` here are **unmodified**, taken from
<https://github.com/cimgui/cimgui> at tag **`1.90.8dock`**, MIT licensed.

## Why they are in this repository

A mod written for SKSE Menu Framework does not link against a framework - it resolves every drawing
function it uses by name at runtime. That name set *is* cimgui: SMF re-exports the generated cimgui
wrappers, and a consumer calls them through `GetProcAddress`. A name the framework does not export
comes back null and that part of the consumer's page silently draws nothing, which is what a user
reported on 2026-09-10 as "settings show up as blank".

This framework used to hand-write those wrappers, and covered 252 of SMF's 1,420 names across four
tranches, each chosen by scanning whichever mods happened to be installed. Vendoring the generated
source ends that: every name exists, with the signature it is supposed to have, correct by
construction rather than by eye.

## Why this exact tag

`1.90.8dock` matches the Dear ImGui this framework embeds on both build lines - 1.90.8 with
vcpkg's `docking-experimental` feature - and it is the one that lines up with SMF:

| cimgui tag | declares | in SMF's export table |
| --- | --- | --- |
| **1.90.8dock** | **1,397** | **1,397 - all of them** |
| 1.90.8 (no docking) | 1,304 | 1,304 |
| docking_inter | 1,615 | 1,320 |

The 23 SMF exports cimgui does not provide are SMF's own API - `AddSectionItem`, `LoadTexture`,
`SKSEPlugin_*` and so on - and live in `source/Compat.cpp`.

`CIMGUI_API` already expands to `extern "C" __declspec(dllexport)`, so compiling this file is the
whole of the integration; `CMakeLists.txt` globs `source/*.cpp`.

## If Dear ImGui is ever upgraded

Re-fetch cimgui at the matching tag. A mismatch between this file and the embedded imgui headers is
a compile error, not a silent break, which is the point of vendoring the generated source rather
than transcribing it.

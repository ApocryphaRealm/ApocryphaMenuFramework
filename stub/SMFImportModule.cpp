// SKSEMenuFramework.dll - THE SMF IMPORT MODULE (2.4.0).
//
// The problem it answers, observed in game on 2026-11-01: Scavenger Sense 0.10.1beta (Nexus 188380) links SKSE Menu
// Framework at BUILD time - its ScavengerSense.dll has an import-table entry for SKSEMenuFramework.dll naming 75
// functions (igText, AddSectionItem, ImDrawList_AddLine ...). The Windows loader resolves that entry itself while SKSE
// loads the plugin, before any of the plugin's code runs, so AMF's import-table alias (SmfAlias.cpp), which answers
// GetModuleHandle / GetProcAddress / file questions, never gets asked. With no module of that name the load fails:
// skse64.log "couldn't load plugin ... ScavengerSense.dll (Error 126)" - the module was not found.
//
// The fix is a module the loader can find by that name: this file. It holds no code at all - every export is a
// FORWARDER to the same-named export of !ApocryphaMenuFramework.dll (generated from AMF's own export table by
// cmake/gen-smf-import-forwards.cmake), so a static importer is bound straight to AMF's functions, exactly the ones a
// dynamic consumer gets from GetProcAddress. Nothing here comes from SKSE Menu Framework's source or binary; matching
// the export NAMES is what compatibility needs.
//
// How it reaches the loader (source/SmfImportModule.cpp in the framework): it is NOT in SKSE\Plugins - SKSE never sees
// it and never loads it as a plugin - but in AMF's own folder, SKSE\Plugins\ApocryphaMenuFramework\SMF-Import\.
// !ApocryphaMenuFramework.dll sorts before every other plugin, and while it loads (DllMain) it loads this file by its
// full path - only when the real SKSE Menu Framework is NOT installed. From then on the module is in the process under
// the name SKSEMenuFramework.dll, and the loader binds every later static importer to it by name, whatever the DLL
// search path. With the real SKSE Menu Framework installed nothing of this runs, so the two never meet.
//
// Linked with /NOENTRY and no C runtime: there is no DllMain and no import, so loading it runs nothing.

#include "smf_import_forwards.inc"

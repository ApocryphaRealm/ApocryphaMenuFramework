#pragma once

// ============================================================================================
// Main-menu DevBench driver (rule 64 applied to the GAME's start menu - the author, 2026-08-28:
// "alter the start menu with the DevBench hooks so that you could press it with DevBench").
//
// Registers `amf.mainmenu` so the vanilla Main Menu can be driven and inspected headlessly:
//   POST /api/tool/amf.mainmenu
//     {"op":"state"}                         -> is the Main Menu open, movie present, paths tried
//     {"op":"userevent","text":"New Game"}   -> post a kUserEvent (BSUIMessageData) to Main Menu -
//                                               the message the menu's own buttons produce
//     {"op":"invoke","path":"_root.X.onPress"} -> call an ActionScript method on the menu movie
//     {"op":"getvar","path":"_root.MenuHolder._name"} -> read an ActionScript variable
//
// userevent/invoke are queued to the MAIN thread (fire-and-forget, observe via lifecycle/log);
// state/getvar block the listener thread on the main-thread task briefly (2s timeout) so the
// answer returns synchronously. This is the exploration surface that lets Claude find and press
// the real New Game path live - the one start-menu action nothing headless could reach before.
// ============================================================================================

namespace DevBenchAPI
{
	struct IDevBenchInterface001;
}

namespace mainmenudriver
{
	// Registers `amf.mainmenu` on the given (non-null) DevBench interface.
	void Register(DevBenchAPI::IDevBenchInterface001* a_devBench);
}

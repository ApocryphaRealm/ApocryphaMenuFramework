#pragma once

// ============================================================================================
// AMF-hosted native Papyrus functions (decisions doc S3/S9 tier 2 item 9). Path A from the
// research the author brought in: bind native C++ into the game's OWN Papyrus VM via
// RE::BSScript::IVirtualMachine::RegisterFunction, rather than embedding a second language.
// Functionally testable without visual judgement: a test script calls a native function, the
// function's own log line confirms it fired, no in-game UI involved.
// ============================================================================================

namespace scripting
{
	// Registers AMF's native functions against the Papyrus VM. Called from the
	// kPostLoad/DLL-message-listener path once the VM is guaranteed to exist.
	void RegisterNativeFunctions();
}

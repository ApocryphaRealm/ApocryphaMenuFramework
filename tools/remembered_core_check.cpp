// Off-game check of include\RememberedCore.h: which labels name an action (never played back on a load) and which do not.
// Build and run: tools\remembered_core_check.bat
#include "../include/RememberedCore.h"

#include <cstdio>

int main()
{
	struct Case
	{
		const char* label;
		bool action;
	};
	static constexpr Case kCases[] = {
		// rows that run something
		{ "$ResetAll", true }, { "Reset all settings", true }, { "$MCM_Uninstall", true }, { "Uninstall mod", true }, { "$sUninstallMod", true },
		{ "Restore defaults", true }, { "Clear all", true }, { "Delete saved", true }, { "Load preset", true }, { "$SaveProfile", true },
		{ "Export settings", true }, { "Stop mod", true }, { "Shut down", true }, { "Force update", true }, { "Rebuild now", true }, { "$WipeData", true },
		// rows that hold a value, Improved Alternate Conversation Camera's among them
		{ "$ForceFirstPerson", false }, { "$ForceThirdPerson", false }, { "$LockOn", false }, { "$CameraSpeed", false }, { "$SmoothTransition", false },
		{ "$SwitchTarget", false }, { "$HeadTracking", false }, { "$HideDialogueMenu", false }, { "$LetterBox", false }, { "Enabled", false },
		{ "Load Legacy Armor", false }, { "Clear Weather Only", false }, { "Default to first person", false }, { "Remove Bloom", false }, { "Restore Health on Wake", false },
		{ "Preset Colors", false }, { "", false },
	};
	int bad = 0;
	int ran = 0;
	for (const Case& c : kCases)
	{
		++ran;
		if (rememberedcore::LooksLikeAction(c.label, "") != c.action)
		{
			std::printf("WRONG: \"%s\" should %sname an action\n", c.label, c.action ? "" : "not ");
			++bad;
		}
	}
	// the shown text counts as well as the raw key (a key like $IDS_12 says nothing)
	++ran;
	if (!rememberedcore::LooksLikeAction("$IDS_12", "Reset all settings"))
	{
		std::printf("WRONG: the shown text was not looked at\n");
		++bad;
	}
	if (bad) { std::printf("remembered_core_check: %d of %d wrong\n", bad, ran); }
	else { std::printf("remembered_core_check: all %d right\n", ran); }
	return bad ? 1 : 0;
}

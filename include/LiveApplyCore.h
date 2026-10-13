#pragma once

// ============================================================================================
// Live apply on converted pages (2.2.1; the owner, 2026-10-13: "If possible then build the live apply for converted
// pages", from ofrose's Nexus comment of 2026-10-08 - TrueHUD's bar position and size changed through AMF applied only
// once AMF closed or another mod was chosen).
//
// Some mods read their settings only when their menu closes: TrueHUD, True Directional Movement, Precision, Floating
// Subtitles and others have `Event OnConfigClose() native` (their DLL re-reads every setting there); others reload or
// save in a Papyrus OnConfigClose. AMF already sends OnSettingChange(id) after every change, as MCM Helper's menu does,
// and OnConfigClose when the page is left. With live apply on, AMF also plays a short close-and-reopen
// (OnConfigClose, then OnConfigOpen) once a change has settled, so such a mod applies it while its page stays open.
// Prisma MCM Redux pages get their apply event (Prisma_OnSettingsApplied) the same way.
//
// This header is the decision only - when a pending change may be applied - with no game and no ImGui, so
// tools\live_apply_check.bat tests it off-game. A change is applied ONCE, after:
//   - no further change for `quiet` seconds (a run of D-pad nudges is one apply),
//   - no control held (a slider being dragged applies once it is let go, never every frame of the drag),
//   - the writes for it have finished (the mod must read the new value) - or `stuck` seconds have passed without them,
//     so a write that never reports back cannot hold the apply for good.
// A pending change is dropped, not applied, when the setting is off or the page was left (leaving sends the mod's own
// close event anyway).
// ============================================================================================

namespace liveapply
{
	enum class Decision
	{
		kIdle,       // nothing pending
		kWait,       // pending, not yet
		kFire,       // apply now (the pending change is consumed)
		kDropOff,    // pending dropped: live apply is off
		kDropLeft    // pending dropped: the page was left, and leaving it applies
	};

	inline const char* Name(Decision a_d)
	{
		switch (a_d)
		{
		case Decision::kIdle: return "idle";
		case Decision::kWait: return "wait";
		case Decision::kFire: return "fire";
		case Decision::kDropOff: return "dropped (live apply off)";
		case Decision::kDropLeft: return "dropped (page left - its close applies)";
		}
		return "?";
	}

	struct Timing
	{
		double quiet = 0.35;   // seconds with no further change
		double stuck = 5.0;    // seconds after which unfinished writes no longer hold the apply
	};

	// One pending apply for one page at a time (a change on another page replaces it: that page was left, and leaving
	// applied the first). Not thread-safe; the caller holds its own lock.
	class Gate
	{
	public:
		// A change on page a_key at time a_now (seconds, any steady clock).
		void Change(int a_key, double a_now)
		{
			_pending = true;
			_key = a_key;
			_last = a_now;
		}

		// a_enabled: the setting. a_open: the page drawn now (-1: none). a_held: a control is held. a_inFlight: writes not
		// yet finished.
		Decision Tick(double a_now, bool a_enabled, int a_open, bool a_held, int a_inFlight, const Timing& a_t = {})
		{
			if (!_pending) { return Decision::kIdle; }
			if (!a_enabled)
			{
				_pending = false;
				return Decision::kDropOff;
			}
			if (a_open != _key)
			{
				_pending = false;
				return Decision::kDropLeft;
			}
			const double quietFor = a_now - _last;
			if (a_held || quietFor < a_t.quiet) { return Decision::kWait; }
			if (a_inFlight > 0 && quietFor < a_t.stuck) { return Decision::kWait; }
			_pending = false;
			return Decision::kFire;
		}

		bool Pending() const { return _pending; }
		int Key() const { return _key; }
		double QuietFor(double a_now) const { return _pending ? a_now - _last : 0.0; }
		void Reset() { _pending = false; }

	private:
		bool _pending = false;
		int _key = -1;
		double _last = 0.0;
	};
}

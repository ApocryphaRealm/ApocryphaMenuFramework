# ApocryphaMenuFramework - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

## 1.0.0 - 2026-08-27 - untested

### Added
- M1 render loop: ImGui embedded via the vcpkg port (dx11+win32 binding features), present hook on the 18-repo-corroborated site, and the DISPUTED D3D-init offset resolved by probing both candidates behind byte-pattern guards at runtime - the winner is logged per runtime. Full theme applied (true black, #F5F2E9, borders on every element, readable TextDisabled); game HUD opacity re-read per frame as one global multiplier; K toggles a display-only window. Every guard fails toward loaded-but-inert with the reason logged, never toward a crash. Corrections this milestone: vcpkg installs imgui backend headers FLAT (not backends/); CommonLibSSE-NG renamed BSRenderManager to BSGraphics::Renderer leaving a zero-byte tombstone header.
- M0 scaffold: original framework (not an SMF fork - licence rails in plan.md), CommonLibSSE-NG dual-runtime plugin skeleton, public C API with SMF_GetReservedKeyCodes as the first export, theme constants (true black / #F5F2E9 / borders everywhere) and the in-game-proven fHUDOpacity resolver ported from DEM. Rendering, input and the page registry are M1-M3.

### Changed
- Default window position is now centre-relative (display centre, centre pivot) - the same resolution-independence principle as LMU's map border, per the author after seeing the 16:35 capture where the unscaled window sat as a sliver in the top-left at 3200x1800. FirstUseEver, so player-moved windows keep their arrangement.
- Menu toggle moved from K to F1 (0x3B), decided during the first in-game smoke test: K collided with Dragon's Eye Minimap's rule-28 default the moment both ran, and F1 matches the established framework convention (SMF). Reserved-keys export updated to report F1. Rule-28 K/L defaults now read as mod-scoped; the framework is the arbiter and takes F1.
### Fixed
- M1.1, from the first smoke test: resolution-aware UI scaling. Font, style metrics and the default window size now scale by displayHeight/1080 (1.67x at 1800p), fixing the far-too-small window the author reported. Camera-still-moves is NOT fixed here - input capture is M2 and now its user-validated top priority.
#pragma once

// Replacement ARTWORK for the framework's menu shell, so a UI author can make Apocrypha Menu
// Framework match their own interface instead of accepting the built-in look.
//
// Requested 2026-09-09 for borokoshow, to match Dragonborn UI. It is the standing project meaning
// of "theme": a theme is REPLACEMENT ART, not a colour tint over the same shapes.
//
// WHAT AN AUTHOR SHIPS
//   Frame       one square PNG, 32-bit RGBA, TRANSPARENT CENTRE, drawn as a nine-slice.
//               192x192 with 64px corners is the suggested default; the corner is an INI key
//               because only the artist knows where their ornament stops.
//   Background  either a small tileable PNG or a full-screen one. Which it is is decided by its
//               own size rather than a fifth INI key: <= kTileThreshold px on both sides tiles,
//               anything larger is stretched to the window. A 256x256 tile and a 1920x1080
//               backdrop therefore both just work.
//   Plates      optional small RGBA PNGs restyling individual controls, found by fixed name
//               inside one folder: toggle.png, slider.png, tab.png.
//
// ALL PNG, NO DDS. AMF decodes through DirectXTK's WIC loader, which handles PNG (and JPG/BMP/
// TIFF) and does not handle DDS at all. Handing it a .dds silently fails to decode, so the
// loader below says so in the log rather than leaving an author guessing.
//
// Every piece is optional and independent: supply only a frame and the rest of the look is
// unchanged. Nothing here can make the menu unusable - a texture that fails to load is skipped
// and the built-in art draws instead.

#include <cstdint>
#include <string>
#include <vector>

struct ImVec2;

namespace skin
{
	// A background at or below this on BOTH axes is treated as a tile; larger is stretched.
	inline constexpr std::uint32_t kTileThreshold = 512;

	enum class Plate : std::uint32_t
	{
		kToggle = 0,  // toggle.png - the on/off switch track
		kSlider,      // slider.png - the slider grab
		kTab,         // tab.png    - the section tab bar
		kCount
	};

	// THE ART LIBRARY (2.1.6, the owner, 2026-10-08: "add an assets folder with sub folders for the customization section
	// to choose from ... break down the themes into their art parts"). Every theme's art is one PART per kind, a PNG named
	// after the theme it came from, in Data/SKSE/Plugins/ApocryphaMenuFramework/assets/<folder>/<name>.png. A frame has a
	// <name>.ini beside it saying how it is cut (uCorner, uDrawCorner, bTileEdges, sHighlight). A theme names its parts
	// (sFrameArt / sBackgroundArt / sToggleArt); the player can pick any part for any theme on Appearance > Art.
	enum class ArtKind : std::uint32_t
	{
		kFrame = 0,    // assets/frames      - the window frame, nine-sliced
		kBackground,   // assets/backgrounds - the window background, tiled or stretched
		kToggle,       // assets/toggles     - the on/off switch track plate
		kCount
	};
	inline constexpr const char* kArtKeys[] = { "sFrame", "sBackground", "sToggle" };          // [Art.<theme id>] keys
	inline constexpr const char* kArtFolders[] = { "frames", "backgrounds", "toggles" };      // under assets/
	inline constexpr const char* kArtNone = "none";   // a player's pick meaning "no art of this kind"

	// The part names in assets/<folder>/ (file names without .png), sorted. Read from disk on each call - the Art page
	// calls it when it opens, not every frame.
	std::vector<std::string> ListArt(ArtKind a_kind);
	// The active theme's own part of that kind ("" = it has none), and the part drawing now ("" = none drawing, or the
	// art came from a path rather than the library).
	std::string ThemeArt(ArtKind a_kind);
	std::string ActiveArt(ArtKind a_kind);
	// A part's texture for the Art page's previews, loaded once and kept (a few small PNGs). Null if it does not load.
	void* ArtThumb(ArtKind a_kind, const std::string& a_name, ImVec2* a_size);

	// Loads (or reloads) every texture named by the current settings. Safe to call at any time
	// from the render thread; safe to call before the D3D device exists, in which case nothing
	// loads and the next call does the work. Called at D3D init and by the DevBench reload op,
	// so an artist can iterate without restarting the game.
	void Reload();

	// Frame. Size is the texture's own pixel size; corner is the INI value, clamped so that two
	// corners always fit inside the texture (an over-large corner would flip the middle slices).
	bool   HasFrame();
	void*  FrameTexture();
	ImVec2 FrameSize();
	float  FrameCorner();
	// From the frame's .ini (2.1.6): the corner's size on screen at 1080p when it differs from its size in the texture
	// (0 = the same; the map edge is drawn from art at twice its size), whether the edges repeat at their own size
	// instead of stretching (a pattern along the edge), and whether a highlighted item gets only the line and the four
	// corners (art with solid edge bands, which would cover the text) rather than the whole frame.
	float  FrameDrawCorner();
	bool   FrameTiles();
	bool   FrameHighlightCorners();
	// Whether ANY frame draws round the window: loaded art, or the Skyrim theme's built-in knotwork when its art did not
	// load (no assets folder) - unless the player picked "none" for the frame.
	bool   DrawsFrame();

	// Background. Tiles() reports how it will be drawn, decided by kTileThreshold above.
	bool   HasBackground();
	void*  BackgroundTexture();
	ImVec2 BackgroundSize();
	bool   BackgroundTiles();

	// Optional per-control plates.
	bool   HasPlate(Plate a_plate);
	void*  PlateTexture(Plate a_plate);
	ImVec2 PlateSize(Plate a_plate);

	// What actually loaded, for the DevBench tool and the log - so "my art is not showing" is
	// answered by asking the framework rather than by guessing at paths.
	std::string StatusJson();
}

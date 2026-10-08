#include "PCH.h"

#include "Skin.h"

#include "ConsumerSurface.h"
#include "Settings.h"
#include "Theme.h"

#include "utils/Logger.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace skin
{
	namespace
	{
		struct Entry
		{
			std::string path;      // as configured, after resolution; empty = not configured
			void*       srv = nullptr;
			ImVec2      size{ 0.0f, 0.0f };
			std::string error;     // why it is not loaded, for StatusJson and the log
		};

		Entry g_frame;
		Entry g_background;
		std::array<Entry, static_cast<std::size_t>(Plate::kCount)> g_plates;
		float g_frameCorner = 64.0f;
		// 2.1.6: the frame's cut from its .ini, and what the library resolved to (for the Art page and StatusJson)
		float g_frameDrawCorner = 0.0f;
		bool  g_frameTiles = false;
		bool  g_frameHighlightCorners = false;
		bool  g_frameNone = false;   // the player picked "none" for the frame - not even the built-in knotwork
		std::array<std::string, kArtKindCount> g_activeArt;
		std::array<ArtPart, kArtKindCount> g_parts;   // the control kinds (Box .. Cursor); Frame / Background / Switch above
		std::array<bool, kArtKindCount> g_partLoaded{};

		constexpr const char* kAssetDir = "SKSE/Plugins/ApocryphaMenuFramework/assets";

		constexpr const char* kPlateFile[] = { "toggle.png", "slider.png", "tab.png" };
		constexpr const char* kPlateName[] = { "toggle", "slider", "tab" };

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		// An author writes a path the way they think about their own mod - "Interface/DragonbornUI/
		// frame.png". Accept that (resolved under the game's Data), and accept an absolute path too,
		// so a work-in-progress file outside the mod folder can be pointed at while iterating.
		std::string Resolve(const std::string& a_configured)
		{
			if (a_configured.empty()) { return {}; }
			std::filesystem::path p(a_configured);
			if (p.is_absolute()) { return p.string(); }
			return (std::filesystem::path("Data") / p).string();
		}

		// Takes an ALREADY-RESOLVED path. It used to call Resolve() itself, which double-prefixed
		// the plate files ("Data\Data\Interface\...") because the plate loop resolves the folder
		// before appending the file name. Resolving happens at the call sites now, exactly once.
		void Load(Entry& a_entry, const std::string& a_path, const char* a_what)
		{
			a_entry = Entry{};
			if (a_path.empty()) { return; }

			a_entry.path = a_path;

			// DDS is the one an author is most likely to reach for, being Skyrim's own texture
			// format, and it is exactly the one that cannot work here. Say so by name.
			if (Lower(std::filesystem::path(a_entry.path).extension().string()) == ".dds")
			{
				a_entry.error = "DDS is not decoded - save it as a 32-bit RGBA PNG";
				logger::warn("skin: {} \"{}\" is a .dds. AMF decodes PNG (WIC), not DDS - export it as a "
							 "32-bit RGBA PNG instead.", a_what, a_entry.path);
				return;
			}

			std::error_code ec;
			if (!std::filesystem::exists(a_entry.path, ec))
			{
				a_entry.error = "file not found";
				logger::warn("skin: {} \"{}\" does not exist", a_what, a_entry.path);
				return;
			}

			ImVec2 size{ 0.0f, 0.0f };
			void* srv = consumer::LoadTexture(a_entry.path.c_str(), &size);
			if (!srv)
			{
				a_entry.error = "could not decode";
				logger::warn("skin: {} \"{}\" could not be decoded - is it a real 32-bit RGBA PNG?",
							 a_what, a_entry.path);
				return;
			}

			a_entry.srv = srv;
			a_entry.size = size;
			logger::info("skin: {} loaded from \"{}\" ({:.0f}x{:.0f})", a_what, a_entry.path, size.x, size.y);
		}

		// assets/<folder>/<name>.png, as a path under Data (Resolve's form).
		std::string ArtPath(ArtKind a_kind, const std::string& a_name)
		{
			return std::format("{}/{}/{}.png", kAssetDir, kArtFolders[static_cast<std::size_t>(a_kind)], a_name);
		}

		// A part's cut, from the <name>.ini beside it. A missing file or key keeps the old behaviour: the corner the
		// caller had, drawn at its own size, edges stretched, the whole frame round a highlighted item.
		struct FrameCut
		{
			std::uint32_t corner = 0;
			std::uint32_t drawCorner = 0;
			bool tile = false;
			bool highlightCorners = false;
			float hotX = 0.0f, hotY = 0.0f;
		};

		FrameCut ReadCut(const char* a_folder, const std::string& a_name)
		{
			FrameCut cut;
			std::ifstream in(std::filesystem::path("Data") / kAssetDir / a_folder / (a_name + ".ini"));
			std::string line;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') { line.pop_back(); }
				const auto eq = line.find('=');
				if (line.empty() || line[0] == ';' || line[0] == '[' || eq == std::string::npos) { continue; }
				const std::string key = Lower(line.substr(0, eq));
				const std::string value = line.substr(eq + 1);
				try
				{
					if (key == "ucorner") { cut.corner = static_cast<std::uint32_t>(std::stoul(value)); }
					else if (key == "udrawcorner") { cut.drawCorner = static_cast<std::uint32_t>(std::stoul(value)); }
					else if (key == "btileedges") { cut.tile = value == "1" || Lower(value) == "true"; }
					else if (key == "shighlight") { cut.highlightCorners = Lower(value) == "corners"; }
					else if (key == "uhotx") { cut.hotX = std::stof(value); }
					else if (key == "uhoty") { cut.hotY = std::stof(value); }
				}
				catch (...)
				{
					logger::warn("skin: {} \"{}\" .ini has a bad line \"{}\"", a_folder, a_name, line);
				}
			}
			return cut;
		}

		// The theme's own part of a kind.
		const std::string& PaletteArt(const theme::Palette& a_t, ArtKind a_kind)
		{
			return a_t.art[static_cast<std::size_t>(a_kind)];
		}

		// One texture per file, loaded once and kept for the session (render thread only): Reload and the Art page's
		// pictures ask for the same few small PNGs again and again, and a fresh texture each time would never be freed.
		ArtImage CachedTexture(const std::string& a_resolved)
		{
			static std::map<std::string, ArtImage> cache;
			auto it = cache.find(a_resolved);
			if (it == cache.end())
			{
				ArtImage img;
				std::error_code ec;
				if (std::filesystem::exists(a_resolved, ec))
				{
					ImVec2 size{ 0.0f, 0.0f };
					img.srv = consumer::LoadTexture(a_resolved.c_str(), &size);
					if (img.srv) { img.w = size.x; img.h = size.y; }
					else { logger::warn("skin: \"{}\" could not be decoded - is it a real 32-bit RGBA PNG?", a_resolved); }
				}
				it = cache.emplace(a_resolved, img).first;
			}
			return it->second;
		}

		// A control kind's part (Box .. Cursor) and its extra layers. Missing extras are not errors.
		bool LoadPart(ArtKind a_kind, const std::string& a_name)
		{
			const std::size_t k = static_cast<std::size_t>(a_kind);
			const char* folder = kArtFolders[k];
			auto file = [&](const char* a_suffix) {
				return CachedTexture(Resolve(std::format("{}/{}/{}{}.png", kAssetDir, folder, a_name, a_suffix)));
			};
			ArtPart part;
			part.main = file("");
			if (!part.main.srv)
			{
				logger::warn("skin: {} part \"{}\" is not in assets/{}", kArtKeys[k] + 1, a_name, folder);
				return false;
			}
			part.edge = file("-edge");
			if (a_kind == ArtKind::kTickBox) { part.extra = file("-mark"); }
			if (a_kind == ArtKind::kScrollbar) { part.extra = file("-track"); part.extraEdge = file("-track-edge"); }
			const FrameCut cut = ReadCut(folder, a_name);
			part.corner = static_cast<float>(cut.corner);
			if (part.corner <= 0.0f) { part.corner = std::floor(std::min(part.main.w, part.main.h) * 0.25f); }
			part.corner = std::min(part.corner, std::min(part.main.w, part.main.h) * 0.5f - 1.0f);
			part.drawCorner = static_cast<float>(cut.drawCorner);
			part.tile = cut.tile;
			part.hotX = cut.hotX;
			part.hotY = cut.hotY;
			part.cornersOnly = cut.highlightCorners;
			g_parts[k] = part;
			logger::info("skin: {} part \"{}\" loaded ({:.0f}x{:.0f}{}{})", kArtKeys[k] + 1, a_name, part.main.w, part.main.h,
						 part.edge.srv ? ", edge" : "", part.extra.srv ? ", extra" : "");
			return true;
		}
	}

	const ArtPart* Part(ArtKind a_kind)
	{
		const std::size_t k = static_cast<std::size_t>(a_kind);
		return k < kArtKindCount && g_partLoaded[k] ? &g_parts[k] : nullptr;
	}

	std::vector<std::string> ListArt(ArtKind a_kind)
	{
		std::vector<std::string> out;
		const auto dir = std::filesystem::path("Data") / kAssetDir / kArtFolders[static_cast<std::size_t>(a_kind)];
		std::error_code ec;
		for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".png")
			{
				// a part's extra layers are not parts of their own
				const std::string stem = it->path().stem().string();
				const std::string low = Lower(stem);
				auto ends = [&low](const char* a_suf) {
					const std::size_t n = std::char_traits<char>::length(a_suf);
					return low.size() > n && low.compare(low.size() - n, n, a_suf) == 0;
				};
				if (ends("-edge") || ends("-mark") || ends("-track")) { continue; }
				out.push_back(stem);
			}
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	std::string ThemeArt(ArtKind a_kind)
	{
		if (theme::ListThemes().empty()) { return {}; }
		return PaletteArt(theme::GetActiveTheme(), a_kind);
	}

	std::string ActiveArt(ArtKind a_kind)
	{
		return g_activeArt[static_cast<std::size_t>(a_kind)];
	}

	void* ArtThumb(ArtKind a_kind, const std::string& a_name, ImVec2* a_size)
	{
		const ArtImage img = CachedTexture(Resolve(ArtPath(a_kind, a_name)));
		if (a_size) { *a_size = ImVec2(img.w, img.h); }
		return img.srv;
	}

	void Reload()
	{
		const auto& v = settings::Get();

		// WHERE THE ART COMES FROM. The master switch is honoured HERE rather than at each draw site,
		// so exactly one place decides whether custom art exists at all. On, the player's own [Skin]
		// art draws. Off, the ACTIVE THEME's art draws if it has any (1.9.8: a theme picked in the
		// Theme list is itself the request for that theme's look), and otherwise nothing does - which
		// is what a player who installed no art replacer must get, including when paths are still
		// sitting in the INI from somebody's experiment.
		std::string   frame, background, plates;
		std::uint32_t corner = 64;
		const char*   source = "none";
		std::string   toggle, frameName;   // 2.1.6: the switch part's file, and the library frame's name (its .ini)
		bool          toggleNone = false;
		g_frameNone = false;
		g_frameDrawCorner = 0.0f;
		g_frameTiles = false;
		g_frameHighlightCorners = false;
		g_activeArt = {};
		g_partLoaded = {};

		// 2.1.6: THE CONTROL KINDS (Box .. Cursor) - the player's pick for this theme, else the theme's own part, else the
		// built-in look. A UI author's [Skin] art covers only the frame, background and plates, so these load either way.
		if (!theme::ListThemes().empty())
		{
			const theme::Palette& t = theme::GetActiveTheme();
			const auto picks = v.themeArt.find(t.id);
			for (std::size_t k = static_cast<std::size_t>(ArtKind::kBox); k < kArtKindCount; ++k)
			{
				std::string name = PaletteArt(t, static_cast<ArtKind>(k));
				if (picks != v.themeArt.end() && !picks->second[k].empty()) { name = picks->second[k]; }
				if (name.empty() || Lower(name) == kArtNone) { continue; }
				if (LoadPart(static_cast<ArtKind>(k), name))
				{
					g_partLoaded[k] = true;
					g_activeArt[k] = name;
				}
			}
		}
		// 2.1.5: the switch on with NO [Skin] paths set used to leave every art theme bare - Oathvein, Vel'dun and Norden lost
		// their frame and background while Skyrim's built-in knotwork stayed (the owner, 2026-10-07, after a stray press had
		// switched it on). The switch only means something when a UI author's art is actually named; otherwise the theme's own.
		if (v.skinEnabled && !(v.skinFrame.empty() && v.skinBackground.empty() && v.skinPlates.empty()))
		{
			frame = v.skinFrame;
			background = v.skinBackground;
			plates = v.skinPlates;
			corner = v.skinFrameCorner;
			source = "[Skin]";
		}
		else if (!theme::ListThemes().empty())   // the registry fills at theme::Apply; D3D init can run first
		{
			const theme::Palette& t = theme::GetActiveTheme();
			frame = t.skinFrame;
			background = t.skinBackground;
			plates = t.skinPlates;
			corner = t.skinFrameCorner;
			source = "theme";

			// 2.1.6: THE ART PARTS, BY NAME - the player's pick for this theme (Appearance > Art) wins, then the theme's
			// own part; either wins over the old path keys above. "none" = no art of that kind, not even the theme's.
			const auto picks = v.themeArt.find(t.id);
			for (std::size_t k = 0; k <= static_cast<std::size_t>(ArtKind::kToggle); ++k)
			{
				const auto kind = static_cast<ArtKind>(k);
				std::string name = PaletteArt(t, kind);
				if (picks != v.themeArt.end() && !picks->second[k].empty()) { name = picks->second[k]; }
				if (name.empty()) { continue; }
				const bool none = Lower(name) == kArtNone;
				const std::string path = none ? std::string() : ArtPath(kind, name);
				switch (kind)
				{
				case ArtKind::kFrame:      frame = path; g_frameNone = none; frameName = none ? "" : name; break;
				case ArtKind::kBackground: background = path; break;
				default:                   toggle = path; toggleNone = none; break;
				}
				if (!none) { g_activeArt[k] = name; }
			}
		}

		g_frame = Entry{};
		g_background = Entry{};
		for (std::size_t i = 0; i < g_plates.size(); ++i) { g_plates[i] = Entry{}; }
		if (frame.empty() && background.empty() && plates.empty() && toggle.empty())
		{
			return;
		}
		logger::info("skin: loading art from {}", source);

		Load(g_frame, Resolve(frame), "frame");
		Load(g_background, Resolve(background), "background");
		if (!g_frame.srv) { g_activeArt[static_cast<std::size_t>(ArtKind::kFrame)].clear(); }
		if (!g_background.srv) { g_activeArt[static_cast<std::size_t>(ArtKind::kBackground)].clear(); }

		// A library frame says how it is cut in the .ini beside it (2.1.6).
		if (g_frame.srv && !frameName.empty())
		{
			const FrameCut cut = ReadCut("frames", frameName);
			if (cut.corner > 0) { corner = cut.corner; }
			g_frameDrawCorner = static_cast<float>(cut.drawCorner);
			g_frameTiles = cut.tile;
			g_frameHighlightCorners = cut.highlightCorners;
		}

		// Plates live by fixed name inside one folder, so the author has one path to get right
		// and can supply any subset of the three.
		if (!plates.empty())
		{
			const std::filesystem::path dir(Resolve(plates));
			for (std::size_t i = 0; i < g_plates.size(); ++i)
			{
				const auto file = (dir / kPlateFile[i]).string();
				std::error_code ec;
				if (!std::filesystem::exists(file, ec)) { continue; }   // a missing plate is not an error
				Load(g_plates[i], file, kPlateName[i]);
			}
		}
		// The switch part by name (2.1.6) replaces a plates-folder toggle.png; "none" takes the switch plate away.
		auto& togglePlate = g_plates[static_cast<std::size_t>(Plate::kToggle)];
		if (!toggle.empty()) { Load(togglePlate, Resolve(toggle), "toggle"); }
		else if (toggleNone) { togglePlate = Entry{}; }
		if (!togglePlate.srv) { g_activeArt[static_cast<std::size_t>(ArtKind::kToggle)].clear(); }

		// Clamp the corner so two of them always fit inside the frame texture. An artist who
		// types 64 for a 96px image would otherwise get flipped middle slices, which looks like
		// corrupt art rather than a bad number.
		g_frameCorner = static_cast<float>(corner);
		if (g_frame.srv && g_frame.size.x > 0.0f && g_frame.size.y > 0.0f)
		{
			const float maxCorner = std::min(g_frame.size.x, g_frame.size.y) * 0.5f - 1.0f;
			if (g_frameCorner > maxCorner)
			{
				logger::warn("skin: uFrameCorner {:.0f} is too large for a {:.0f}x{:.0f} frame; clamped to {:.0f}",
							 g_frameCorner, g_frame.size.x, g_frame.size.y, maxCorner);
				g_frameCorner = maxCorner;
			}
			if (g_frameCorner < 1.0f) { g_frameCorner = 1.0f; }
		}
	}

	bool   HasFrame() { return g_frame.srv != nullptr; }
	void*  FrameTexture() { return g_frame.srv; }
	ImVec2 FrameSize() { return g_frame.size; }
	float  FrameCorner() { return g_frameCorner; }
	float  FrameDrawCorner() { return g_frameDrawCorner; }
	bool   FrameTiles() { return g_frame.srv && g_frameTiles; }
	bool   FrameHighlightCorners() { return g_frame.srv && g_frameHighlightCorners; }

	bool DrawsFrame()
	{
		if (g_frame.srv) { return true; }
		if (g_frameNone || theme::ListThemes().empty()) { return false; }
		return theme::GetActiveTheme().knotwork;
	}

	bool   HasBackground() { return g_background.srv != nullptr; }
	void*  BackgroundTexture() { return g_background.srv; }
	ImVec2 BackgroundSize() { return g_background.size; }

	bool BackgroundTiles()
	{
		return g_background.srv && g_background.size.x <= static_cast<float>(kTileThreshold) &&
			   g_background.size.y <= static_cast<float>(kTileThreshold);
	}

	bool HasPlate(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() && g_plates[i].srv != nullptr;
	}

	void* PlateTexture(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() ? g_plates[i].srv : nullptr;
	}

	ImVec2 PlateSize(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() ? g_plates[i].size : ImVec2(0.0f, 0.0f);
	}

	std::string StatusJson()
	{
		// 1.9.8: a resolved path is "Data\SKSE/..." - the backslash went out unescaped and the reply was not valid
		// JSON (found when the new themes first loaded art through this op). Escape \ and " in every string.
		auto esc = [](const std::string& a_s) {
			std::string out;
			out.reserve(a_s.size());
			for (const char c : a_s)
			{
				if (c == '\\' || c == '"') { out += '\\'; }
				out += c;
			}
			return out;
		};
		auto one = [&esc](const char* a_name, const Entry& a_e) {
			return std::format(R"({{"what":"{}","path":"{}","loaded":{},"w":{:.0f},"h":{:.0f},"error":"{}"}})",
							   a_name, esc(a_e.path), a_e.srv ? "true" : "false", a_e.size.x, a_e.size.y, esc(a_e.error));
		};
		std::string plates;
		for (std::size_t i = 0; i < g_plates.size(); ++i)
		{
			if (!plates.empty()) { plates += ","; }
			plates += one(kPlateName[i], g_plates[i]);
		}
		// 2.1.6: the library parts drawing now, every kind by its [Art] key ("" = the built-in look), and the frame's cut
		std::string art;
		for (std::size_t k = 0; k < kArtKindCount; ++k)
		{
			art += std::format(R"({}"{}":"{}")", k ? "," : "", kArtKeys[k] + 1, esc(g_activeArt[k]));
		}
		return std::format(R"({{"frame":{},"frameCorner":{:.0f},"frameDrawCorner":{:.0f},"frameTiles":{},"frameHighlight":"{}",)"
						   R"("frameNone":{},"art":{{{}}},"background":{},"backgroundTiles":{},"plates":[{}]}})",
						   one("frame", g_frame), g_frameCorner, g_frameDrawCorner, FrameTiles() ? "true" : "false",
						   FrameHighlightCorners() ? "corners" : "whole", g_frameNone ? "true" : "false", art,
						   one("background", g_background), BackgroundTiles() ? "true" : "false", plates);
	}
}

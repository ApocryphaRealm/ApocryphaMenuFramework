# ApocryphaMenuFramework - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

<!-- VERSIONING-RULES -->
> **Versioning rules (CLAUDE.md rules 6 and 48 - identical for mods and documents):**
> * `X.Y.Z`. A change increments the THIRD number. At `.9` the MINOR rolls: `1.0.9 -> 1.1.0`;
>   `1.0.10` never exists.
> * The next number is **LAST WORKING + 1**. A failed, scratch or untested test build does NOT
>   consume its number - the next attempt at the same step REUSES it.
> * Numbers are never typed. ONE tool, `.MD\scripts\version-gate.ps1`, holds every version rule
>   and is a GATE that fails: `bump` issues the next number and writes every location, `record`
>   proves it working (evidence + the binary's hash), `gate` refuses packaging or finalizing
>   anything it did not issue. Documents go through `docs-pipeline.ps1 -Action bump` and the rules
>   through `rules-version.ps1 -Action bump`, both of which take their arithmetic from that same
>   tool. A number typed by hand is wrong until the tool agrees.

## 2.1.6 - 2026-10-08 - untested - the art library: build your own look; the Oblivion theme

The owner, 2026-10-08: "start working on the new file structure for amf, add an assets folder with sub folders for the
customization section to choose from, and import the oblivion theme and break down the themes into their art parts and put
them into the subfolders for the theme to draw from and fix the small things that need fixing from before", then "i want 5
art options for each kind that all have different shapes, include the default as one of those 5" and "make sure the help
pages are up to date too", and then: "I want more than just those three kinds ... the scroll bar ... the box shapes because
the boxes are different from the frame ... sliders ... the toggles ... the lines that appear in a mod menu as a horizontal
line under a section header ... the on-screen keyboard ... and anything else that you can think of".

### Added
- **Slider tracks and scroll bar tracks are kinds of their own** (the owner, 2026-10-08: "define ... the slider itself and
  then the slider grab separately ... and the same for the scroll bar"). Appearance > Art gains Slider tracks and Scroll
  bar tracks, one shape per set (assets/slidertracks, assets/scrolltracks), and Colours gains Slider tracks. A slider's
  track used to take the Box art; the ImGui patch now marks it (ImGuiArtPart_SliderTrack), as it marks a tick box.
- **A colour for every art kind** (the owner, 2026-10-08: the Skyrim scroll bar "is currently the same color as its
  background and I don't have an option ... all the different kinds should have a color change option"). Appearance >
  Colours gains Buttons, Tabs, Tick marks, Scroll bar, Scroll bar track, Section lines, Arrows, Switch knob, Popups and
  lists (art) and Mouse pointer (art); Boxes, Selection and Slider grabs are now just those. Each new one follows the colour
  it used to share until it is picked, so nothing looks different until then; each theme keeps its own picks.
- **The art library** - `SKSE/Plugins/ApocryphaMenuFramework/assets/frames`, `assets/backgrounds`, `assets/toggles`: every
  theme's art as one PART per kind, named after where it came from. A frame carries a `<name>.ini` saying how it is cut
  (uCorner, uDrawCorner, bTileEdges, sHighlight = whole / corners). A theme names its parts (sFrameArt, sBackgroundArt,
  sToggleArt) instead of file paths; the old sSkinFrame / sSkinBackground / sSkinPlates path keys still work for themes
  made before.
- **Fourteen kinds, five shapes of each, the default among them.** Frame, Background, Popups and lists, Highlight frame,
  Boxes, Buttons, Tick boxes, Switch, Slider grabs, Scroll bars, Tabs, Arrows, Section lines and the Mouse pointer
  (include/ArtKinds.h). Frames: Skyrim knotwork (default), Norden, Oathvein, Vel'dun, Oblivion map edge. Backgrounds:
  plain (default), grain, parchment, weave, lattice. Switches: rounded (default), Norden, Oathvein, Vel'dun, Oblivion
  scroll. Popups and the highlight frame pick from the frames. Every other kind: the built-in shape (default) and one
  part in each theme's shape language - Norden's rounded corners and bright ticks, Oathvein's slanted cut and scratch,
  Vel'dun's cut corners and inner line, the Oblivion scroll's scooped corners and brass studs (tools/make-control-art.py,
  original art drawn from shapes). The three themes' grain tiles were the same speckle three times, so they are one part.
- **The on-screen keyboard follows the art**: its keys are buttons and its panel a framed window, so Buttons, Frame and
  Background restyle it.
- **The art reaches every page**: Dear ImGui is built with a small patch (cmake/ports*/imgui/amf-art-hooks.patch, one
  function pointer called where ImGui draws a box, button, tick box, slider grab, scroll bar, section line, tab, arrow or
  popup), and AMF draws the picked part there instead. The framework's pages, converted MCM pages, every mod's page and
  the keyboard all change alike, with the text and behaviour untouched. A part is drawn white and tinted with the
  control's colour at that moment, its edge layer in the line colour, so the Colours picks and hover / held states show.
- **Appearance > Art**: grouped as Window, Controls, and Lines and pointer - a dropdown per kind (the theme's own, the
  kind's default, or any part) with a picture of the part drawing now; kept per theme ([Art.<theme id>] in User.ini, like the colours), Theme's own art puts them all back.
  Colours > Frame art tints them. "None" for the frame takes even the Skyrim knotwork away.
- **The Oblivion theme**, from the Oblivion Remastered port: brown ink on parchment with brass accents, the embroidered
  map-edge frame (its stitched edges repeat instead of stretching, scaled with the layout as on the port), a parchment
  ground and a scroll-shaped switch.
- DevBench amf.menu op=art: lists every kind's parts, or picks one for the active theme (kind=Frame, Box, TickBox, ...;
  loaded at the next frame); op=skin reports the parts drawing for every kind and the frame's cut.
- **Prisma MCM Redux menus here** (the owner, 2026-10-08: "Settings wrappers + coexistence", suffix "(Prisma)"). A mod's
  Redux config (`SKSE/Plugins/PrismaMCMRedux/Configs/<ModID>.json`) becomes an entry "<mod> (Prisma)": its pages as tabs,
  an About tab, headers, precise sliders on the config's step, switches, dropdowns, key binds and action buttons, greyed
  out while a setting's condition is not met. Values go to Redux's own INI (`PrismaMCMRedux/Settings/<ModID>.ini`) in the
  text its own window writes; leaving the page sends Prisma_OnSettingsApplied, an action button Prisma_OnAction_<mod>.
  A mod that also has its own or an MCM page here keeps that one entry. PMCM is left alone.
- **Converted menus > Prisma**: the three-way choice every converted system gets (the owner: "Same as sky ui ... The
  standard logic for these menu conversion projects") - here and in Prisma's own window / here only (Redux's key off in
  PrismaCore.ini from the next game start, the player's key remembered and put back) / Prisma's own window only - and a
  switch per Prisma menu. [Prisma] iControl in the INI. DevBench amf.menu op=prisma (list, get, set, action, apply).
- **FLICK: each mod here or in FLICK's own window** (the owner's three-way logic, 2026-10-08). Converted menus > FLICK lists
  every FLICK mod with a choice: this menu, or FLICK's own window when FLICK itself (FUCK.dll) is installed. AMF answers
  FLICK per calling mod, so a mod sent to FLICK gets the real FLICK and the rest stay here. "Both at once" is not
  possible - a FLICK mod hands its page to one framework. Kept in FlickLeftToFlick.txt with the mod's name; applies from
  the next game start. DevBench amf.menu op=flick dll=... place=amf|flick.
- **Switch knobs, a kind of their own** (the owner, 2026-10-08: the switch art "only changed the outline shape of the switch,
  but not the button or circle that's white that's on top of it" - the knobs "should be a separate thing and should be
  gold or match the frame art ... that piece that's at the corner of each frame"). Appearance > Art > Switch knobs, beside
  Switch: one knob per theme, made from that theme's section-line end piece (the owner: "use the art ... at the end of
  each one of those section lines or some variation of them"), in the colours of its frame art and tinted only by the
  Frame art colour (assets/knobs, tools/make-knob-art.py) - Vel'dun's riveted diamond, Oblivion's gold compass star in
  its ring, Oathvein's ridged blade, a rounded plate with Norden's corner ticks (its serifed bar does not read as a
  knob), and for Skyrim the knot from the knotwork frame's corner. Each theme uses its own; Circle (default) is the
  built-in knob.

### Changed
- **The Skyrim theme's accent is white** (testing, the owner: "Everything that was originally yellow ... should be now
  white"): the highlight, and the tick marks and slider grabs that follow it, are the knotwork's white (#F5F2E9)
  instead of the old gold; its description on Appearance > Theme and text says so, in all eleven languages.
- **What you can select has no frame of its own** (testing, the owner: "take the frame art off of the search box and
  the buttons ... so that the frame accentuates the plain appearance", then as the rule: "the things that can be
  selected and hovered over ... when selected and hovered over they have a frame that goes around them. They don't
  need a frame of their own"). In every art set, boxes (the search box, dropdowns), buttons, tick boxes, tabs,
  switches and slider tracks are the set's shape only - the hover / selection frame goes round them. Scroll bars and
  their tracks, which are never selected, keep their framed art. Slider tracks and tabs keep a single hairline in the
  theme's line colour, as minimal as Untarnished's (the owner: without "even a simple thin frame ... you can't see
  where the sliders actually are on the page" - in Skyrim the track's fill is the window's own black - then "all the
  tabs also have a simple, minimal outline as well"); a tab's is open along the bottom.
- **The Skyrim theme uses its knotwork set for every kind**, as every other theme uses its namesakes - so its
  "Theme's own (Skyrim)" on Appearance > Art is the Skyrim set. Its switch is square, so it fills the hover frame,
  and its scroll bar is a knotwork one - the twin strand with the frame's corner knot at each end, in a
  knotwork-framed track, like Oblivion's rope with a compass rose at each end.
- **Every art piece is sharp at any size** (the owner: "make sure that everything is nice and sharp looking, as they're
  all pretty tiny. And make sure all the art pieces for every theme are nice and sharp"). The parts were drawn at twice
  their 1080p size, so at a big text size on a 4K screen the game stretched them and they went soft. Every generated
  part - boxes, buttons, tick boxes and ticks, tabs, slider and scroll grabs and tracks, switch tracks, knobs, arrows,
  pointers - is now drawn at four times its 1080p size (the .ini corners follow; on-screen sizes are unchanged), and the
  art library's textures load with mipmaps, so a part drawn small is filtered cleanly instead of shimmering. The theme
  frames already draw at their art's own size and are unchanged. The scroll bar's old "-track" layers (replaced by the
  Scroll bar tracks kind) are removed.
- **The Oblivion switch matches its set** (the owner: its "outline and shape doesn't match" - "I meant like how the tab,
  slider grab, scroll grab boxes look"). The scroll cartouche is replaced by the Oblivion plate the tabs and grabs use -
  scooped corners, a brass-brown outline - named "oblivion" like the other sets (the theme's sToggleArt follows), so
  the Switch list stays at five.
- **Every theme is its own art; a Skyrim set** (the owner, 2026-10-08: "each theme should basically be its own built-in" -
  "Theme's own (built-in)" beside a separate "Built-in (default)" said the same thing twice). Every Art dropdown is now
  "Theme's own (...)" then the five sets, and a theme with no part of a kind names itself there - "Theme's own (Skyrim)"
  - since its built-in shape IS its own look. The Skyrim theme keeps its own art (the knotwork frame, its built-in
  controls, and now the knot knob cut from its frame); the new **Skyrim** set is a pick for the other themes, made from
  the knotwork itself in its own grey strands on black (the owner: "any of its art ... should follow its Nordic knotwork
  design and color"): the frame nine-sliced down to each plate, its corner knot as the tick and the section line's ends,
  double strands for the line and the arrow, a grey pointer. Background keeps Plain beside its four textures.
- **Converted menus: one MCM tab** (the owner, 2026-10-08: "I just want the tabs that apply to MCM menus to be within a tab
  called MCM"). The sub-tabs are MCM | FLICK | Prisma | Spacing; MCM holds its own row - Menus, Choose menus,
  Remembered settings. The shoulder buttons walk the outer row (it had declared five tabs for six, so they never reached
  Prisma); the D-pad reaches the inner one.
- **The mouse gets the highlight frame on the side list's controls** (the owner, 2026-10-08: hovering "the filter and sort
  buttons doesn't make the frame art appear over them like it does with the controller"): the tick box, A-Z, Fold, Filter,
  Sort and the search box, as the mod names already had.
- The Skyrim knotwork is now a part too (frames/skyrim-knotwork.png); the copy built into the DLL stays as the fallback
  when the assets folder is missing.
- The highlight frame round a selected item takes its form from the frame's .ini: line-and-corners for the knotwork and
  the map edge, the whole frame for the thin-line frames.
- The in-game help (Help > How it looks) covers the Theme and text, Window, Colours and Art pages, every art kind
  named, in all 11 languages.
- tools/make-theme-art.py writes its art into the library; the theme INIs are edited by hand.

### Fixed
- **A slow menu's scripts are no longer paused mid-call** (testing: C.O.I.N. and I.C.O.W. pages waiting out the 15 s
  cut-off). While a converted page's script call runs, the menu lets the game run so the script can; it used to pause
  again after 3 s, and a paused game runs no scripts, so a slow OpenConfig could only sit out the rest of the 15 s.
  The game now runs until the call finishes or the cut-off moves on. The cut-off's log line names the mod. (A mod
  still starting up on a new game - Honed Metal finished its own start about a minute in - answers once it is ready.)
- **A framed pane's scroll bar sits inside its frame** (testing, the owner: the scroll bar art "isn't visible through
  the frame art. So you can tell that it's moving, but you can't see the art"). The frame round the mod list and the
  page reaches into them, and the scroll bar sat under its band - Oblivion's rope grab over the frame's own rope. The
  scroll bar now sits inside the band (each frame's .ini says how far it reaches: uBand), the pane's content
  narrowing to match; a frame-less theme is unchanged.
- **No duplicate in the Art lists** (testing, the owner): the theme's own part, already the first entry ("Theme's own
  (Skyrim)"), is no longer listed again below it.
- **Colours stay inside their frames** (the owner: the switches' "color leaks out from the green and red coloring" and
  "The frame should go around the color, not on top of it"). Each switch track is a fill, tinted on / off, and its
  outline as its own layer in its own colours; and every plate's fill - boxes, buttons, tick boxes, tabs, slider and
  scroll grabs and tracks, switches - is cut to the inside of its outline, so no colour shows outside the frame or
  between the knotwork's strands (tools/make-control-art.py contain()).
- **The Skyrim knotwork frame no longer fills the middle with black** (the owner: "I just don't want it to have a
  background attached to the middle of the frame ... protruding so harshly"). It keeps its black backing under the
  knots and a thin black outline under every strand; the solid black inside the frame is gone, so the menu's
  Background colour shows there. The built-in copy (KnotworkBorder.h) matches (tools/knotwork-transparent.py).
- **Buttons art reaches every button** (the owner: it "doesn't actually seem to apply to ... the filter or sort buttons, or
  where mods keep their save, reload ... and restore defaults buttons"). A framed item was told apart by its colour alone,
  so a theme whose buttons and fields share a colour drew every button with the Box art, and a mod's button in colours of
  its own got none. Now text fields, sliders and drags (ImGui's "inputable" items) are boxes, rows and headers keep
  their look, and every other framed item is a button.
- **Background art covers the whole menu** (the owner: it "only changes the top row when it should apply to all of the
  background"). The panes paint their own colour over the window's, so the art is now drawn in each pane too, over that
  colour. The four textures are three to five times stronger - grain, weave and parchment barely showed before.
- **Section lines redrawn** (the owner's screenshot, 2026-10-08: "not sharp or detailed", and the end pieces "poking into
  the things above and below them when the rows are too close together"). The four lines are drawn at three times the
  size, so the screen always scales them down and they stay sharp, with a dark outline that reads on parchment and black,
  and smaller, finer end pieces (a cut diamond with rivets, a ringed compass star, serifed bars, a ridged blade). A line
  is never drawn taller than the gap between rows, and a short piece - the bit before a heading - is the rail alone.
- **A theme's own art is its namesake in every kind** (the owner: the Oblivion theme's own section lines "differs from its
  namesake"). Norden, Norden - Black, Oathvein, Vel'dun and Oblivion name their own boxes, buttons, tick boxes, slider
  grabs, scroll bars, section lines, tabs, arrows and pointer, so "Theme's own" on Appearance > Art is the same look as
  picking the theme's name there; before, those kinds fell back to the built-in shapes.
- **The Oblivion frame was missing from Appearance > Art.** The list hid every picture ending in "-edge" as another part's
  edge layer, and the frame is named oblivion-map-edge; a layer is now hidden only when the part it belongs to is there.
- The slider in the Colours preview moves one step of its shown digit per press, like every other slider (rule 68); the
  package gate's temporary exemption for the framework is gone.
- The Choose-menus help ("Switch off a menu to keep it in SkyUI's menu only ...") shows again: 2.1.5's MCM Memory import
  help had taken its translation key. That help has its own key now (AMF_McmMemImportHelp), in all 11 languages.

## 2.1.5 - 2026-10-08 - working - converted MCM pages: help bar, heading colours, icons, translated headings

The owner, 2026-10-07, after comparing AMF's converted MCM pages with MCM Bridge's: "it's really not that bad in
comparison. If anything, I just want AMF to include Font Awesome for the generated menus ... different colors to
different things", the "$" and underscores in section headings, and the help text kept inside AMF's window.

### Added
- **A live preview on Appearance > Colours** (the owner, 2026-10-07: "a column that displays an example for when you
  change a color"): beside the colour list, tabs, a section heading, an option with its value and help, a switch with
  the theme's highlight frame round it, a tick box, a slider, a button, a selected row and a row under the mouse - all
  in the colours in use, so a pick shows at once. Off the D-pad's path.
- **Preview first, then Apply** (Appearance > Colours, [Display] bColorsApplyNow; the owner, 2026-10-08): by default a
  colour you pick shows only in the preview column; **Apply**, right beside the switch, puts the picks on the current
  theme, and Discard drops them. Switched on, "Show changes everywhere straight away" changes the whole menu at once
  (and applies anything waiting). Theme.cpp now works a look out from any set of picks (BuildLook), which the preview
  borrows.
- **Help bar** (Appearance, on by default; [Display] bHelpBar): on a converted MCM page the highlighted option's help
  shows in a bar under the right pane, inside the menu, like SkyUI's info line - with the mouse or the controller. It
  wraps to the pane and keeps the last help shown. Off: the help shows as a popup, now wrapped to the right pane's width
  and placed inside it (it used to follow the mouse off the window). HelpBar.cpp.
- **Text colours by role** on converted pages: labels in the theme's text colour, values grey, section headings in a
  heading colour, help and page notes in a help colour. Every theme carries both (new theme keys `sTextHeader`,
  `sTextHelp`; a theme without them takes its accent for headings and its dim tone toward the text for help). Skyrim and
  Untarnished: gold headings, steel-blue help.
- **Colours** (Appearance, the owner: "changing the different things that make up the framework's art, like its frame,
  box, sliders, and other things to different colors", "change the background color, like how Oathvein is gray"): the
  player's own colour for twelve parts of the menu over the theme's - background, frame lines and borders, frame art (a
  tint over the theme's frame and background pictures), boxes and buttons, text, secondary text, selection and tabs,
  sliders and tick marks, switch on, switch off, section headings, help text. A picker each, a row of twelve preset
  swatches the D-pad walks, Theme to go back, and All back to the theme. Every shade the theme works out (hover washes,
  separators, the see-through fade) follows the picked colour. **Kept per theme** (the owner: "if they change the
  highlight color on the Skyrim theme from yellow to blue, then it should stay that color only in the Skyrim theme" -
  "each theme can be considered a kind of preset"): each theme keeps its own changes, saved as they are made, in a
  [Colors.<theme id>] section of User.ini; All back to the theme clears the active theme's only. Plus a **Hover
  highlight** colour.
- **The theme's frame round the highlighted item** (the owner: "the same thing that Skyrim for Witcher 3 does by having
  frame art on the selected box ... while your mouse hovers over different menu names, it has a frame going around it",
  "the frame should match the theme frame"): the active theme's own frame art - Skyrim's knotwork, or a theme's
  frame.png - round the controller / keyboard highlight anywhere in the menu and, fainter, round the menu name under the
  mouse. A theme's own frame art (Oathvein, Vel'dun, Norden) is the whole frame, scaled to the row; the Skyrim knotwork,
  whose solid bands covered the row's text when squeezed onto it (the owner's screenshot), is a thin line with its corner
  ornaments. Untarnished, which has no art, gets a plain line. The blue nav box stays. The Hover highlight colour
  (Appearance > Colours) sets the hover wash and the hover frame's tint; unset, it follows the selection colour.
- **Spacing of converted pages** (Settings > MCM menus; a player found Atlas Map Markers' rows "very close together"):
  the gap between the two columns and extra space between rows, precise sliders ([MCM] uColumnGap, uRowSpacing; 75% and 20% by default - the owner, 2026-10-07).
- **Font Awesome icons** on converted pages: key buttons (keyboard), clear (x), reset to default, text fields (pen),
  colours (palette), buttons that run something (chevron), options the mod disabled (lock), help and notes (info),
  loading (hourglass), what cannot be drawn (warning). Ten solid glyphs merged into the text face (AmfIcons.h).

- **Import from MCM Memory** (Settings > MCM menus > Remembered settings; the owner: "the whole point of having the import
  from MCM memory feature is so that they can import their settings and then deactivate MCM memory"). Shown when MCM
  Memory has a saved profile: pick it and import. Every setting it saved that AMF can keep is merged into the ACTIVE
  profile (his choice) - script menus as page + name + type + which-of-that-name (counted in slot order), MCM Helper
  menus' live Global / Property controls as their values (ModSetting values are in MCM Helper's own INI already and are
  counted, not copied); its enable switches come first, its excluded pages stay out, and a menu it leaves out of its
  automatic restore is left out of AMF's. The result says what came over, what MCM Helper keeps, which menus are not in
  this game (import again later) and which rows could not come (replayed buttons, rows with no name, cycling text) -
  by name. Its files are only read. While MCM Memory's own automatic restore is on, the tab says both will set the same
  menus after a new game (his choice: AMF restores anyway). Matched by the menu's ModName (MCM Memory keys a menu
  "<script>::<ModName>"). DevBench: amf.mcm op=remembered action=mcmmemory | import {name}.

### Changed
- **Sub-tabs on Appearance and MCM menus** (the owner: "sub tabs ... rather than a long page with collapsible
  sections"). Appearance: Theme and text / Window / Colours. MCM menus: Menus / Choose menus / Remembered settings /
  Spacing. They are declared to the nav like a mod page's own tabs, so the bumpers walk only them, wrapping round.
- **Y in a page goes up to the main tabs** (the owner: "when pressing Y, instead of zooming all the way out to the main
  [left] pane ... it should just send you to the main tabs"). Y in the right pane puts the highlight on the open main
  tab (Settings' General / Appearance / MCM menus / Menu list, Controls', Help's, a mod's pages). It used to open the
  options of the list's highlighted row and pull the highlight out to the list - the list took Y every frame; it now
  takes it only while it has the highlight.
- **Y no longer acts like A in our window.** It reached ImGui as its "activate / type into" button, so on a page it
  switched a toggle and opened a slider as a text box. In AMF's window Y does only the framework's jobs; a mod's own
  window still gets it. The Controls page's text for that action says both jobs.
- **Fold, on the Mods row** between A-Z and Sort (the owner: "a collapse and uncollapse toggle. When it's on, it
  collapses all [separators], and when it's off, it uncollapses them. And if the user goes and uncollapses one
  individually, then the toggle doesn't auto-reassert itself until it's toggled again"; "the sort button ... on the
  farthest right"). It acts once, when switched; [Display] bFoldAllSeparators keeps where it was left.
- **Filter, on the Mods row** just before Sort (the owner: "a filter button ... a small context menu where you can type
  in a word ... whether you want to filter for that item or filter out that item", then "persistent and saved within
  AMF ... the same way that Mod Organizer 2 does it with a little colored button with a plus minus"). A saved list of
  words, each with a coloured button as MO2's filter rows cycle: off, green + (only menus with the word), red - (menus
  with it hidden) - a click steps forward, a right-click back; an x removes a word; Add puts a new one in as +. Match
  every + word (off: any one, MO2's OR). + words list the matches flat, as the search does; - words alone keep the
  separators. Matched against the names shown. Kept between games ([ListFilter] sWords, bMatchAll); the button is
  coloured while any word is in use.
- **The persistence test is gone** from the bottom of Settings > General (the owner: "get rid of the persistence test");
  it showed only at log level 0. The per-save channel itself is unchanged.

### Fixed
- **Custom menu art switched on with no art named** left Oathvein, Vel'dun, Norden and Norden - Black bare - no frame,
  no background, a plain line for the highlight - while Skyrim's built-in knotwork stayed (the owner's test, 2026-10-07).
  The switch now uses a UI author's art only when [Skin] names some; otherwise the theme's own art draws. Skin.cpp.
- **The highlight frame round the top bar on D-pad up** from the panes (first seen as "a single straight horizontal
  line" under the title): the invisible band that drags the window by the mouse was a D-pad stop, and the new frame
  drew round it. The band is mouse-only now; no frame is drawn round a rect under half a line tall either.
- **A separator would not open after Fold** until the highlight left it and came back: the "(n)" in a folded row's
  label made it a new item on every fold, so the highlight held an ID that no longer existed. One ID now, folded or open.
  The same fault, same fix: the Filter words' +/- buttons (A worked once, then not until the highlight moved off and
  back - the owner, 2026-10-07), the A-Z / Z-A switch, and on converted MCM pages the key buttons ("..." while
  waiting) and text rows that show their own value.
- **An active Filter was easy to miss**: a saved "-mcm" hid every converted page, so groups looked empty after Fold
  opened them. The button now reads "Filter (n)" while words are in use, and a separator's "(n)" counts only the rows
  the Filter lets through. The Fold press also logs its frame and input, to catch a double fire seen once (fold, then
  open, 0.3 s apart).
- **The highlight follows the bumpers**: switching a tab with LB/RB changed the tab but left the highlight where it
  was (the owner, 2026-10-07). The opened tab now takes it - the main tabs and the settings sub-tabs.
- **A highlight frame round nothing** (the owner's screenshot, 2026-10-08: a small frame beside the General tab): the
  frame was drawn where the highlighted item last stood even when that item was not drawn this frame (a tab change).
  It is drawn only when ImGui saw the item this frame (NavIdIsAlive).
- **A converted MCM page stuck on "Loading"** with "Pause the game" on (the owner, 2026-10-08, Atlas Map Markers: the
  first page drew, the others only after closing and opening AMF). The game's script engine can stop while the game is
  paused, so the page's SetPage waited out its 15 s. While a menu's script call has been waiting 0.12 s, the pause now
  lets go, and holds again once the queue is empty - time moves only while a page loads (at most 3 s per call). AMF no
  longer adds its own pause on top of the journal's when opened from the System row. Taking the journal's own pause away
  as well froze the game in testing, so it is never touched.
- **A-Z / Z-A did nothing in a list of separators**: it sorted only the mods above the first separator. It now sorts
  the mods inside every separator (and the loose ones), and never moves a separator (the owner, 2026-10-08). Moving a
  mod while sorted changes your own order underneath, so unticking alphabetical still gives back the order you made.
- **Section headings showed "$KEY_Names".** A heading wrapped in font tags (Atlas Map Markers:
  `<font color='#FF9900'>$ATLAS_GlobalMarkerSettings</font>`) was never looked up - only text starting with "$" was.
  The key inside the tags is now translated ("GLOBAL MARKER SETTINGS"). A key no translation file carries now reads as
  words - its "$", its capitals prefix and underscores dropped, camelCase spaced ("Global Marker Settings") - where it
  used to lose only the "$" (a menu whose own name is such a key gets the readable name).
- **"\n" in help text** showed as the two characters; it is now a line break, as in SkyUI.
- **Values ran into the next column** on two-column pages ("DefaultGem Geodes") and under the pane's right edge: the
  columns now have a gutter and right-aligned values stay clear of the edge.

## 2.1.4 - 2026-10-07 - working - SKSE Menu Framework 3.18's interface; D-pad right stays on the Mods row's controls

### Added
- **SKSE Menu Framework 3.18's interface** (a Nexus report, 2026-10-07: NPC Preset Applier, which needs SMF 3.18,
  opened under AMF but its preset portraits never showed and could not be generated). Compared export for export
  with SMF 3's own source (QTR-Modding/SKSE-Menu-Framework-3, 2026-09-28), AMF lacked three functions - a mod
  asking for them got AMF's logging stand-in, which returns nothing:
  - `GetMenuFrameworkAPIVersion` - now 1, SMF 3.18's number: the check a mod needing 3.18's functions makes;
  - `RenameSection` / `DeleteSection` - rename or remove a mod's whole menu, or one page and everything under it.
  `GetMenuFrameworkVersion` now reports 3.8, what SMF 3.18 itself returns (it was 3.7).
- **Menu paths read as SMF 3.18 reads them:** an escaped `\/` is a slash inside a name, and a path with an empty
  segment is refused (AddSectionItem split at the first raw slash before).

### Fixed
- **Mods waiting on SKSE Menu Framework's frame events now get them.** AMF never sent SMF's open, close,
  before-render and after-render events, so a mod that does its work in them waited forever: NPC Preset Applier's
  portrait batch started and never made a portrait. AMF now sends them where SMF 3 does: open and close when its
  menu or a blocking mod window opens or closes, before-render ahead of each frame's draw, after-render once the
  frame is drawn. Renderer.cpp: PresentHook.
- **`LoadTexture` reads DDS files and paths with non-English letters.** A `.dds` went through Windows' WIC decoders,
  which cannot read DDS, and failed with "could not decode"; it now loads as DDS, as SMF does. The path was widened
  byte by byte, so any non-English letter in it (a Windows user name, a localised folder) broke the load; it is now
  read as UTF-8.
- **D-pad right on the Mods row skipped its own controls.** On the row with the alphabetical tick box, the A-Z / Z-A
  switch and the Sort button, right went straight across to the options pane instead of to the next control (the
  owner, 2026-10-07: "pressing D-pad right skips past the toggle and sort button and goes to the right pane"). A
  right press in the list pane is now decided one frame late, the way the options pane's sideways press already
  is: if it moved the highlight to a control beside it, it stays in the list pane; a press that moved nothing
  (on a menu entry, or on the row's last control) goes across to the options as before. Renderer.cpp:
  g_pendingSideRight.

## 2.1.3 - 2026-10-07 - working - no freeze when a page first draws Font Awesome icons; SVG icons load

### Added
- **SVG textures for SKSE Menu Framework mods.** `LoadTexture` now reads `.svg` files (nanosvg, zlib licence - see
  THIRD_PARTY_NOTICES.md). Walk With Me's icons (Data/Interface/WalkWithMe/*.svg) were all "could not decode" in a user's
  2.1.2 log and missing from its page. An SVG is drawn with its long side at 256 px or more, so it stays sharp when shrunk;
  the size reported to the mod is the SVG's own. Checked in game with Walk With Me 0.2.5: 12 SVGs loaded, its section
  icons drawn.

### Fixed
- **The game froze (2.1.1: crashed) the first time a page drawing Font Awesome icons was opened** - KnightQueen1 (Nexus,
  2026-10-07, AE 1.6.1170): clicking Cinematic Conversation Camera or MCM Memory, both of which push the "solid" face.
  Their log ended at "atlas 3 built", the rebuild that added the face. That rebuild began with `io.Fonts->Clear()`, which
  frees every ImFont - and a mod drawing through this framework may keep the ImFont* it was handed (its HUD element, its
  window, a pushed face); with ~100 mods, theirs did, and drew with a freed font on the next frame.
  A face a mod asks for is now ADDED to the built atlas, which is built again: nothing is freed, and ImGui 1.90.8 refills
  the existing ImFont objects in place. The log says so on each add ("the text face a mod may hold is the same object,
  rebuilt in place"). Full rebuilds stay for the player's own language, face and text-size changes. (Renderer.cpp:
  AddIconFace / AddIconFaces / RequestIconFaces; ConsumerSurface.cpp asks for the add.)
- Checked in game (SE 1.5.97, profile MCM Minimal with MCM Memory 1.5.6, Cinematic Conversation Camera 1.5.0 and Risa's
  All In One Menu 5.5): both pages draw their icons; the log shows the face added and the held text face unchanged. The
  freeze itself did not happen here with 2.1.2 either - it needs a mod that keeps a font, which the reporter's list has.

## 2.1.2 - 2026-10-06 - untested - the menu's words in every language, and MCM settings that come back on a new game

The owner, after 2.1.1: "Go ahead and make the changes, but don't post anything."

The owner, 2026-10-06, after comparing another mod that saves MCM settings (Nexus 189722): "Let's build it into AMF so that the settings you change
for all these different MCMs are backed up and saved so that on a new game they still apply." Plan:
4. plans\amf-remembered-settings\PLAN.md.

### Added
- **AMF remembers MCM settings and sets them again on a new game** (MCM menus page, "Remembered settings"; RememberedSettings.cpp). A new game forgets what save-held MCM settings
  were set to; AMF now remembers them in a profile and sets them again. That mod's own code was not read; only its page
  was, to know what players expect. It covers:
  - SkyUI menus written only in a mod's script (phase 3);
  - MCM Helper controls kept in a global or a script property (phase 2).
  MCM Helper's ModSetting values are in its own INI and survive a new game already.
  - **Automatic backup** (`[RememberedSettings] bAutoBackup=1`). AMF records each change it makes there.
    - Script menus: after the page is rebuilt, the option's new value is read back.
    - Action rows are never recorded.
    - Each setting is kept by its raw page and label (and which of same-labelled options it is), so a language change
      does not lose it.
    - Settings are kept in the order first changed, so an "enable" switch comes back before what it reveals.
  - **Back up all now**: opens every menu AMF can read, builds every page and reads every toggle, slider, menu, colour,
    key and text field - including what was set in SkyUI's own menu.
  - **Restore on a new game** (`bRestoreOnNewGame=1`): after a NEW game only (a loaded save keeps its own), once the
    menus appear, from 40 s to 4 min.
    - Script menus get the page's own calls (Request then accept, the page rebuilt after each) and OnConfigClose at the end.
    - MCM Helper controls get the loader's own write, OnSettingChange and action, then OnConfigClose.
    - Only settings that differ are touched; a menu's saved text is found again in its list (the saved index is the
      fallback).
  - **Restore now**, on demand.
  - **Profiles**:
    - stored as `SKSE\Plugins\ApocryphaMenuFramework\RememberedSettings\<name>.json`, outside the save and the download, written
      through a .tmp;
    - new (empty or a copy), switch and delete;
    - `sProfile`.
  - **Per menu**: whether it takes part in the automatic restore, and Forget. A menu not in this game stays in the profile
    untouched.
  - DevBench `amf.mcm op=remembered`: action status | backup | restore | records | forget | auto | profile | create | delete |
    newgame.
  - 23 strings, in all eleven languages (`tools\remembered_settings_strings.py`).
  - Named "Remembered settings" everywhere - the page, the INI section `[RememberedSettings]`, the profile folder and the log, at the owner's request: it remembers settings and sets them again on a new game.

### Changed
- **A converted menu's "(MCM)" ending shows in the language picked** - "Photo Mode (Mod-Konfig.)", "Photo Mode （模组配置）" -
  on screen only, in the side list, the MCM menus list and Bring in from SkyUI. The stored entry name keeps " (MCM)": it is
  what the player's order, renames and learned placements are keyed on (`personalization::SetEntryNameFilter`, registered
  by McmSort like the separator filter). A name the player gave an entry shows as typed. One string, eleven languages.
- **"MCM" written out in the body text of every language** - 13 strings in each of Czech, French, German, Italian,
  Japanese, Korean, Polish, Russian and Spanish (Chinese had it in 2.1.1): the language's own words for "mod configuration
  menu". MCM Helper, a product name, stays. French help names its categories as 2.1.1 renamed them (Affichage, Combats).
- No duplicate "Utilities and Fixes" to merge: the owner's saved layout has one utility separator ("Utility"); the second
  existed only during 2.1.1's test sorts, and since 2.1.1 a sort reuses "Utility" for that category.

## 2.1.1 - 2026-10-05 - working - one menu key, text that fits, separators in your language, a see-through window

Found in the 2026-10-05 Nexus banner reshoot (every page shot in game, Norden - Black, from the journal's SKSE MENUS row)
and from the owner's notes on it. The owner: "go ahead and start fixing AMF".

### Fixed
- **Controls no longer cuts its text off** in the narrower window the SKSE MENUS row opens. The key column is as wide as
  its widest key ("unbound", "Backspace", "Left stick left" were clipped to "unbour", "Backsp", "Left stick lef"), each
  function's description wraps inside its column instead of running past it, and the note about reserved keys has a
  line of its own instead of running off the pane beside the buttons.
- **One place to set the menu key** (the owner: "there's duplicate entries for the menus toggle key ... There should
  just be one"). Settings > General no longer has its own Menu toggle key / Rebind; Controls > Open and close the menu is
  the one place. The "Window position: Centre" line beside it, which offered nothing to set, went with it.
- **Category separators follow the language you pick** (the owner: "when you change the language, the separators did
  not change their language"). The sort now stores a category separator by its English name and shows it in the
  language AMF is showing, in the list, its folded count, Send to and the Menu list table. A separator you named
  yourself shows exactly what you typed. Separators an earlier sort made in English follow the language too.
- **Converted MCM pages follow the language you pick** when the mod ships a translation for it. A mod's own text was
  read only in Skyrim's game language; it is now read in the language AMF shows, then the game's, then English, and
  read again when you change the language. A menu's entry and tab names keep the language they registered in - the
  entry name is what your order and renames are keyed on.
- **Help is up to date** in all eleven languages: the SKSE MENUS row instead of "System -> Mod menus"; the menu key under
  Controls; the bumpers and Page Up / Page Down walking the tabs; Theme and Font under Settings > Appearance; settings
  kept in User.ini (the download never contains it), not the shipped INI; and the log level as `[Log] uLogLevel` in the
  INI - there was never a Log level setting on the page.
- **Controls fits any window size** (found again at the default size in the banner reshoot): the button column is as wide
  as Rebind and Unbind side by side instead of a flat 13 em, and function names wrap instead of clipping ("Open and
  close th").
- **MCM entry and tab names stay in the game's language.** The language change above had them follow AMF's picked
  language when a menu registered; an entry's name keys the player's order, renames and learned placements, so a
  session in another language turned "Accuracy - Localized Damage (MCM)" into a German name, loose at the top of the
  list. Page text still follows AMF's language.
- **Separators and tabs read in every language** (the owner: "utility separator, NPC separator, mcm tab are not in
  chinese" ... "persist in every translation" ... "German was missing the German word for audio separator. But you
  should probably double check all the separators for all the languages"): a separator named by a category's key
  ("Utility") is that category; the NPC category and the MCM menus tab are written out in full in every language
  (Nichtspielerfiguren, Mod-Konfigurationsmenüs ...); category names that were the English word now use the
  language's own (Ton, Son, Suono, Sonido, Spielmechanik, Jouabilité, Animationen, Combats, Affichage); Chinese uses
  模组配置菜单 throughout. Every category checked in all eleven files: none missing, none left as the English word.
- **Dropdowns open without an empty band** above and below the list (Theme, Font, Language, Bring in from SkyUI, and
  every dropdown on a converted MCM page). ImGui's list takes its padding from the window's, which the theme sizes for
  the knotwork frame; dropdowns now open with the right-click menus' padding (`theme::BeginComboTight` / `ComboTight`).
- **The rename and "Name this separator" boxes** lost the empty title strip across their top.

### Added
- **The window: three switches on Appearance, all on by default** (Barzing on Nexus, 2026-10-05: "the possibility to
  resize window also in height size", "the possibility to move the window", "the semi transparence of the window"; the
  owner replied "ill add" them, then: "seperate toggles" ... "in apperance teb" ... "have it default to on, along with the
  other settings we just added, like the move the window and see-through window at max opacity").
  - **Move the window** (`[Window] bMovable`): drag the top row - the name and version - and the window follows, kept
    whole on the screen; it reopens where it was left, and Reset to the default size puts it back in the middle. The
    body never drags it, so a page's sliders and rows keep their clicks (why ImGui's own move stays off). Off: it sits in
    the middle of the screen.
  - **Resize the window** (`[Window] bFreeResize`): on, any edge or corner resizes it - height and width alike, a corner
    no longer keeping the shape; off, it cannot be resized at all (`ImGuiWindowFlags_NoResize` - the owner: "make sure
    the toggle actually toggles off the resizing"). Checked in game with `tools/window_watch.py` while the owner dragged:
    the height had looked fixed only because his saved size was 99% of the screen tall, edges at the screen's edges;
    at the default size the top edge took it 1124 -> 1795 and back.
  - **See-through window** (`[Display] bSeeThrough`) and **Window opacity** (`uWindowOpacity`, 5-100%, a precise
    slider; 100 by default, so it looks solid until lowered). Not one factor for everything (the owner: "affect the
    black background proportionally more than things like the text or the boxes, because the black background is what
    is blocking their view"): the window and pane backgrounds - and a UI author's background picture - take the opacity
    as set; boxes, borders, tabs and scrollbars keep 30% plus 70% of it; text keeps 60% plus 40% of it. Right-click
    menus and tooltips stay solid.
- **The Mods row: a tick box, one switch and a Sort button** (the owner: "we only need one toggle because switched off,
  it would be Z to A, and switch on, it would be A to Z ... a tick box ... Whether they want alphabetical sorting on ...
  and then a sort button for sorting and adding the separators"). The tick box turns alphabetical order on (off: the
  order arranged by hand); the switch is A-Z on, Z-A off, greyed while the box is unticked; Sort runs the MCM category
  sort, separators and all (Undo stays on Settings > Menu list). A tick box against rule 32 because the owner asked for
  one by name. Replaces 2026-09-19's two switches.
- DevBench: `op=state` reports the window's real rect (`mainWindow`), and `op=mouse` presses or releases a button, so a
  drag can be driven and measured (`tools/window_resize_test.py`); `tools/window_watch.py` only reads, for when the
  owner drives.
- Strings: the window and Mods-row switches, their help and tips, all eleven languages; compiled defaults match the
  shipped INI (rule 16).

### Changed
- **The persistence test is hidden** (the owner: "we can hide the persistence test"). The developer's "Persistence test
  (S10)" box at the bottom of Settings > General shows only at `[Log] uLogLevel=0`.
- **The sort follows the owner's own placements** (rule 67, from his McmSortLearned.txt): ASG Multithreaded -> Magic and
  Skills (it is Acquisitive Soul Gem Multithreaded - the earlier taught rule had read "ASG" as a grass mod), OCPA ->
  Gameplay, Read the Room -> Animation, Stendarr Rising / Hall of the Vigilant -> Quests and Places. 35 taught rules,
  163 in all (`tools/mcm_sort_check.bat`: rules ok 163 of 163).
- Seven strings retired with the Settings menu key (eleven languages).

## 2.1.0 - 2026-10-05 - working - choose which MCM menus come in, and sort them into categories

xLenax on the AMF page, 2026-10-04: with the MCM options off, the menus "still appear in the settings page, just not in
the actual Menu", and "I'd like to have an option to choose which MCMs I'd like to import instead of importing all of
them or None" - with about 200 MCMs, wanting only the few in regular use.

The owner, 2026-10-05, for the same release: "an auto sort function which sorted the imported menus into categories,
sort of like our mod manager plugin but built into AMF. And it can just sort by name, it doesn't have to be perfect."

### Added
- **Choose which MCM menus appear here.** A folded section under the MCM switches on the settings page lists every menu
  found, MCM Helper and script ones alike. It shows how many are in, and has a filter by name, All on / All off, and a
  switch per menu.
  - A menu switched off leaves this menu and stays in SkyUI's own menu. The "take out of SkyUI's list" switch then
    leaves it alone, and gives it back if AMF had taken it out.
  - Choices are kept per menu in `SKSE\Plugins\ApocryphaMenuFramework\McmImport.txt`, outside the download, so an
    update never resets them.
- **Bring in MCM menus not switched below** (`[MCM] bImportNewMenus`, default on, which is how 2.0.9 behaves). Off: only
  the menus switched on in the list come in, so a 200-menu list can start from none and pick the few in use.
- DevBench `amf.mcm op=import`:
  - action list: every menu with its key, entry, kind and whether it is in;
  - set {key, on}: one menu;
  - all {on}: every menu;
  - new {on}: the default for menus not chosen by hand.
- **Sort MCM menus into categories** (settings page, under the MCM menu choice). Each MCM menu that comes in goes under a
  Menu-list separator for its kind: Interface, Controls, Camera, Combat, Animation, Magic and Skills, Characters and
  Bodies, NPCs Followers and Creatures, Audio, Quests and Places, World and Visuals, Gameplay, Utilities and Fixes, or
  Other when nothing matches.
  - A menu is judged by its name only: its entry name plus its mod or plugin name, each whole and with joined words
    split ("TrueHUD" is also read as "True HUD"). The name rules are MO2 Modlist Manager's, generated into
    `source/McmCategoryRules.inc` by `tools/gen_mcm_categories.py` (128 rules). `tools/try_mcm_categories.py` and
    `tools/mcm_sort_check.bat` run the same steps on a list of names, in Python and under MSVC's regex.
  - It only rearranges. No menu is hidden or removed, and only menus that come in are moved.
  - A separator with that name already in the list (the shown name or the English one, any letter case) is reused, so a
    second run makes none.
  - A menu already under any separator stays where it is, whether the player put it there or an earlier sort did, so
    running it again changes nothing. The page has this one button (the owner, 2026-10-05: "I just want it to add a
    button that does it"); sorting those too ("re-sort all") is a DevBench op only.
  - **Undo the sort.** The order from before a sort that changed anything is saved as the layout preset "Before MCM
    sort". The button loads it, then deletes it. It is also listed with the other layout presets.
- **The sort learns from your own moves** (the owner, 2026-10-05: "if they add a new mod menu and sort it and it's not in
  the right location then they can sort it into the right one and the sorter will acknowledge that as a new rule
  automatically").
  - Move an MCM menu under another category's separator and AMF remembers that category for it.
  - The next sort puts it there, even after Undo or a fresh list.
  - Moving it back under the category the name rules give forgets it again.
  - Kept per menu in `SKSE\Plugins\ApocryphaMenuFramework\McmSortLearned.txt`, outside the download, so an update keeps
    it (the owner: "so that they keep it when they update"). The new gate rule `no-player-data-files-in-packages` refuses
    any package carrying it, `McmImport.txt`, `McmHiddenInSkyUI.txt` or `User.ini`.
  - Learning runs only when the Menu list's layout changes (`personalization::LayoutRevision()`), not every frame.
- **31 more name rules** taught by hand from the names that landed in Other on the owner's list (C.O.I.N., TNG,
  ASG, Recorder, Order Squad and so on), weighted above the generated ones. Other is empty on that list now.
- **Framework Settings is split into tabs by area** (the owner, 2026-10-05: "divide the AMF settings page into several
  tabs that are divided by their area that they affect"): General, Appearance, MCM menus and Menu list. The bumpers and
  Page Up / Page Down walk them, as on Controls and Help. The sort button and its Undo sit at the top of Menu list.
- **Keep in SkyUI only** in a Menu-list entry's right-click menu: takes that MCM menu out of AMF and leaves it in
  SkyUI's menu, the same as switching it off in the MCM menus tab.
- **Bring in from SkyUI**: a dropdown in the MCM menus tab listing only the MCM menus left to SkyUI; picking one brings
  it in.
- **Defaults keep SkyUI's menu whole**: every menu comes in and "Take those mods out of SkyUI's list" stays off, so a menu
  can still be changed in SkyUI too.
- DevBench `amf.mcm op=sort`:
  - action preview: the category each menu would get;
  - run / all: sort (all = Re-sort all);
  - undo: restore.
  - learned: the placements learned from your moves.
  - Every action also returns the Menu list's separators with their menus.
- 40 new strings, in all eleven languages.

### Fixed
- **The Menu list on the settings page no longer shows entries that have nothing to show.** That covers an MCM loader
  switched off, or a menu left out with the choice above. Before, those entries stayed in the rename/reorder table
  while the menu itself had no row for them.
  - Each entry keeps its place in the saved order.
  - The number in the position box is its place among the rows shown, and a number typed there moves it to that row's
    place.
- **A game loaded while a SkyUI-list pass ran no longer stalls the give-back** (pre-release review, 2026-10-05). A load
  drops Papyrus calls in flight, so that pass never finished. The passes after the load only marked "again", and nothing
  asked again after the 60 s override. "Hide off, then load at once" could leave the menus AMF had hidden out of SkyUI
  until the next switch change.
  - A load now clears the pass state and starts a new pass generation.
  - A pass queued before the load does not run, and a late finish from it is ignored.
- **The MCM Helper list is no longer grown while another thread walks it** (same review, crash-class). Switching the
  MCM Helper loader on for the first time in a session read the configs into the shared list one by one. Meanwhile a
  SkyUI-list pass, the import list or DevBench could be walking it on another thread.
  - The configs are now read and registered into a list of their own, then published in one move under the lock.
  - Those walkers read a copy taken under the lock.

## 2.0.9 - 2026-10-04 - working - every MCM menu, whatever SkyUI's list does

### Known
- Under **Menu Maid 2**, "Take those mods out of SkyUI's list" takes nothing out. Menu Maid keeps its list in its
  DLL and copies it into SkyUI's `_modConfigs` only when the Journal opens, so AMF reads an empty list there (and the
  DevBench layout says "stock"). AMF still shows every menu. Found in the 2.0.9 combined run; parked (the owner
  does not use Menu Maid 2).

### Fixed
- **Script MCM menus missing from AMF in a large list** (Soulsthat, Nexus, 2026-10-04). In his list these menus never
  appeared: CBBE 3BA, Custom Skills Menu, Dialogue Timescale, DVA, Fort Takeovers, Helios, HMA Expanded, LOD Reload
  Bug Fix, LOTD, Missives, moreHUD, OBody NG, QuickLoot IE, Seasonal Weathers, T.N.G. and the Wyrmstooth MCM.
  - **Cause.** AMF only took a SkyUI config script whose `_configManager` variable passed a "registered" check, and
    that check tested the variable's stored TYPE (`IsNoneObject`), not its value. A config SkyUI never registered
    holds None in one of two forms: an object-typed None, which the check let through, or a None-typed value, which
    it dropped. A config goes unregistered when it falls past SkyUI's 128-menu limit (RegisterMod returns -1),
    registers late, or another mod has replaced the manager. In Njordlinger (MCM Unlocked), 23 of 48 configs held no
    manager after a New Game.
  - **Fix.** A config is taken when it holds a manager (the value, not the type) OR its own script has run its
    set-up (`_initialized`, so its name and pages are filled), whatever form its None takes. SkyUI 5.2 uses
    `_configManager` only to remember that a config registered, so AMF drives an unregistered one the same way.
    The log notes "not registered with SkyUI's manager; AMF drives it directly". Each discovery line counts the
    unregistered menus, and DevBench `amf.mcm op=skyui action=list` gives each menu's `registered`.
  - Tested 2026-10-04 (Njordlinger Test, MCM Unlocked, a New Game through Alternate Perspective):
    - 48 found, 23 unregistered.
    - After SkyUI's own `SKICP_configManagerReset`: 48 found, 48 unregistered.
    - moreHUD's `_configManager` made None-typed (TestBench papyrus `var`): still found.
    - T.N.G. with `_initialized` false: dropped (47), and back when set again.
    - AMF opened the unregistered, None-typed moreHUD menu and read its General page (15 options).
  - Discovery now looks again at 1, 2, 4 and 8 minutes after a load (it used to stop at 40 s). It also looks once
    each time the AMF menu opens, at most once every 2 s, so a menu that set itself up late is listed when the
    player looks. The request sits on AMF's own open, so it runs over a mod's window that already has the input
    too (RaceMenu Atelier in character creation). Tested: "the menu opened - one more discovery pass queued", and
    the pass ran 8 ms later.
- **"Take those mods out of SkyUI's list" under MCM Unlocked** (Nexus 180186) or another 128-limit lift. AMF checked
  SkyUI's list in stock SkyUI's `_modConfigs` array, and those mods keep the list elsewhere. MCM Unlocked keeps it in
  its DLL; the "Barzing" layout uses `_MainMenu` plus `_modConfigsP1`, `P2` and so on. In a list with MCM Unlocked
  (Njordlinger runs it), the switch therefore took nothing out, and turning it back off retried in the background for
  ten minutes.
  - AMF now reads the list from whichever of the three keeps it. Under MCM Unlocked that is `MCMUnlocked.GetConfigBase`
    with the menu's name.
  - A list kept any other way is left alone, and the settings page says that the switch takes nothing out there.
  - A menu SkyUI never registered is not AMF's to take out or give back.
  - DevBench `amf.mcm op=skyuilist` reads the same three, and reports which one is in use (`layout`).
- **Switches changed in quick succession no longer leave menus out of SkyUI's list.** Under MCM Unlocked, AMF's checks
  of the list wait for the game's scripts to answer, so two passes over the list could overlap. A newer pass then read a
  menu as still in the list while an older pass was still taking it out, and the menu ended up out with nothing
  recording it.
  - Example: a loader switched on and the hide switch off within a second left 1 menu out; a burst of changes left 3.
  - Now only one pass runs at a time. It finishes only when every call it made has come back and been checked against
    SkyUI's list. A change made during a pass gets one more pass afterwards.
  - Tested under MCM Unlocked (Njordlinger Test, 83 menus): hide on, both loaders off, the two quick sequences, and a
    burst of 8 changes. All 83 menus were where the switches said each time, and the record of hidden menus was empty
    at the end.

## 2.0.8 - 2026-10-04 - working - the menu fits the image the game draws

### Fixed
- The menu and its tooltips ran off the right and bottom of the screen when the game draws a smaller image than its
  window - a borderless window with a lower render resolution scaled up (SSE Display Tweaks' upscaling, Auto
  Resolution's ratio, an upscaler). Soulsthat's report, 2560x1440: the menu clipped at the bottom and the right, the
  right side before the left, it slid while being resized, and a tooltip near the edge was cut off. The screen size
  came from the game window (ImGui's Win32 backend) while everything is drawn on the swap chain's image; it is now the
  image's size, and the log says once when the two differ.

### Tested (2026-10-04, SE 1.5.97, Njordlinger Test, Auto Resolution fRatio 0.5 -> 1600x900 drawn in a 3200x1800 window)
- Before (2.0.7): the menu's left edge sat near the screen's left and its right and bottom ran off the image.
- After: centred and whole, and the log read "the game window is 3200x1800 but draws a 1600x900 image".

## 2.0.7 - 2026-10-04 - working - MCM menus as AMF pages

The owner, 2026-10-04, asked for a tool like Dynamic Interface Patcher that turns MCM menus into AMF menus. He set it
experimental at first, then released it the same day once its tests had passed. Every mod with an MCM menu now also
gets that menu as a page here. AMF reads the mod's own files and scripts while the game runs; nothing of the mod's is
edited or shipped.

### Added
- **MCM Helper menus** (`Data\MCM\Config\<mod>\config.json`). Each one is an AMF entry, "<Mod> (MCM)", with its MCM pages
  as tabs:
  - Every control is drawn: toggles, precise sliders, steppers and dropdowns, text fields, colours, key binds, headers,
    text rows and action buttons. A control's group conditions grey out or hide it, as in MCM Helper's own menu.
  - A change goes through MCM Helper itself (its live store and `MCM\Settings\<mod>.ini`). Then the mod is told, through
    OnSettingChange and the action. OnConfigOpen / OnConfigClose are sent as the page opens and closes, so mods that
    apply their settings only when the menu closes (TrueHUD, True Directional Movement, Precision) apply them too.
  - Global variables and script properties are read and written live, and action buttons call the mod's own functions.
  - Labels come from the mod's own translation file, in the game's language.
- **SkyUI menus written only in a mod's script** (a quest script on `SKI_ConfigBase`, with no config.json). AMF makes the
  same calls on the mod's script that SkyUI's own menu makes, one at a time, and reads each page from the script itself:
  - toggles, sliders with the mod's own range, dropdowns (their lists fetched when opened), text rows, colours, text
    input and key binds;
  - Reset to default (right-click), and each option's help text on hover;
  - the mod's yes/no questions, shown as AMF popups.
- **Settings page.** Three switches, kept in `[MCM]`:
  - `bLoadMcmHelperConfigs` (default 1): MCM Helper menus.
  - `bLoadSkyUIScriptMenus` (default 1): SkyUI script menus.
  - `bHideInSkyUI` (default 0): takes the menus drawn here out of SkyUI's own MCM list. Only the menus AMF itself took
    out are ever put back. They are listed in `ApocryphaMenuFramework\McmHiddenInSkyUI.txt`, and SkyUI keeps its list
    in the save. `setstage SKI_ConfigManagerInstance 1` restores every menu in SkyUI.
  - **Turning the feature off gives every menu back to SkyUI.** It does not matter whether a switch is turned off on the
    page or in the INI with the game closed, or whether the save holds the menus hidden. A menu is written down before it
    is taken out. It leaves the list only once SkyUI's own registered-menu array shows it back; what SkyUI's call
    returns is not trusted, because in a large list those calls can come back empty.
- **DevBench `amf.mcm`.** For MCM Helper menus: list, controls, get, set, press, refresh, script and switch. For script
  menus, `op=skyui`: list, open, page, options, select, slider, menu, menuoptions, color, key, input, default, info,
  answer and close.
- **Languages.** 22 strings in all eleven languages. A mod's own labels stay in that mod's language.

### Tested in game (Njordlinger, 2026-10-04)
- **Found:** 33 MCM Helper mods and 51 script-only SkyUI menus.
- **Changes applied live, read back from the game or the mod's own state:**
  - Simple Offence Suppression's game settings, and FEC's death-camera time (`fPlayerDeathReloadTime`).
  - TrueHUD's loot popup.
  - Vivid Routines' globals.
  - FEC's reset button.
  - Precision's script properties.
  - SkyUI's own Unequip Armor setting.
  - Pick Up Radius: a toggle, a slider, a dropdown list, help text, and its "Load saved JSON preset?" question.
  - Equipment Manager: a text input, a key bind, Reset to default, and a colour, on all 13 of its pages.
- **On a stripped-down profile** (the owner, 2026-10-04: test the off switch on "a much more stripped down profile").
  The profile had SkyUI and MCM Helper, BTPS and Floating Subtitles (MCM Helper menus), and Hot Key Skill (a script
  menu). SkyUI's list was read from its own array every time:
  1. The hide switch took all 4 menus out of SkyUI's list.
  2. A save and reload kept them out.
  3. Switching both loaders off returned all 4, and AMF's own entries went with it.
  4. Hiding them again, saving, quitting, and turning both loaders off in the INI also worked: loading that save
     returned all 4 within seconds.
- **Controller:** the D-pad moved into a script menu's page, and A flipped a toggle through the mod's own script.
  The owner then nudged sliders with a real controller (BTPS): each press moved one shown digit (0.4 -> 0.5 -> 0.4
  -> 0.3), and MCM Helper saved each change.

### Limits
- Key binds take keyboard and mouse only; there is no controller button yet. A mod's own "this key is already used"
  warning does not fire.
- An MCM page that is a custom picture or SWF is not drawn. A control whose source type MCM Helper names but AMF does
  not read stays read-only, with a note.

## 2.0.6 - 2026-10-03 - working

C0kAdam, author of dMenu NG (Nexus 166751), 2026-10-03: under AMF the Font Awesome icon dMenu draws on each collapsing
header sat on top of the header's arrow, while under SKSE Menu Framework it did not. His guess - that AMF's separately
built icon faces were the cause - was not it: the icon glyph is drawn exactly where dMenu asks. What moved was the
header's own edge.

### Fixed
- A mod's collapsing headers and tree nodes now measure like they do under SKSE Menu Framework. A framed header in Dear
  ImGui 1.90.8 widens its frame to the left by half the window's padding, and AMF's windows carry a 34 px padding (room
  for the knotwork frame) where ImGui's default is 8 - so the frame's left edge, which a mod reads with
  GetItemRectMin(), sat 16 px left of the text instead of 3, 13 px further from the arrow. dMenu NG places its section
  icon a fixed distance from that edge, so the icon landed on the arrow. Every collapsing-header and tree-node function a
  mod calls now runs with ImGui's default padding (scaled the way AMF scales its own at high resolutions), and AMF's is
  put back straight after; a window whose padding the mod chose itself is left as it is. AMF's own pages, which do not
  go through those exports, look exactly as before. Measured offline at 1080p: edge 3 px left of the cursor, the arrow
  clear of the icon by 2 px (closed) and 0.9 px (open) - the same as stock ImGui.
- A mod passing a null label to InputText / InputTextMultiline / InputTextWithHint / InputTextEx is guarded again: the
  generator placed those four functions' null guard after their return, where it never ran.

### Tested (2026-10-03, SE 1.5.97, Njordlinger Test, dMenu NG 1.4 with WHEELER - Refined's dMenu settings)
- Under 2.0.5 the reporter's overlap reproduced; under 2.0.6 every section header on the WHEELER - dMenu page (Ammo Wheel:
  GENERAL, KEYBINDS, VISUAL PRESETS, LAYOUT, BEHAVIOR, TIME SLOW, SLOT CONTENT AND TEXT, CENTER PANEL, POPUP, PRIMITIVE
  SKIN, ...; Action Hotkeys Bridge Layout: SLOTS 1-20) shows its arrow clear of the icon, as under SKSE Menu Framework
  (the owner's screenshot, 3200x1800).

## 2.0.5 - 2026-10-03 - working

HadToRegister, Nexus, 2026-10-03: *"Updated to 2.04: it refuses to open with F1. It's set in the INI file as F1 ... The
settings in BOTH INI files try and cancel each other out; ... I couldn't open the menu with F1 until I deleted the INI file
in Data/SKSE/Plugins/ApocryphaMenuFramework"*.

### Fixed
- User.ini holds only what you changed. Since 2.0.3 the first change in the menu copied EVERY setting into
  SKSE\Plugins\ApocryphaMenuFramework\User.ini, and User.ini wins every key it holds - so from then on the shipped
  ApocryphaMenuFramework.ini, which still read like the settings file, was ignored for every key and an edit there did
  nothing. A setting is now written to User.ini only when it differs from the shipped value (or, for a key the shipped
  file does not carry, the built-in default), and on load a User.ini value equal to the shipped one is not counted as
  yours; an existing 2.0.3/2.0.4 User.ini is cleaned up the next time a setting is saved. The mod list's renames, order,
  favourites and separators are kept whole, as before, and a key a later version adds still comes from the shipped file.
- The menu key is one key. The menu opens on Controls > "Open and close the menu"; [Input] uToggleKey was a second copy
  that only the settings page's label and the reserved-key list read, so editing it - in either file - never changed the
  key that opens the menu, and the settings page's Rebind moved the label but not the key. uToggleKey now sets that
  binding (it wins when User.ini gives both and they differ; a Controls-page rebind alone is still honoured), and both
  rebind paths change the same key.
- uToggleKey accepts hex: "0x3B" read as 0 - no key at all - because the number parser stopped at the "x" and called
  that a success. Whole-number settings take decimal or 0x hex, and a value with anything left over is refused with a
  warning instead of half-read. A uToggleKey that is not a usable key (above 255, Escape, or a key another menu function
  holds) falls back to F1 with a warning naming the value; 0 is still "no key".
- With neither INI present the default controls were never put in place, so no key opened the menu.

### Added
- The log says, once per load, each setting your User.ini sets over a DIFFERENT shipped value
  ("settings: [Input] uToggleKey - your User.ini says 42 (Left Shift) over the shipped 59 (F1); User.ini wins"), how many
  User.ini values merely repeat the shipped ones, and where the menu key came from.
- amf.menu op=state reports menuKey {uToggleKey, bound, name, source}; amf.keybind op=state reports toggleKeySource.
  source is user, user-controls, shipped, default or fallback.
- The shipped INI's header says where your settings go: User.ini holds only what you changed and wins; edit User.ini (or
  the shipped file, for anything not changed in the menu); delete User.ini to go back to the defaults. User.ini's own
  header says it holds only what you changed.

### Tested (2026-10-03, SE 1.5.97, Njordlinger Test, through DevBench - the key presses injected into AMF's input path)
- A 2.0.4-era User.ini (172 lines, every key): 13 values counted as the player's and named in the log, 37 that only
  repeated the shipped values no longer pin them; menu key F1 from the shipped file; F1 opens the menu, F2 does not.
  After a theme change User.ini was rewritten with only the changed values and the list sections (67 lines).
- Controls-page rebind to F4: one key written to both lines (uToggleKey=62, sToggleMenu=1,62,0,-1); F4 opens, F1 not.
- User.ini uToggleKey 60 with sToggleMenu 61: the "two keys" warning, F2 wins and opens, F3 and F1 do not.
- A Controls rebind alone (sToggleMenu 61): F3, source user-controls, opens.
- No User.ini, shipped uToggleKey=0x3C: read as F2 (hex), and the hand edit to the shipped file takes effect.
- User.ini uToggleKey=300: refused with a warning, F1 instead, F1 opens.

## 2.0.4 - 2026-10-03 - working

### Fixed
- A mod's own window now gets the mouse, the keyboard and the controller. Mods that open a window of their own
  through the SKSE Menu Framework API (AddWindow) and ask for the player's input - RaceMenu Atelier's editor, FSMP,
  Equip or Unequip All - drew on screen with no cursor, and clicks, dragging, the wheel, their hotkeys and typing did
  nothing unless the framework menu happened to be open as well (mmmizuhara, 2026-10-03: RaceMenu Atelier "isn't
  working ... It works fine when I switch back to SKSE Menu Framework"). While such a window is open the cursor is
  drawn, the window takes the input and typing, and the game's own controls are held, exactly as with the framework
  menu; the mod's own hotkeys (Atelier's F4) still reach it first. The game is NOT paused for it - the pause setting
  stays the framework menu's. When the window closes the game has its input back on the next frame, and a key the
  game saw pressed before the window opened is still released to it, so nothing sticks. The menu key still opens the
  framework menu over such a window, and closing the menu leaves the window with the input. The framework menu's own
  command keys (favourite, tab steps, grab) no longer latch while only a mod's window is up.
- A passive overlay never takes the game's input. The SKSE Menu Framework header marks every window it creates as
  blocking by default, so the first 2.0.4 build would have taken all of the game's input for any always-on window
  drawn during play (found in testing before release, with StepUpOnto SKSE's NPC perf overlay). A mod's window now
  gets the input only when it is open, asks to block input AND draws a window that takes the mouse; a window drawn
  with ImGui's NoMouseInputs / NoInputs (an overlay the mouse passes through) is left on screen and the game keeps its
  controls. StepUpOnto's overlay does take the mouse, so while it is shown it holds the input, as a clickable overlay
  does. The log names the window that takes the input, and says once when an open, blocking window is passed over as
  an overlay.
- The console key still reaches the game while a mod's window holds the input. Opening RaceMenu from the console
  (showracemenu) left the console stuck open behind RaceMenu Atelier, because its ~ never got through (the owner's
  screenshot, 2026-10-03). The key the game's controls map to the console (~ by default) now passes to the game unless
  a text box is being typed in.
- RaceMenu Atelier's icons, and those of other SKSE Menu Framework mods, drew as "?". Those mods draw Font Awesome icons
  after asking for a Font Awesome face by name ("fa-solid-900", or PushSolid / PushRegular / PushBrands), and AMF had only
  its text face. AMF now ships Font Awesome Free 6.7.2's solid, regular and brands fonts
  (SKSE\Plugins\ApocryphaMenuFramework\icons\, SIL OFL 1.1, licence text beside them) and builds a face the first time a
  mod asks for it: the menu's text face with that style's icons merged in, so an icon and a label draw in one line, sized
  and placed on the text's baseline. A load order with no icon-using mod keeps the font atlas it had. Any other font
  name still gets the current font; a missing icon file is logged once and that face keeps the old behaviour. The
  font atlas is no longer rounded up to a power-of-two height, which keeps the icons' cost down and also trims the
  Japanese and Chinese atlas everyone already had (at 1800p: Japanese 2048x4096 -> 2048x2857, measured offline).
- Less work per frame for mods whose ImGui wrappers look up every function on every call (about a thousand lookups a
  frame for RaceMenu Atelier): a name already seen is now found with a shared lock and one hash, with no allocation and
  no scan.

### Added
- An SDK for mod authors (optional file "Apocrypha Menu Framework 2.0.4 - SDK"): AMF.h, the one header shared with the
  Oblivion Remastered framework, PreciseSlider.h and a complete SKSE example plugin with its build files, MIT. On
  Skyrim, AMF::UseFrameworkImGui() shares the framework's Dear ImGui through its cimgui exports (framework 1.7.2+); the
  example pins Dear ImGui 1.90.8 docking with the framework's own vcpkg overlay port, because the stock port gives
  1.90.2 and the header then refuses to share (it checks). It replaces the Custom Menu Art Preview file.
- amf.menu op=state reports consumerInput: whether a mod's window holds the input as the input hook sees it. The log
  says when a mod's window takes the input and hands it back. Each entry in consumerWindows also lists acceptsMouse and
  submitted - the top-level ImGui windows that mod drew on the last frame, with their flags (hex), noMouseInputs,
  noInputs, position and size.

### Tested (2026-10-03, SE 1.5.97, Njordlinger Test, RaceMenu Atelier 1.0.0)
- Under 2.0.3 the bug reproduced: Atelier registered ("registered with SKSE Menu Framework 3.70"), its window open and
  blocking, no cursor, no input. Under 2.0.4: the cursor is shown, a real mouse move moved it, F4 reached Atelier and
  swapped to the stock RaceMenu with the input handed back at once, the framework menu opened and closed over the
  panel, and closing RaceMenu gave the game its controls back. The owner saw the icons draw (his screenshot).
- The SDK example drew its page with the framework's Dear ImGui; a build against 1.90.2 was refused, as designed.

## 2.0.3 - 2026-10-02 - untested

### Re-uploaded 2026-10-03 (same number, the owner's exception: installer and documentation only)
- The installer's "New install or update?" question is removed again - not for this version (the owner,
  2026-10-03). The installer asks two questions and always installs the default ApocryphaMenuFramework.ini.
  Updating from 2.0.2 or older, the mod order and other settings reset once; from then on they are kept in
  User.ini, which no update replaces. Both DLLs are the 2.0.3 binaries recorded working, unchanged.

### Changed
- Your settings survive updates. Everything set in the menu - theme, text size, window positions, keys, and the mod list's order, separators, favourites and renames - is now saved to SKSE\Plugins\ApocryphaMenuFramework\User.ini, a file the download never contains, so an update cannot replace it (under Mod Organizer 2 it lands in overwrite). The shipped ApocryphaMenuFramework.ini now holds only the defaults and is never written. xLenax's request via the owner, 2026-10-02.

### Added
- An installer question for anyone updating from 2.0.2 or earlier, whose settings are still in the old INI: "Updating - keep my current settings" leaves that file where it is (Mod Organizer 2: choose Merge, not Replace), and AMF moves its values into User.ini the first time a setting changes.
- Layout presets on Framework Settings > Menu list: save the list's order, separators, favourites and names under a name, load it back, or delete it. Stored in SKSE\Plugins\ApocryphaMenuFramework\Presets\. amf.menu op=preset (save | load | delete | list) drives it for testing. All 11 languages.

## 2.0.2 - 2026-10-02 - working

### Changed
- The start-up curtain fills screens of any shape (the owner: the splash "doesnt fill the screen all the way on
  different resolutions"). A modlist may ship its splash art at several shapes in a `splash` folder beside
  `splash.png` - `splash-16x9.png`, `splash-21x9.png`, `splash-4x3.png` and so on. The curtain takes the one whose shape
  is closest to the game's screen (`splash.png` itself competes, so a list without the folder behaves as before) and
  names it in the log. A picture within 3% of the screen's shape now FILLS the screen edge to edge, trimming a hair;
  any other shape is still fitted inside, so lettering is never cut. An explicit `[Startup] sCurtainImage` is used
  as it is.
- The same folder feeds Njordlinger's MO2 plugin Splash Fit, which sizes MO2's own start-up splash to the monitor.

## 2.0.1 - 2026-10-01 - working

The first release of the 2.0 work on Nexus. 2.0.0 was tagged before the finalize gate ran and was never posted; its
fixes could not reuse the tagged number, so they are 2.0.1.

### Fixed (the finalize gate)
- The log ships at info: uLogLevel=2 in the compiled default and the shipped INI, whose comment now lists every level
  (rule 14, 2026-09-26). A report still asks for 0.
- The Text size slider is a precise slider - one 0.01 step per keyboard or D-pad press instead of ImGui's 1% of the range
  (rule 68; include/PreciseSlider.h, the same header as the Oblivion framework's).
- The package README names the version and lists what 2.0 changed.

## 2.0.0 - 2026-10-01 - working

Separators, grab-and-move and a window that fits its names.

The owner, 2026-10-02: "add to the next AMF update for Oblivion and Skyrim that we want to add a press Y on controller or
right click on mouse to create a separator and the separator should function just like a mod in the rename and reorder
function ... mods menus are children of the separator above them like in mo2"; "the mods can be collapsed into the
separator and ... a send to option for sending the selected mod to a separator"; "let's make the A to Z sorting ignore
mods in a separator ... you can also favorite separators ... favorite mods stay at the top"; "Go ahead and build the
Skyrim one."

### Added
- Separators in the side list. Y / right-click on a row: "New separator above" (named at once in the rename window,
  default "New separator"); on a mod: "Send to" > every separator, or "No separator" out of its group. A separator row
  shows a fold arrow and its name; A or a click folds / unfolds it (folded it shows how many menus it holds); its own
  menu has Expand/Collapse, favourite, Rename, New separator above, Move to the top and Delete separator (its menus join
  the separator above).
- Separators are entries in the saved order ([MenuOrder] sOrder, named ::sep:<n>, their names in [MenuAlias]); the
  mods after one, up to the next, are its mods - MO2's model. Folded ones in [MenuSeparators] sCollapsed. Typing a
  position on the Menu list page moves a separator WITH its mods; a mod typed next to a group's mod joins that group.
- Display order: favourite mods first, then favourite separators with their mods, then the loose mods, then the other
  separators. A-Z / Z-A sort the loose mods only. Searching shows a flat list of matches, folded groups included.
- Reset to alphabetical keeps the separators (empty, at the end) instead of deleting them.
- amf.menu op separator {action add | remove | send | collapse | favourite}; the state's order rows say which are
  separators (collapsed, children) and each mod's depth / hidden.
- Ten new strings in all eleven languages.

### Changed (the same test build, the owner watching the captures, 2026-10-02)
- The side pane fits its names ("make it so that the names are always fully visible ... by making the left pane auto
  adjust its width"): its width is the widest name shown (a separator with its arrow and count, a mod under one with
  its indent) plus the gutter, padding and scrollbar - no longer a flat 30%.
- The window grows instead of squeezing the page ("the right pane ... now you can't see hardly anything"): the page
  pane keeps at least 28 characters' width, and when both do not fit the window widens itself once, up to 98% of the
  screen. The width comes from the names alone, so it does not change with the mod picked ("I don't want it to
  dynamically change per mod selected").
- No title bar ("get rid of that top bar with the arrow ... fill it in with the Nordic knotwork ... we don't need a
  way to hide the menu aside from the hotkey or pressing the start button"): the knotwork frame runs round the whole
  window, the collapse arrow is gone.
- One centred window ("AMF is still locked to the center of the screen" and "make the system row version of AMF also
  not specific to the journal bounds"): the key-opened and the System-row window share one geometry - centred, not
  movable, one remembered size; the journal-panel fit is retired. The settings page's window section and the System
  row's help say so (new keys AMF_WindowSize / AMF_WindowSizeHelp / AMF_ResetSize / AMF_SizeDefault /
  AMF_SystemRowHelp2 in all eleven languages; the four retired keys removed).
- The separator's fold arrow is ImGui's own drawn arrow; a separator moved next to a loose or pinned mod goes to the
  head of the separators instead of swallowing the loose mods below it.

### Added (the same test build, after the owner's controller test, 2026-10-02)
- "Move to the top" on a mod goes to the top of its own separator ("moves it to the top of the separator that it's in.
  That way it's distinct from favoriting"); a loose mod goes to the head of the loose mods.
- "Reorder" in a mod's options: a small window with an up and a down arrow, one place per press, the window staying
  open so a mod can be walked several places. Stepping past a separator carries the mod into or out of that group; a
  folded group is stepped over. New keys AMF_Reorder / AMF_MoveUp / AMF_MoveDown.
- Grab and move ("pressing right stick will select the mod and then going and moving the stick up or down will move its
  position up or down. And this should be rebindable"): three new actions on the Controls page - "Pick up a mod to
  move it" (R3), "Move the picked-up mod up" / "down" (right stick up / down). R3 on a highlighted mod boxes it as held;
  each push of the stick moves it one place (the same step as Reorder), repeating every 0.12 s after a 0.35 s hold;
  R3 again, B, or moving the highlight off it puts it down. A move bound to a button or key steps once per press. New
  keys AMF_ActGrabMod / AMF_ActGrabUp / AMF_ActGrabDown / AMF_ActGrabModHelp / AMF_ActGrabMoveHelp; INI
  Bindings.sGrabMod / sGrabUp / sGrabDown.
- amf.menu op stick {which 0|1, x, y, hold frames}: a thumbstick pushed and let go through the same record a real
  stick event becomes, so the grab-and-move is driven headlessly.
- The Reorder and Send to submenus take the context menu's tight padding (the release captures showed the theme's
  frame padding as an empty band round the two arrows and the separator names).
- Numbered 2.0.0, not 1.10.0 (the owner: "Wrong version number.") - at x.9.9 the major rolls.

## 1.9.9 - 2026-09-28 - working

### Fixed
- **The controller triggers reach the menus.** L2 and R2 never got to ImGui: Skyrim reports them as button ids 0x9 and 0xA,
  not as XInput bits, and the framework's gamepad table had no entry for either. A page could not see them, so a mod's
  "press a button to bind" window never caught L2 (the owner, 2026-09-28, binding Show Player In Inventory's rotate key).
  They now arrive as ImGui's GamepadL2 / GamepadR2. The framework's own actions do not use the triggers, so nothing else
  changes.

## 1.9.8 - 2026-09-25 - working

### Added
- **Four new themes: Vel'dun, Oathvein, Norden and Norden - Black** (the owner, 2026-09-25: *"Download velduun ui, oathvein ui, and make amf themes
  based on them"*). Pick any of them from the Theme list on the Framework Settings page. Each brings its own frame, background
  grain and toggle-switch shape along with its colours:
  - **Vel'dun** - bone-coloured double lines with cut corners and small diamonds on a warm dark-brown panel, tan headings
    and selection. The colours are Vel'dun UI's own ImGui style (Nithog, Nexus 176230).
  - **Oathvein** - a thin grey line with crossed scratch marks at the corners on charcoal, and a blood-red selection.
    Colours sampled from Oathvein UI (Nithog, Nexus 160916).
  - **Norden** and **Norden - Black** (the owner, same day: *"Add a norden theme and norden black theme as well"*) - a
    thin slate line with bright corner ticks, silver-grey highlights and Norden UI's `#333333` panel (Nithog, Nexus 166086);
    Norden - Black takes the panel to black by the same rule as our Norden UI - Black recolour and shares the Norden art.
  The art was drawn for AMF (`tools/make-theme-art.py` regenerates it); no files from any of the three UIs are included,
  and none of them is required.
- **A theme INI can now be a complete theme.** Besides `sName`, `sBackground`, `sFrame` and `fBorderThickness`, a file in
  `SKSE/Plugins/ApocryphaMenuFramework/themes/` may set `sBorder`, `sText`, `sTextDim`, `sAccent`, `bKnotwork`, and its own
  art - `sSkinFrame`, `uSkinFrameCorner`, `sSkinBackground`, `sSkinPlates` - which draws whenever that theme is picked.
  Colours take `#RRGGBBAA` as well as `#RRGGBB`, so a panel can be slightly see-through. A player's own `[Skin]` art, when
  switched on, still takes precedence over a theme's.

### Fixed
- The `amf.process op=skin` test reply wrote art paths with a bare backslash (`Data\SKSE/...`), so it was not valid JSON
  whenever any art was loaded. Paths and errors are escaped now.

### Verified
- SE 1.5.97 (Njordlinger Test) and 1.7.104 (Line-17): each of the four themes picked in game loads its frame, background
  and toggle plate and draws them on the Framework Settings page; switching back to Skyrim returns the knotwork.

## 1.9.7 - 2026-09-22 - untested

### Added
- **Pause the game while the menu is open** (the owner, 2026-09-22: *"next amf update gets a toggle in settings to stop
  time while menu is active"*). A toggle on the Framework Settings page, `[Menu] bPauseGame` in the INI, off by default.
  On, the framework holds one count on the game's pause counter (`UI::numPausesGame`) while its window is open - the
  same thing the game's own pausing menus do - and gives it back when the window closes or the toggle is turned off.
  The count is only touched on the main thread and never more than once, so it cannot leave the game paused or take a
  count another menu holds. Translated in all eleven languages.

### Fixed
- **Crash at data load beside Theo's Render Pipeline** (reported by Soporatus, 2026-09-23). The SKSE Menu Framework alias
  patched the import table of every DLL anywhere under `SKSE\Plugins`, including the NVIDIA Streamline runtime Theo's
  Render Pipeline ships in its own subfolder, and overwrote import entries another plugin had already hooked. The alias
  now patches only (a) DLLs directly in `SKSE\Plugins` - SKSE never loads a subfolder's DLLs as plugins - (b) that name
  the framework somewhere in their image, which every consumer must because the name is what it looks the framework up
  by, and (c) only import entries that still point at kernel32 / kernelbase: an entry another plugin redirected is left
  alone. The log's alias line now counts the plugins skipped as non-consumers and the entries left as another plugin's hook.

## 1.9.6 - 2026-09-19 - untested

### Fixed
- **The right-click / Y menu on a mod row fits its three options** (the owner, 2026-09-21: *"fix the empty space ...
  and make the outer bounds of the box smaller so it fits around the 3 options"*). Every window takes the theme's
  padding - the knotwork corner plus 8 px, so the frame art has room - and the context menu, which has no frame art,
  inherited it as an empty band around "Add to favourites", "Rename..." and "Move to the top". It now uses a tight
  padding of its own (about a third of the text height).
- **A text box could be cancelled the moment it opened, so nothing could be deleted or typed** (the owner, testing
  1.9.6 on 2026-09-21: *"I'm clicking the text box and it's not letting me delete the word anymore ... It did
  actually require me to press escape just now, and it started typing again"*). The log had 62 text-field deaths,
  each with "keys this frame: Escape(d)": the menu believed Escape was still HELD. Escape closes this menu, and its
  release arrives after the menu is hidden, so the release was never seen. A held key repeats, and Escape is a text
  box's cancel, so every box opened afterwards was cancelled about 40 ms later. Pressing Escape again delivered a
  release, which is why it came back. Opening the menu now starts with no key held. Each frame, any key the menu
  still thinks is down but the keyboard reports up is released, and logged, whatever path lost its release. A key
  typed into a text box also no longer fires a framework command (F favourites a mod, Page Up / Page Down switch tabs).
- **The F1 window moves again, by its top bar** (the owner, 2026-09-21: *"the F1 called AMF does not, as it is fixed
  in position, which should still be movable if they grab it by the top"*). It opens where it was last left (the
  screen centre the first time), moves by its title bar only like the System-row window, and still grows both sides
  evenly when an edge is dragged. The centre it grows about is now wherever the window has been put.

### Fixed
- **The window moves by its top bar and nothing else.** The owner, 2026-09-19: *"We need to make it so you can't drag
  AMF by anything but the top bar of the entire menu interface because I can be pointing my cursor at the item in the
  preview pane and instead move the AMF menu around or move the menu around while moving the item and rotating it."*
  Dear ImGui moves a window when its BODY is dragged, and a mod's page is all body, so a drag meant for a slider, a 3D
  preview or a row of items moved this window instead - the log shows three window positions saved in ten seconds
  while he was trying to turn an item.

  It also cost every page its left-click: the move takes the active id on the frame the button goes down, and while
  something is active every `IsItemHovered()` in that frame answers false - so a plain click on a row did nothing at
  all. One flag, `ConfigWindowsMoveFromTitleBarOnly`, fixes both, for this window and for every consumer's.

### Fixed
- **Typing could still be dead after 1.9.5, on a load order where another mod has driven the engine's text-entry
  count negative.** phbd01 reported the same "I have to press Escape first" shape again after 1.9.5 shipped.
  `ControlMap::AllowTextInput` moves a COUNTER, and the engine only produces characters while that counter is above
  zero - so 1.9.5's single `+1` was not enough if some other mod had called it false more often than true and left the
  count at, say, -2. The framework now READS the count and raises it until it is actually positive (bounded, and it
  remembers how many raises it made so exactly that many are undone), and if it still cannot be made positive it says
  so in the log with the number - which names the mod responsible instead of leaving the fault looking like ours.

  **And it stays working while the box is focused** (phbd01 again, 2026-09-21: *"it is fixed at first now, but it
  returns after some time, I can click but not type, I have to press escape again to type"*). The count was only
  checked when a text field took focus, so if another mod lowered it while the field was still focused, typing died
  until Escape dropped the focus and the next click raised it again. The framework now checks the count every frame
  while a field holds focus and tops it back up, logging the first drop with the number. When the field lets go it
  lowers the count only by what it added and never below zero, so a mod that reset the count is not left negative.

### Added
- **The bumpers walk the tabs** (the owner, 2026-09-19: *"bumpers navigate tabs, dpad doesnt"*). L1 goes to the
  previous tab and R1 to the next, on a mod's own tab bar if it drew one and otherwise on the framework's page bar -
  the innermost bar takes the press. The D-pad still never steps a tab: moving the highlight onto one and activating
  it stays the other way, which is the standing rule it has followed since 1.9.0. Both are bindable like everything
  else, and default to Page Up / Page Down on the keyboard side.
- **"Open a mod's options" is now a bindable action** rather than Y being a hard-wired special case. It still defaults
  to Y, and still opens the same menu a right-click opens; it can now be moved from the Controls page like every other
  control. It shares Y with the on-screen keyboard's backspace, which the exclusivity rule allows because the
  keyboard's own actions only act while the keyboard is open.
- **"Favourite the highlighted mod"**, for players who would rather not go through the menu at all. L3 on a controller,
  F on the keyboard.

### Changed
- Actions that are not a navigation key - the two tab steps, the context menu and the favourite command - are raised
  as flags by the input hook and consumed once per frame by the renderer, rather than being read as ImGui keys. A
  single press can therefore never fire on more than one row of the list.

## 1.9.5 - 2026-09-19 - untested

### Fixed
- **Text boxes took no typing at all.** phbd01 (2026-09-19, Nexus): *"when I click on the search bar and try to type,
  nothing happens. I have to press 'Escape' first before I can type. This gets really frustrating... Never happened in
  og menu."* Skyrim only turns a WM_CHAR into an `RE::CharEvent` while `ControlMap`'s text-entry count is above zero,
  and this framework has no WndProc hook - a CharEvent is the ONLY way a typed letter reaches ImGui here. The
  framework never called `ControlMap::AllowTextInput`, so whether typing worked at all depended on some other mod
  happening to have raised that count and left it raised. It now raises the engine's text entry for exactly as long as
  an ImGui text field wants the keyboard, and releases it when the field or the menu closes. (SKSE Menu Framework takes
  WM_CHAR through the Win32 backend instead, which is why it never showed the fault.)
- **Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V and Ctrl+Z did nothing in a text box.** Only navigation and editing keys were mapped
  from scan codes to ImGui keys; letters and digits were not, so ImGui never saw the shortcut half of a chord.

### Added
- **Menu favourites.** Right-click a mod in the list - or press Y on a controller, which opens the same menu - to add
  it to your favourites, rename it, or move it to the top. A favourite sits at the top of the list with a filled white
  box beside its name, and favourites keep the order you added them in, so a new one lands after the last. They are
  kept in the framework's own INI under `[MenuFavourites]`, alongside the rename and order settings that were already
  there, and they work with a custom order and with either sort.
- **Sorting the mod list.** Two switches on the "Mods" row itself: A-Z and Z-A. They are alternatives, and turning both
  off gives back whatever order the list was arranged in by hand. Favourites stay pinned at the top under either.
- **An instruction manual on the Help page**, in tabs (the owner: *"the help row should have tabs: controls, features,
  readme, and others as you see fit"*): Controls, Features, Readme and Troubleshooting. The tab bar is submitted the
  same way a mod's own page bar is, so the D-pad walks it the same way.
- **The thumbsticks, for a consumer that wants them.** `AMF_SetSticksCaptured(bool)` lets a mod's page take both
  sticks away from navigation while it is handling something, and `AMF_GetStick(which, x, y, clicked, live)` reads
  them apart - the framework otherwise collapses both onto one set of navigation axes and decides which is in charge.
  L3 and R3 now also reach ImGui as `ImGuiKey_GamepadL3` / `ImGuiKey_GamepadR3`; they were not mapped at all before.
  The capture is released by the framework itself when its menu closes, so a mod that forgets cannot wedge the pad.
  (Item Explorer's 3D preview is the first consumer: R3 on a row takes hold of the item.)

## 1.9.4 - 2026-09-18 - untested

### Fixed
- **1.9.3 stopped the game from starting.** It logged the Address Library report a second time by calling the guard's
  `Check()` again after `SKSE::Init`; with that build the game died about a second after launch, before SKSE wrote a
  line of its own log, and no message box appeared (the owner, 2026-09-18: *"1.9.2 boots but not 1.9.3"*, *"test
  profile runs with amf 1.9.1 but not 1.9.3"*). The guard now REMEMBERS the report it made before `SKSE::Init`, and
  the framework logs that copy: the line a bug report needs is still there, and nothing is asked of the system twice.
  **1.9.4 replaces 1.9.3 - do not run 1.9.3.**

## 1.9.3 - 2026-09-18 - untested

### Fixed
- **The Address Library line reaches the log.** 1.9.2 moved the guard before SKSE::Init, where this framework's own
  logger does not exist yet (it starts after Init on purpose, so the 1.7 line's CommonLibSSE-NG logger cannot replace
  it), so the guard's line was written to nothing - a bug report from AMF carried no Address Library line at all. The
  same report is now logged again as soon as the log is up: game version, the file the library wants, where it looked,
  and whether it is there. The message box for a missing file is unchanged.

## 1.9.2 - 2026-09-18 - untested

### Fixed
- **The Address Library guard now runs before SKSE::Init.** CommonLibSSE-NG's Init opens the Address Library itself, so the guard added for a missing file sat after the very call that fails on it and never ran; oproso's log (Perfected Wheeler 1.3.2, 2026-09-18) showed the banner, then CommonLib's bare 'failed to open address library file', and no [AddressLibrary] line. The check is now the first thing after the logger, so a missing file is named - game version, file, folder - and the plugin loads inert.

## 1.9.1 - 2026-09-18 - working

### Fixed
- **The hang watchdog no longer terminates a game that is merely out of focus** (the owner, 2026-09-18, twice: *"the game died again"* with no crash log - the framework's own log said `watchdog: terminating the process - no frame for 120s`). Skyrim presents no frames while its window is not in the foreground, and the watchdog read that as a hang. It now stands down while the game window is not the foreground window or is minimised, logs the stand-down and the return, and gives the game the full window again once focus is back.

## 1.9.0 - 2026-09-18 - working

### Changed
- **A sideways D-pad or stick press inside a page never changes the tab** (the owner, 2026-09-18: *"i want the only way for the dpad to switch tabs in our mods is to select said tab and activate it, im tired of switching tabs by accident"*). The 1.8.8 rule (move to a widget first, step a tab only at the edge) and the 1.8.7 one before it (left steps back through tabs) are both gone: a press moves between widgets, a left press with nothing to move to returns to the mod list, and a tab - page tabs and a mod's declared inner tabs alike - changes only when the highlight is on the tab and A is pressed. AMF_DeclareInnerTabs still exists for compatibility but never asks a page to change tab.

### Fixed
- **Back, Clear and Y (backspace) on the on-screen keyboard now delete** (the owner, 2026-09-18: *"the back button on the keyboard and the clear button and the y controller button dont do anything"*). The framework runs ImGui with event trickling off, so a synthetic key's press and release queued together were applied in one frame and the key was never seen down; the release is now queued a frame after the press, and Clear selects the whole text through the edit state before deleting it.
- **The controller is the keyboard's alone while it is open** (the owner, 2026-09-18: *"backspace didnt work, and neither did shift, the dpad presses leak out of the keyboard, lock the dpad to the keyboard until they exit or switch to mouse"*). ImGui's Win32 backend was polling XInput every frame and feeding the D-pad and face buttons into ImGui a second time, past the framework's own translation - so with a real pad the highlight under the keyboard kept moving, and the backend's X and Y events undid the keyboard's shift and backspace. A spliced test press never reached XInput, which is why the 1.8.9 proof passed. The backend's polling is compiled out (IMGUI_IMPL_WIN32_DISABLE_GAMEPAD in the imgui port); every gamepad event now comes from the game's input queue alone, and the keyboard also swallows the right stick while it is open.
- **The on-screen keyboard now opens on the framework's own text boxes** - the mod-list Search bar, the alias and position fields on the Settings page (the owner, 2026-09-18: *"the keyboard appears while in item explorer but not when using amfs own search bar"*). The hook that tells the keyboard "the highlight is on a text box" is injected into the C-API wrappers a mod's page draws through, and the framework's own boxes are drawn with ImGui directly, so they never registered; each of them now notes itself after it is drawn.

## 1.8.9 - 2026-09-18 - working

### Added
- **An on-screen keyboard for controller players, built into the framework so every mod's search box gets it** (the owner, 2026-09-18: *"add this in-game keyboard to AMF ... toggleable in the settings page ... if this keyboard would be effective on a search bar inside of another mod, even more so"*; on where it sits: *"separate from the AMF framework menu bounds and just bound to the screen and set at the bottom of the player's screen"*; and *"whenever they press circle, it teleports the controller nav box back to the search bar"*). Highlight any text box on a mod's page with the D-pad and press A: the box starts taking input and a key grid appears across the bottom of the screen - bound to the screen, not to the window, so it never covers the page. The D-pad and left stick walk the keys, A types one, B puts the highlight back on the box and gives the pad back to the page, X is shift, Y is backspace; Space, Back, Clear, Shift and Done sit on the bottom row. It works in every page because the framework exports the text-field calls themselves: the four `igInputText*` exports now report the item they drew, so the keyboard knows which highlighted item is a text box without any mod declaring anything. Off by `bOnScreenKeyboard=0` or the toggle on the Settings page. Two exports for a mod that wants to summon it itself: `AMF_ShowKeyboard()` and `AMF_HideKeyboard()`. `amf.menu op=state` carries a `keyboard` block (open, target, cursor, the text so far) so a driven test can read it.
- **`AMF_DrawThemeFrame(drawList, x0, y0, x1, y1)`** - the active theme's frame (the Skyrim theme's Nordic knotwork, or a UI author's frame art) drawn around a consumer's own box, just outside the rect as around the window. Item Explorer's floating 3D preview is the first caller (the owner: *"apply the same Nordic knotwork that's in the Skyrim theme for AMF to this texture rendering frame"*). Returns false under a theme without a frame so the caller can draw a plain line.
- The framework now carries the Address Library guard every mod of ours ships: if the Address Library file for the game is missing, it loads inert with a message naming the file instead of crashing.

## 1.8.8 - 2026-09-16 - untested

### Changed
- **A sideways press inside a mod's page moves to the next widget first, and steps a tab only at the edge** (the owner, 2026-09-16, in Item Explorer: *"dpad right sends you to the favorites tab instead of the add item box"*). Item Explorer's Browse page lays its widgets side by side - the count slider, then the "Search every plugin" toggle on the same line - and a D-pad right meant to reach the next one was taken by the framework as "next tab" before ImGui had a chance to move the cursor. The press is now noted on the frame it happens and decided one frame later, when ImGui reports through `NavJustMovedToId` whether the cursor landed on another widget: if it did, nothing else happens; if it did not, the tab steps exactly as before (a page's own inner tabs first, then the framework's), and a left press with nothing to move to and no tab to step back through still returns to the mod list. Keyboard arrows and the stick follow the same rule. A driven `amf.menu op=nav` press moves no ImGui cursor, so it always steps, and the driving tool's proof of this path is unchanged.

### Fixed
- **Text the GAME supplies draws in a Japanese, Korean, Chinese or Russian game** (2026-09-17, the same class as littlefot's Wheeler report on that mod's Nexus page, checked across every mod of ours that owns a font atlas). The atlas was built from the default Latin set plus every character in the translation files - which is right for the framework's own text but says nothing about an item or spell name that Item Explorer or another page reads out of the game: a Japanese game's kanji were in no file the builder had read, and drew as `?`. The atlas now also holds Dear ImGui's built-in ranges for the script of the framework's language AND of the game's own sLanguage (Japanese, Korean, the 2500 common simplified Chinese characters, Cyrillic, Thai, Vietnamese), and the system face merged in for the missing glyphs is chosen by whichever of the two needs it - so a Japanese game with the framework's pages set to English still gets kana and kanji. Nothing changes for an English game.
- The driving tool gained `op=font`: the languages the last atlas was built for, its glyph count and size, and one probe glyph per script (kana, hangul, hanzi, Cyrillic), so a language switch is proved by reading the atlas.

## 1.8.7 - 2026-09-16 - untested

### Added
- **The startup curtain shows the MODLIST'S OWN splash art, with no configuration** (the owner's idea, 2026-09-16: *"can we make the curtain for AMF display the Njordlinger splash art?"*, then *"i'd like the curtain display splash art path to be usable by other mod list authors that use wabbajack, so it just always reads whatever the splash art is for that mod list"*). Mod Organizer keeps `splash.png` in the root of its instance and a Wabbajack list ships one as its branding, so the curtain walks up from the game's working folder - a Stock Game or Game Root sits inside the instance - and uses the first one it finds. A list author gets their own art on the curtain by doing nothing at all. The walk is bounded to a few levels so it can never wander off across the disk, and the file it chose is named in the log, so a surprising picture is traceable rather than mysterious. `[Startup] sCurtainImage` overrides it with a path relative to Data, and `none` forces plain black. The picture is fitted inside the screen with its shape kept and centred on black - fitted rather than cropped, because a splash is lettering and composition that filling the screen would cut - and it fades out on the curtain's own alpha. Decoding goes through WIC, which Windows already provides, so the framework gains no new dependency for one ornament; PNG, JPEG and BMP all work. Everything about it fails soft: no splash, a bad path or a device that will not take the texture leaves the curtain black and the game starting normally.
- **A page can declare its OWN tab bar to the framework, so the D-pad walks it too** (the owner, 2026-09-16: *"the nav box behaves properly on the main tabs of the mod page for Wheeler, but when going to the other tabs within those tabs, it does not"*, and on which fix to build: *"whichever solution will work on our end and not require patching by other authors but we can build it into mods we make though"*). The framework can only measure the tab bar IT submits, so a consumer drawing its own `BeginTabBar` inside a page is invisible to the nav decision and the D-pad does nothing there. It cannot be fixed by guessing from outside, so the page says what it has: `AMF_DeclareInnerTabs(count, current)`, called once per frame from its render function, returns the tab the D-pad asked for or -1. Inner tabs are stepped FIRST - before the framework's own page tabs and before leaving the pane - because they are the innermost thing the press could mean. **Opt-in by construction**: a mod that never resolves the symbol declares nothing, leaves the counts at zero and behaves exactly as today, so this goes into our own mods without asking any other author to patch theirs. A declaration is retired the moment a page stops renewing it, so stale numbers cannot steer navigation.

### Fixed
- **D-pad left walks a mod's tabs instead of dropping out to the mod list** (the owner, 2026-09-16: *"d-pad left, making it go all the way back to the left pane instead of just scrolling the tabs ... it should only go back to the far left pane when you're already done scrolling left and there's nothing left to scroll"*). Left was never the broken half. Stepping RIGHT required `g_tabBarHasNav` - ImGui's navigation cursor sitting literally on the tab bar - and opening a mod leaves focus in the page below it, so right never advanced the tab and the index stayed at 0. Left then had nothing to step back through and fell to its last branch, which is exactly "at the first tab, go back to the list". Right no longer asks where the focus happens to be, so the index moves, and left steps back through the tabs until the first one. Measured rather than reasoned: `amf.menu op=state` publishes page, pageIndex and pageCount, and two `op=nav dir=right` presses on Wheeler left it at index 0 of 3. `!editing` still guards both, so pushing left or right inside a slider adjusts the value rather than changing tab.
- **The curtain no longer flashes white before it goes black** (the owner, 2026-09-16: *"the curtain that's built into AMF has a white shuttering effect at the very beginning of its startup"*). A filled rectangle is not a special case in Dear ImGui - it is geometry sampling the font atlas's white pixel - so with no atlas texture bound it draws untextured, which on screen is WHITE. At startup that is precisely the state for the first frames: the atlas is created on the first frame that needs it, and this framework queues a deliberate rebuild for its own fonts on top of that, invalidating the device objects again. The thing whose whole job is to show black was therefore the thing flashing white. The curtain now draws nothing until the atlas has a texture. Skipping those frames is the right answer rather than substituting something else: an uncovered frame or two of the game's own startup is what the curtain would have hidden anyway, and the fade cannot begin while frames are that slow.

- **The startup curtain no longer freezes half-faded on its way out** (the owner, 2026-09-16: *"the curtain that's built into AMF has a slight stutter between when it's supposed to close and when it shows the menu where it's partially transparent and it holds that for about a second"*). The fade was started the moment the main menu registered as open, and timed by the wall clock. But the menu registers while its movie is still loading, so the frames right after it are enormous - one can be most of a second on its own - and a wall-clock fade drawn across a frame that long leaves the curtain at whatever alpha it had reached and holds it there until the next frame arrives. That is the stutter: black, then abruptly half-transparent, held, then gone. The fade now begins only once the game is actually presenting frames fast enough to draw one (six consecutive frames under 40 ms), and the curtain stays FULLY opaque until then - solid black is what the curtain is for, half-black frozen for a second is not. Waiting for those frames is itself capped at 3 s, so a game that never settles still gets its screen back, and if a long frame lands mid-fade the curtain is cut outright rather than left holding a half-faded screen.

## 1.8.6 - 2026-09-15 - untested

### Fixed
- the startup curtain lifted on its timeout instead of on the main menu. Observed in game on the owner's list the same evening 1.8.5 shipped: the curtain covered the screen at 23:39:19.861 and released itself at 23:39:49.888 with "the main menu was never seen within the timeout", while AMF was still registering mod pages at 23:40:14 - so the main menu simply had not arrived yet and the curtain uncovered the tail of startup, which is the one thing it exists to hide. Two wrong explanations were checked and discarded first: a main-menu replacer (the mod named "Open Animation - No Main Menu GUI" ships only an Open Animation Replacer config and is unrelated) and a detection failure (DevBench's amf.mainmenu confirmed the menu genuinely was not open). The cause was simply that 30 seconds is not long enough for a heavy load order.
- the timeout is now `uTimeoutSeconds` under `[Startup]`, defaulting to 120, so a slow list can be given more room without a rebuild.

### Added
- a second way out, so that a longer timeout cannot strand anyone: the curtain also lifts the moment the player is in a loaded world (`PlayerCharacter::GetSingleton()` with a non-null `parentCell`, the same idiom Dragon's Eye Minimap uses). Covering actual gameplay would be far worse than showing a little of startup, and `parentCell` stays null until a save or new game really loads, so it cannot fire early.

## 1.8.5 - 2026-09-15 - working

### Added
- a startup curtain: the screen is held black from the first frame the framework draws until the game's main menu is up, so the logo frames and the half-drawn menu behind them are never shown. The owner asked for this as its own mod ("the mod that makes the screen black until the menu loads") and then, the same day, for it to live here instead ("build it into amf with a toggle in the settings page of amf") - which is the right place: the framework already owns the present hook and its surface draws every frame whether or not its own menu is up, so a separate plugin would have had to stand up a second D3D hook and race this one for the same frame.
- a toggle for it on the Framework Settings page, and `bBlackCurtain` under a new `[Startup]` section of the INI, shipped on by default. Turning the toggle off while the curtain is still up lifts it immediately rather than at the next launch - the one control that fixes a stuck curtain must not itself be hidden behind the curtain.
- Failing safe is the whole design of `Curtain.cpp`: the curtain lifts on the main menu appearing, on a 30-second hard timeout, or on the setting being turned off, and never returns for the life of the process. A curtain that does not lift is indistinguishable from a game that will not start, and the player has no way to argue with it. `RE::UI` is null-checked because this runs long before that singleton exists.

## 1.8.4 - 2026-09-15 - working

### Fixed
- the System-menu row drew but did nothing under Dragonborn UI (borokoshow's report): the press listener was skipped whenever the row was already in the list, and the row was identified by a remembered index that SystemPage.SetShowMod moves when it splices a Mod Manager row in at index 2. The listener is now attached on every journal open whether the row is found or pushed, and a press is matched by the entry's own text instead of its index.

### Changed
- the controller navigation box is bright blue in every theme instead of the old gold (the owner: 'the next amf version should have a bright blue controller nav box instead of the old yellow one'), so the focused item stands out from the gold selection wash rather than blending into it.

### Added
- DevBench op amf.menu systemrow: the live journal category list - every row's text in order, whether our press listener is attached, and the index the row was added at - so a 'the row is in the wrong place and does nothing' report is answered by reading the menu instead of reasoning about someone else's SWF.

## 1.8.3 - 2026-09-14 - working

### Added
- AMF_SetPageVisible(mod, page, visible): a mod can hide or show one of its registered pages. A hidden page is left out of the mod's tabs (a mod with one visible page shows it without a tab bar) and comes back exactly as it was; registration is untouched. Idempotent, so a consumer may call it every frame; false when the (mod, page) pair is not registered. DevBench amf.menu state lists each mod's hiddenPages. For Wheeler - Refined's Advanced settings switch (the owner: 'the advanced settings toggle doesnt hide the advanced settings tabs').

## 1.8.1 - 2026-09-13 - untested

### Fixed
- The System-row window's remembered position followed the player from one journal art to another: dragged under Quest Journal Overhaul's redesign, it then opened at that spot in the game's own journal. The position is now remembered per journal art (`Window.sNestedArt`): under any other art it is ignored and the journal is measured afresh, and the next drag saves a position for that art. The key-opened window is unchanged.

## 1.8.0 - 2026-09-13 - untested

### Fixed
- Quest Journal Overhaul - Entire Journal Redesigned: the SKSE MENUS row worked, but the window opened from it was sized to the vanilla panel rectangle, which that art keeps where it always was while drawing its System page as a button column left of a divider and a content pane right of it - so the window sat across the buttons. The pane is now measured from that art's own divider, header rule and page rectangle, and a stored window position that starts left of the divider (dragged under other art) is set aside for the measured pane. Vanilla and the other replacers are unchanged.

### Added
- `amf.menu bounds path=<clip>` measures any clip of the open journal as fractions of the screen, so the next art replacer is measured rather than guessed at.

## 1.7.9 - 2026-09-13 - untested

### Fixed
- Crash thirteen seconds into the game (Haron's crash log, 1.7.6, SE 1.5.97): the input hook wrote the pruned event list back through the caller's pointer, and with seven other input hooks in the chain that pointer can point into another mod's read-only memory (a constant empty list). The write-back now happens only when the head actually changed and the slot is writable memory; otherwise the pruned list is passed on through the framework's own array and the caller's memory is left alone. The 1.7.5 stale-event fix (the search bar dying after an erase) is kept for the ordinary case.

### Added
- `amf.menu state` reports ImGui's navigation cursor (`navId`, `navWindow`) so a "the cursor jumps to the top of the list" report can be measured from a driving script.

## 1.7.8 - 2026-09-13 - untested

### Fixed
- Dragging a corner of the key-opened window scaled the text along with the window and let it grow past the screen, jumping as it went. A corner drag now keeps the window's shape and grows only its width and height, the window cannot be larger than the game screen, and the size is applied through ImGui's own resize so nothing jumps. Edge drags still grow both sides about the centre.

## 1.7.7 - 2026-09-13 - untested

### Changed
- The key-opened window is fixed to the centre of the screen. Dragging an edge grows the window on both sides so the centre never moves; dragging a corner scales the whole window, text included, with its shape kept. The window opened from the game's System menu row is untouched and keeps its own placement and resizing. The scale is remembered with the window's other geometry.

- Every remaining Dear ImGui default colour is now on the theme's palette (text selection, resize grips, tables, plots, modal dim). The menu list selection and hover use the theme's accent, as the tabs already did.

### Added
- AMF_OpenMenu(modName) and AMF_CloseMenu exports, so a mod's own settings key can open this menu on that mod's page (Wheeler uses it from 1.0.10).
- The amf.menu driving tool reports the style colours the menu draws with (op style), for the report that the mod list selection looks blue.

## 1.7.6 - 2026-09-13 - untested

### Changed
- The reserved-key list other mods ask for (SMF_GetReservedKeyCodes) now reports the menu key the player CURRENTLY has set, first, followed by the menu's navigation keys. It reported a fixed list with F1 hard-coded, so a mod checking it could still take the menu key from a player who had moved the menu to another key. F1 is reserved only while it is the menu key.

## 1.7.5 - 2026-09-12 - untested

### Fixed
- A key released inside the menu now reaches the game only if the game saw its press. An unconditional pass-through line overwrote that decision, so every release reached the game - a shout key pressed inside the menu could complete as a shout on its release.
- The menu's input hook now writes an EMPTY list head back to the game when it has consumed every event. It did not, so the engine handed the just-consumed node back on every following dispatch that carried no new input, and the last key event before the hands left the keyboard was re-processed every frame: a released Backspace stayed held and deleted each character as it was typed, a character replayed dozens of times, and every later press was swallowed. This is the mechanism behind the search box going dead after erasing (xLenax, 1.7.4) and the earlier reorder-field report.

### Added
- The log names any consumer input callback that claims a press while the menu is open, since from the player's side that is indistinguishable from the menu freezing. The amf.menu driving tool gained inject, injectchar, type and key operations and reports the search box, text-input and modifier state, so keyboard paths are provable without a person at the keyboard.

## 1.7.4 - 2026-09-12 - working

### Fixed
- Input callbacks registered by other mods are now dispatched while the framework menu is CLOSED, not only while it is open - which is how the reference framework behaves. With the menu down an event passes through to the game unless a mod's callback explicitly claims it, so ordinary gameplay keys are untouched.
- Reordering the mod list no longer stops responding after a few moves. The move was applied in the middle of the frame that was still drawing the list, so every row after it was laid out against a sequence that no longer matched. The move is now applied once, after the table closes.
- The framework now reports the SKSE Menu Framework interface version it impersonates (3.7) instead of 1.2. That export answers 'which SMF am I talking to', not 'which AMF is this', and the stale 1.2 made mods refuse to register at all - one logged 'skse framework 1.2 found. Expected minimum version not met.' and never appeared in the list. A mod that fails that check returns before installing anything, so this also brought back hotkeys that looked like a separate fault. The value was read out of the reference framework's own code bytes, not guessed.

### Added
- Diagnostics for consumer hotkeys: the DevBench state report now includes blockingWindowOpen (exactly what IsAnyBlockingWindowOpened answers a mod) and a consumerWindows list naming each registered window with its open/blocking flags, so a window left latched open identifies itself instead of leaving several suspects.

## 1.7.3 - 2026-09-10 - untested

### Fixed
- Restored the null checks the hand-written wrappers used to do, now generated over the vendored cimgui rather than written by hand: 656 guards across 614 functions, so a mod passing a null label or a null value pointer gets a no-op instead of crashing the game. ImGui's optional p_* pointers are deliberately left alone, because guarding those would stop windows drawing.

## 1.7.2 - 2026-09-10 - untested

### Changed
- Full export parity with SKSE Menu Framework: all 1,420 of its entry points are now exported, up from 252, by vendoring the generated cimgui 1.90.8dock this framework's Dear ImGui matches instead of hand-writing wrappers a tranche at a time. A mod can no longer come up blank because of a name we never got round to adding.

## 1.7.1 - 2026-09-10 - untested

### Fixed
- A mod's settings page could come up blank: an SMF consumer resolves every drawing function it uses by name from the framework, so a name this framework did not export came back null and that part of the page simply drew nothing. Thirty-eight missing entry points were added, measured by scanning every SKSE plugin on the machine that uses the framework - all 140 are now fully covered.

## 1.7.0 - 2026-09-10 - untested

### Added
- A master switch for custom menu art, off by default, in the INI as [Skin] bEnabled and as a toggle on the Framework Settings page - so the built-in look is what you get unless you deliberately install artwork and turn it on.
- A Save button on the settings page's menu list, so an order you have dragged into place survives the game closing instead of reverting to the registration order.

## 1.6.9 - 2026-09-09 - working

Custom menu art, so a UI author can make the framework match their own interface instead of
accepting the built-in look. Requested for borokoshow, to match Dragonborn UI.

Four new `[Skin]` INI keys, all optional and independent:

* `sFrame` + `uFrameCorner` - a square RGBA PNG with a transparent centre, drawn as a nine-slice
  around the window. 192x192 with 64px corners is the suggested default. The corner is a key
  because only the artist knows where their ornament stops; an over-large value is clamped and
  logged rather than drawn as flipped middle slices.
* `sBackground` - a PNG drawn behind the window's content. **Tiled if it is 512px or smaller on
  both sides, stretched otherwise**, decided from the image rather than from a fifth key, so a
  256x256 seamless tile and a 1920x1080 backdrop both simply work.
* `sPlates` - a folder holding any of `toggle.png`, `slider.png`, `tab.png`. `toggle.png` draws
  now, tinted by the on/off state so the switch stays readable as a switch. The slider and tab
  plates load and are reported but are not drawn yet: those two are Dear ImGui built-ins rather
  than this framework's own widgets, so restyling them means replacing the widgets outright,
  which would land on every consumer's page at once and is not worth doing carelessly.

A supplied frame REPLACES the built-in knotwork rather than drawing over it, and is drawn
whichever theme is selected - shipping frame art is itself the request for a frame.

The existing nine-slice was generalised rather than duplicated, so an author's frame goes through
exactly the same geometry the built-in knotwork was proven on.

PNG only: the texture loader decodes through WIC, which reads PNG/JPG/BMP/TIFF and does not read
DDS at all. A `.dds` is now rejected by name in the log rather than failing as an unexplained
"could not decode". (Adding real DDS support via DirectXTK remains the separate open to-do item.)

Two DevBench ops, so an artist iterates without restarting: `amf.process op=skin` reports exactly
what loaded and why anything did not, and `op=skinreload` re-reads every texture from disk.

No new user-visible strings, so no translation work is owed for this version.

Proven in game on Test Build (SE 1.5.97): a 192x192 frame with 64px corners and a 256x256 tiled
background drew correctly around and behind the live window, corners fixed and edges stretched,
with the toggle plate tinted by state; `op=skinreload` reloaded from disk mid-session.

## 1.6.8 - 2026-09-08 - working

### Fixed
- The search box above the mod list has been drawing English in every language since 1.6.6: its two strings (the "Search" hint and "no mod matches that") were never added to any translation file, English included. They are now in all eleven. A missing key falls back to the compiled English, so nothing was broken - it just was not translated.
- The Controls line describing the section-tab navigation, added in 1.6.7, reached only English, Japanese, Korean, Chinese and Russian. German, French, Spanish, Italian, Polish and Czech now have it too.

### Added
- `.MD\scripts	ranslation-coverage.py` (project tooling, not shipped): compares the keys the code asks for against every shipped translation file, for every mod. `pre-finalize-check.ps1` now runs it, so a mod can no longer finalize with strings that never reached the files.

## 1.6.7 - 2026-09-08 - working

### Changed
- The D-pad now walks a mod's tabs instead of falling out of the menu on the first press. In a mod with several sections - Character Progression Control has twelve - left steps back one tab, and only a left press already at the FIRST tab hands navigation back to the mod list. Right steps forward a tab while the cursor is on the tab bar itself, and still moves between a page's own controls below it. Before this, left inside a mod's page had no use except leaving it, so the tabs could not be reached with the D-pad at all.

### Added
- amf.menu gained op=nav (arg dir: left|right) and op=focus (arg pane: list|options): the D-pad press and the pane placement, driven from DevBench. nav is read in the same place a real press is read, so a test exercises the shipped decision rather than a path around it.
- The amf.menu state JSON now reports the open mod's page, pageIndex and pageCount, so a driving tool can assert which section is showing without reading pixels.

## 1.6.6 - 2026-09-08 - working

### Added
- Added a search box above the mod list. Once a load order registers thirty or more pages the list is longer than the pane, and typing two or three letters is faster than scrolling. It matches the name you actually see, so a renamed entry is found by its new name.

## 1.6.5 - 2026-09-06 - working

### Added
- The font atlas is now built from EVERY mod's translation file of the active language in Interface/Translations (any <Mod>_<language>.txt), not only the framework's own, so a consumer mod's translated page draws its kana, hangul, hanzi or Cyrillic without touching fonts itself.
- AMF_GetLanguage() in the C API: the language the framework is showing, for consumer mods to follow - one Language setting drives every page.

## 1.6.4 - 2026-09-06 - working

### Added
- Language support for the framework's own text. Every string the framework draws (the Settings, Controls and Help pages and the window chrome) now comes from Interface/Translations/ApocryphaMenuFramework_<language>.txt - the SKSE translation file format - in the game's language, with an INI override (sLanguage) and a Language combo on the Framework Settings page that switches live. Anything a translation lacks falls back to English, then to the compiled text.
- Eleven languages shipped: English, Japanese, Korean, Chinese, Russian, German, French, Spanish, Italian, Polish, Czech (the first four proven in game). The FOMOD asks which to install: all (follow the game) or one.
- The font atlas is built from the loaded translation's own characters, and a system CJK or Hangul face is merged in for the glyphs the Latin face lacks (Meiryo / MS Gothic, Malgun Gothic, Microsoft YaHei / SimSun), so kana, kanji, hangul, hanzi and Cyrillic all draw.
- amf.menu gained op=language (a translation name or auto).

### Proof
- Test Build SE 1.5.97, 2026-09-06 22:52: one launch, live switches to Japanese, Korean, Chinese, Russian and German over amf.menu, each captured on the Framework Settings page with its script rendered; 55 strings read per language; the merged face logged per language; auto returned to English.

## 1.6.3 - 2026-09-06 - working

### Changed
- THE DLL IS NOW NAMED !ApocryphaMenuFramework.dll. SKSE loads plugins in alphabetical order, and a mod that takes hold of the framework at its own load (Ammo Patcher, through the vendored SKSE Menu Framework header) only finds a framework that loaded before it; the leading ! puts this DLL first. Only the DLL and its PDB are renamed - the INI, the log, the data folder and every setting keep their names. The old name still resolves: a mod that looks the framework up as ApocryphaMenuFramework by name is answered with this module.
- If an old ApocryphaMenuFramework.dll is left beside the new file (a hand update), it loads inert, the log says so, and a notice at load asks for it to be deleted. Mod Organizer users replacing the mod folder are unaffected.

## 1.6.2 - 2026-09-06 - working

### Fixed
- A mod with many sections keeps its tab labels whole. The section tab bar now uses the scrolling fitting policy with a tab-list button on its left, so a page with twelve sections (Character Progression Control) no longer squeezes them to "Level... Expe... Skills"; the bar scrolls and the list button opens every section by name. The same fix SkyHUD Settings Menu 1.0.3 made for its own tab bar, now in the framework for every mod.

## 1.6.1 - 2026-09-05 - working

### Changed
- THE EMBEDDED DEAR IMGUI IS NOW THE ONE SKSE MENU FRAMEWORK USES: 1.90.8, docking branch, on both build lines (an overlay port; vcpkg's registry never carried 1.90.8). Mods built against the SKSE Menu Framework header now meet exactly the colour, style-variable, item-flag and struct layout that header was generated from, so nothing needs translating; the translation added in 1.5.9 stays in place as a safety net.

### Added
- FAST EXIT, so a closing game can never get stuck where nothing can end it. When the game asks Windows to exit, the framework flushes its log and ends the process at once instead of letting every loaded DLL and driver run its shutdown code - the phase in which a Skyrim 1.7.104 test game wedged on 2026-09-05 into a state no in-process or external kill could reach. On by default (bEnabled under [FastExit]; a toggle on the Settings page). Nothing the game needs is lost: saves are written when you save, settings when you change them.

## 1.6.0 - 2026-09-05 - untested

### Changed
- The log now names WHICH mod asked for the framework. Every line the alias writes when it answers a module-name or file-name lookup carries the asking DLL's name, so a mod that reaches the framework and still shows no page can be told apart from one that never reached it (Ammo Patcher and Critical Hit Tweaks, 2026-09-05).

## 1.5.9 - 2026-09-05 - untested

### Fixed
- THE FRAMEWORK NO LONGER GOES SILENT WHEN AN EARLIER SKYRIM IS STILL RUNNING. The once-only load guard used a name shared across the whole logon session, so a SkyrimSE.exe left behind from a previous session - stuck on exit, which happens - made the next game's framework believe it had already loaded: SKSE reported it loaded, and it did nothing, with no log line and no menu. The guard is now scoped to the running game process.
- PAGES FROM THOSE MODS NO LONGER CRASH THE GAME, AND THEIR COLOURS AND SPACING COME OUT RIGHT. Once the pages above could reach this framework, the first one opened (Simple Follower Framework) crashed the game: it asked for the style object (igGetStyle), which this framework did not provide, and read from the null it got back. Eighteen framework calls the ten mods on hand use were missing (igGetStyle, igBeginGroup, igEndGroup, igRadioButton_Bool, igPushStyleVar_Float/Vec2, igPopStyleVar, igPushItemFlag, igPopItemFlag, igPushStyleColor_U32, igAlignTextToFramePadding, igCalcTextSize, igIsItemDeactivatedAfterEdit, igInputFloat3, igSliderFloat3, igSliderFloat4, igSetNextItemOpen, igTreeNode_Str) and are now provided. Underneath that: the shared consumer header numbers colours, style variables and item flags the way the Dear ImGui 1.90 docking branch does, which is not how either of this framework's build lines number them, so every such index is now translated by name, and igGetStyle hands back the style laid out the way the consumer reads it. Verified by driving all 30 registered pages in game without a crash or a missing-call warning.
- THIRD-PARTY SKSE MENU FRAMEWORK MODS NOW APPEAR. Since release, most mods built against SKSE Menu Framework registered nothing with this framework and showed no page (Nexus reports 2026-09-05; Discord list: Bobbing Framework, Casting Bar, Cinematic Conversation Camera, First Person FOV SKSE, HorsePower, Mesh Rendering Framework, Simple Follower Framework, Tears of Kyne, True Flasks). Their shared consumer header first checks that a file named Data/SKSE/Plugins/SKSEMenuFramework.dll exists and skips registration when it does not - and this framework ships no file under that name. A file-system query for exactly that name is now answered with this framework's own DLL, the same way a module-name lookup already was, so the check passes and the mod goes on to register. The C++ runtime DLL is covered too, since a mod built against the dynamic runtime performs that check inside it. Found by reproducing on this machine with the MO2 alias plugin removed: only the framework's own mods registered, every third-party mod logged 'SKSE Menu Framework not installed'.

## 1.5.8 - 2026-09-05 - untested

### Added
- SCREEN-WIDE DRAWING FOR MOD MENUS. A mod's settings page can now draw an overlay anywhere on
  the screen, not only inside its own window - the foreground and background draw lists are
  exposed (igGetForegroundDrawList, igGetBackgroundDrawList) along with the rectangle and text
  draw calls. The first use is a HUD-position preview that shows where a HUD element will sit
  while you adjust it; any mod can use it.

## 1.5.7 - 2026-09-04 - working

> Line 1 observed on Apostasy Test Build (SE 1.5.97) 2026-09-04 23:53: log opens "Build line: SE
> 1.5.97 / AE 1.6.x (CommonLibSSE-NG 3.7.0); game 1.5.97.0", 21 sections listed, menu opened and
> captured, no crash log. Line 17 observed on Steam 1.7.104 + SKSE 2.3.1 + Address Library v13
> 2026-09-04 23:47-23:55: "Build line: Skyrim 1.7.x (CommonLibSSE-NG 7.2.0); game 1.7.104.0", DXGI
> present + D3D init + PollInputDevices hooked, DevBench answered amf.menu state. Wrong-pick test:
> the line-1 DLL on 1.7.104 stops with this mod's own "re-run the installer" box, not CommonLib's.

### Added
- A SKYRIM 1.7.x BUILD, AND AN INSTALLER THAT ASKS WHICH GAME YOU HAVE. Skyrim 1.7.99 / 1.7.104
  (what Steam installs since 28 August 2026) ship their Address Library databases in a new format
  that the CommonLibSSE-NG this mod was built on cannot read, so on those games the mod could
  never start. The same source now builds twice: the SE 1.5.97 / AE 1.6.x line as before, and a
  Skyrim 1.7.x line on CommonLibSSE-NG 7.2.0. The download is a FOMOD whose one question is your
  game version; it installs the matching DLL. The 1.7 DLL is distributed under GPL-3.0-or-later
  because of the library it links (its licence texts and a NOTICE install alongside it); the
  mod's own source and the SE/1.6 DLL stay MIT.
- The load-time log now names the build line and the game version on its third line, so a log
  from a bug report says at once which DLL the player has and which game they ran it on.

### Changed
- THE ADDRESS LIBRARY CHECK NOW TELLS THE TRUTH ABOUT SKYRIM 1.7.x. Skyrim 1.7.99 and 1.7.104
  ship their Address Library databases in a new file format (5) that the CommonLibSSE-NG this
  build links cannot read, so on those games the mod could never get past its first address
  lookup - and 1.5.6's message would have sent the player to install a library that changes
  nothing. The pre-check now reads the database's format number and, on a 1.7.x game or a
  format this build does not read, stops with: which game versions ARE supported (SE 1.5.97,
  AE 1.6.x), that updating the library will not help, and the Discord hub. The missing-file
  message now names the "All in One" download plainly.

## 1.5.6 - 2026-09-05 - untested

### Added
- POINT-AND-CLICK DRIVING FOR TESTS. `amf.menu` gains `op=cursor` (x, y in display pixels) and `op=click` (optional x, y, button), and `op=state` now reports the software cursor. While the menu is open the game recentres the OS cursor every frame, so the framework's own integrated cursor is the only position Dear ImGui ever sees - an outside driver could not point at a widget at all. The cursor is placed and its position published ahead of any queued button in the same frame, and a click's press and release land on separate frames because event trickling is off in this framework. Verified: a third-party mod's checkbox toggled by a driven click, its own Save Settings pressed the same way, the value written to its INI on disk, the game saved and reloaded with the page showing the change.
- A PLAYER-FACING ADDRESS LIBRARY CHECK. Every address here resolves through Address Library, and CommonLibSSE opens its database on the first lookup with an error that names a build-directory hash and nothing to act on (bug report 2026-09-04). The framework now checks for the database file for the running game version before its first lookup and, when it is missing, stops with the file name, the download to install (Address Library for SKSE Plugins - All in One), and where to get help.

## 1.5.5 - 2026-09-04 - untested

### Added
- A NULL GUARD FOR EXPORTS THIS FRAMEWORK DOES NOT HAVE, AND A LISTENER THAT NAMES THEM. The stock SKSE Menu Framework header calls whatever `GetProcAddress` returns without checking it, so a consumer asking for a widget export this framework lacks used to receive null and crash the game the first time its page drew. This framework already answers the module-name lookup for SKSE plugins; it now answers their `GetProcAddress` for its own module too. A name it exports resolves normally; a name shaped like the SMF consumer API that it lacks resolves to a generated stub that returns zero (in both the integer and floating-point return registers) and logs the name once - the page draws with a hole in it instead of the game dying, and the log says which export to add next. Names not shaped like the consumer API get the honest null: the first live run showed a plugin probing every module for `ReShadeRegisterAddon`, taking a stub as "this is ReShade", and calling it.
- Every resolved export is logged once at debug as an inventory of what consumers actually use.

### Fixed
- Argument null guards across the widget exports: a null label, format string, value pointer, buffer or draw list no longer reaches Dear ImGui.

## 1.5.4 - 2026-09-04 - untested

### Fixed
- MODS BUILT FOR SKSE MENU FRAMEWORK NOW ACTUALLY REGISTER. 1.5.3's module-name alias was right and arrived too late: measured against a real-SMF reference run, two third-party consumers resolve the framework inside their own `SKSEPlugin_Load` - about 100 ms after this framework loaded, before any SKSE message exists - and the stock header caches whatever that first call returns. Our sweeps at load and `kPostLoad` could not reach an import table that did not exist yet. The alias is now applied from a loader DLL-load notification (`LdrRegisterDllNotification`), which fires for each plugin after its imports are resolved and before its entry point runs, so it precedes every possible first lookup. Verified: nine third-party SMF mods plus DevBench registered - Block Overhaul, Dynamic Wind, Input Manager, Quick Commands, STB Widgets, Show Player In Inventory, StepUpOnto, Typing Mode, devbench - taking the menu from 12 sections to 21, and DevBench reports the framework version it resolved (`v1.2`) instead of `v0.0`.
- OPENING A THIRD-PARTY PAGE NO LONGER CRASHES THE GAME. With registration working, the first page opened (Block Overhaul) called an ImGui widget export this framework did not have; the stock header's widget wrappers are not null-guarded, so a missing export is a call to address zero. Added the 61 widget exports the installed consumers reference and this framework lacked - windows, popups, combos, tab bars, tables, tooltips, drag/input floats, tree nodes, style colours, text wrap, `igGetIO`, `ImDrawList_AddLine` and the rest - with the public consumer header's signatures verbatim. Every third-party page was then opened in turn with the game surviving each one, and Input Manager's and STB Widgets' pages photographed rendering their tabs, sliders, combos and tables.

## 1.5.3 - 2026-09-04 - untested

### Added
- MODS BUILT FOR SKSE MENU FRAMEWORK NOW FIND THIS FRAMEWORK. The stock SMF consumer header resolves the framework by module name - `GetModuleHandleW(L"SKSEMenuFramework")` - which returned null against AMF, so every such mod registered nothing. AMF now answers that name: it redirects the `GetModuleHandleW`/`A` imports of SKSE plugin modules so a lookup of `SKSEMenuFramework` resolves to this module, and everything else passes through untouched. No second binary ships, nothing is renamed, and no system code is patched - an import-table entry is a data pointer. Measured in game: four mods that had never registered (Carry Weight Per Level, Remember Lockpick Angle, Sure of Stealing, Wait Your Turn Redux) now appear, taking the menu from eight sections to twelve.
- THE CONSUMER SURFACE beyond settings pages, which was missing entirely: `AddWindow`, `AddWindowWithView`, `RegisterHudElement`, `UnregisterHudElement`, `LoadTexture`, `DisposeTexture`, `PushFont`, `PushRegular`, `PushSolid`, `PushBrands`, `Pop` and `IsHotkeyEnabled`. A mod that wants its own window or a HUD element calls these rather than `AddSectionItem`, and their absence was invisible from its side - the header's wrappers are `if (func) { ... }` with no else, so a missing export is a silent no-op rather than an error. Consumer windows and HUD elements draw every frame, independent of whether this framework's own menu is open; textures are decoded once and cached by path.

### Changed
- `IsAnyBlockingWindowOpened` now also reports a consumer window that is open and taking input, not just this framework's own menu, so a menu launcher gets one truthful answer for the whole process.

### Fixed
- The import redirect is confined to modules under `SKSE\Plugins`. The first build applied it process-wide and the game died a few seconds after load with no crash log at all: MO2's usvfs virtualises the file system by hooking this same call family, so redirecting its imports breaks the thing every file access depends on.

## 1.5.2 - 2026-09-04 - working

### Changed
- THE NESTED SURFACE NOW FITS THE JOURNAL IT IS HOSTED IN. Opened from the SKSE MENUS row, the framework is sized and placed to the journal panel around it instead of to its own default proportions, so it reads as a page of that menu rather than a larger window sitting on top of it.
- The panel is MEASURED off the live movie, every frame, rather than assumed. Its size differs under every art replacer - which is the same reason the row is injected instead of shipped - so a measured rectangle fits Untarnished UI, Dear Diary and vanilla alike with no patch for any of them. The last good measurement is kept while the journal fades its panel in and out, so the window never flickers between two sizes.
- This is GEOMETRY ONLY, and it applies to the nested row ALONE. Opened by the hotkey, or by a menu launcher such as Risa's All In One Menu through the API, the framework keeps its own window exactly as before. What it draws is identical either way.

### Removed
- THE AMF API DEMO IS GONE, along with its `bShowApiDemo` setting and the `[Debug]` section that held it. It existed to dogfood the public API and to give the window enough selectable content to judge gamepad navigation while the registry was empty; the registry is not empty any more, and a demo entry sitting among a player's real mods is a development artifact showing up in a shipped product.

### Changed
- THE WINDOW REMEMBERS WHERE YOU LEAVE IT, SEPARATELY FOR EACH WAY OF OPENING IT. These are profiles rather than fixed presets: one for the System menu row, one for the key. Each starts at a sensible default - the journal panel it is hosted in, and the centre of the screen - and the first time you move or resize either, that is what it opens at from then on. A menu replacer whose panel sits somewhere else therefore needs dragging once rather than a patch.
- Both windows can now be moved and resized. This supersedes the earlier "preset positions, never free placement" rule for this window; that rule existed so the window could not be left somewhere useless, and a remembered position with a reset button gives the same safety while letting people fix a layout we cannot predict.
- "Reset both to default" on the settings page puts them back. It matters more than a reset usually does, because the nested default is the journal panel measured live - not something you could drag your way back to.
- Geometry is stored as fractions of the screen, so a saved position stays right if you change resolution.
- The shipped INI now carries the `[Window]` and `[Watchdog]` sections. `[Watchdog]` was written by the settings page but never shipped, so the file the mod saves had a section the file it installed did not.
- KEYBOARD OR CONTROLLER NAVIGATION IS NOW DETECTED, ALWAYS, AND IS NO LONGER A SETTING. There were two switches for it - one holding the mode, one deciding whether the detector was allowed to write it - which is two controls describing one fact the game already knows, and they could be left contradicting reality: a player who picked up a pad kept keyboard navigation until they found the toggle. `bControllerMode` and `bAutoInputMode` are both gone from the INI and the settings page; the menu simply follows whatever you last used and shows which one it is reading, so its accuracy can still be judged while playing.
- Only DELIBERATE input moves it, unchanged from when detection was opt-in: a button going down, a mouse click, real mouse movement, or a stick past the navigation deadzone. A resting stick or a nudged mouse cannot flip navigation mid-menu.
- `AMF_GetInputMode()` now reports the LIVE device rather than a stored setting. Consumers use it to word their own prompts, and a saved value could say "keyboard" while the player was on a controller. The export, its name and its signature are unchanged.
- ONE BUILD, NO INSTALLER. The FOMOD is gone and 1.5.1's two options are retired. Both ways in are now on by default - the SKSE MENUS row in the game's System tab, and F1 - and either can be switched off from the settings page or the INI. Which one you prefer is a preference, and a preference that needs a reinstall to change is the wrong shape for it; it also forked the INI, the documentation and every support answer in two for no gain.
- The System row is now ON THE SETTINGS PAGE, where the installer question used to be. It was previously reachable only by editing the INI by hand, which was tolerable while the installer asked about it and is not now that nothing else does.

### Fixed
- THE MENU NOW CALLS ITSELF BY THE PRODUCT'S NAME. The window title, the version line, the help text and the INI header read "ApocryphaRealm Menu Framework"; they had been showing "Apocrypha Menu Framework", which is the form reserved for the repository, the DLL and the INI filename rather than for anything a player reads. The banner has said ApocryphaRealm since 1.4.x, so the menu was the odd one out.
- The window identities moved from `##` to `###`. ImGui hashes the whole label when the separator is `##`, so renaming the visible part would have silently orphaned each window's saved layout entry; with `###` the identity is fixed and the displayed name can change freely.
- A SIZE SET BY HAND ON THE HOTKEY WINDOW NO LONGER GETS OVERWRITTEN BY THE NESTED ONE. Both modes drew the same ImGui window name, so both shared one saved geometry in the layout file - and the nested mode rewrites its size every frame, so it clobbered the entry and the hotkey window came back the wrong size. Each mode now has its own window identity and its own remembered geometry. The hotkey window's identity is unchanged, so a size already saved survives the update.
- The nested window is inset by the window padding rather than filling the panel rectangle exactly, so its border no longer sits flush on top of the journal's own and read as one thick misaligned rule. It also no longer offers a resize handle, because the size there belongs to the journal - a handle that visibly does nothing is what the snapping looked like from the outside.
- THE NESTED WINDOW WAS BIGGER THAN THE SCREEN. It was measured from `Menu_mc`, and `getBounds` returns the union of a clip and ALL its children whether or not they are visible - the journal keeps its Save/Load, Creations, Help and Confirm panels parented and hidden rather than removed, so the honest bounds of `Menu_mc` really are larger than the panel you can see. In game it measured 113% of the screen wide and 125% tall. It now measures `PanelRect`, the invisible rectangle the page already carries for exactly this purpose and which has no children of its own, and any candidate that comes back larger than the stage is rejected rather than used.
- THE NESTED INSTALL NOW KEEPS F1. It shipped with the key unbound, on the reasoning that the row is the thing you are meant to find; that left the journal as the only way in, so reaching mod settings always meant pausing first. Both work now - the row opens the framework fitted to the journal, F1 opens it as its own centred window - and `uToggleKey=0` still gives the key back to the game for anyone who wants that. A menu launcher that takes the key over, such as Risa's All In One Menu, continues to do so.
- APPEARANCE SETTINGS ARE NO LONGER HIDDEN in the nested install. They were hidden on the belief that a nested surface would wear the game's own menu artwork and so have nothing to theme. It does not - nesting changes geometry only, and everything inside that rectangle is still drawn by this framework in this theme - so the install that most needs the controls was the one that could not reach them, and was told something untrue about its own menu while it happened.
- SystemRow.h described a handler-wrapping approach that was tried, does not work, and is not what the code does. `GetVariable` does not return an AS2 function - not off the page instance and not off the class prototype - so there is nothing to read out and stash. The file now documents the second listener that is actually installed, and why leaving the game's own handler untouched is the safer shape.

## 1.5.1 - 2026-09-04 - working

### Added
- MOD SETTINGS ARE NOW REACHED FROM THE GAME'S OWN PAUSE MENU. The journal's System tab carries a new row, SKSE MENUS, below QUIT and beside SAVE, LOAD and SETTINGS. Selecting it opens this framework, so settings are found where a player already looks for settings instead of behind a hotkey they have to be told about.
- The row is added to the menu AS IT OPENS rather than by shipping a replacement for any of the game's files. That matters for compatibility: the journal's artwork is one file, and every menu-replacement mod ships its own copy of it, so a framework that shipped one too would collide with all of them. This one collides with none, and takes on whatever look the installed artwork gives it.
- A listener is added BESIDE the menu's own handler rather than replacing it, so save, load, installed content, settings, controls, help and quit keep behaving exactly as they did. In the worst case the new row does nothing; it cannot break the menu it lives in.
- A FOMOD installer with two choices. Nested puts the row in the game's menu, unbinds the hotkey, and hides the appearance settings because the surface wears your own menu artwork. Standalone keeps the framework as its own window on its own key with the full settings, which is also what menu launchers expect to find. The binary is the same either way - only the settings it starts with differ - and `bSystemMenuRow` switches between them at any time.

### Notes
- Checked against Risa's All In One Menu 5.0: it detects this framework, lists it in its launcher, reads the nested build's unbound key correctly, and edits none of our settings. The two run together with no errors on either side.

## 1.5.0 - 2026-09-03 - working

### Added
- THE LAUNCHER-CONTROL API. A menu-launcher mod - one that gathers other mods' menus behind a
  single key - drives a menu framework through three exports rather than a consumer header:
  `GetMainWindow`, `IsAnyBlockingWindowOpened` and `SetHotkeyEnabled`. None of the three existed
  here, so a launcher that reached this framework could do nothing with it. All three are now
  exported with the signatures and semantics those launchers already expect.
- `GetMainWindow` returns a static object whose LAYOUT is the contract, not just its address:
  `std::atomic<bool> IsOpen` then `std::atomic<bool> BlockUserInput`, nothing before them. The
  caller stores into `IsOpen` directly to show or hide the menu. It has static storage duration,
  so a caller holding the pointer across a save load cannot end up writing into freed memory.
- A once-per-frame reconcile on the render thread keeps that object and the framework's own
  visibility agreeing in BOTH directions: a menu opened from outside appears, and a menu closed
  in game - by the toggle key, by a DevBench call, by anything - stops reporting itself as open.
- `SetHotkeyEnabled(false)` hands the framework's own toggle key to the launcher for the session.
  Runtime only: it is never written to the INI, so the player's own binding comes back if the
  launcher is uninstalled.

### Fixed
- The aliased second `SKSEPlugin_Load` returned false, and SKSE calls `FreeLibrary` on any plugin
  whose load reports incompatible. It now returns true and does nothing else - the once-guard
  still refuses to initialise twice, it just no longer asks for the module to be thrown away. In
  the shipped configuration both names resolve to one module, so nothing was actually lost; this
  removes a misleading "reported as incompatible" line from every SKSE log and makes the
  behaviour correct if the alias is ever a distinct file.

### Known limitation, stated rather than papered over
- These exports are NOT yet reachable through the MO2 companion plugin's virtual
  `SKSEMenuFramework.dll` name. That alias is a usvfs PATH mapping, so the loader opens the same
  real file and dedupes it into one module named `ApocryphaMenuFramework.dll` - read from a
  running game's module list, no module of the SMF name exists in the process at all. A launcher
  that resolves the framework with `GetModuleHandleW(L"SKSEMenuFramework.dll")` therefore finds
  nothing, and no amount of exporting on this side can change that.
- Found while checking this framework against Risa's All In One Menu 4.9 (2026-09-03), whose
  framework button logged `GetMainWindow returned null`. The route chosen is to ask that mod to
  probe `ApocryphaMenuFramework.dll` as a second module name - one line on their side, and
  everything after it already works. Nothing here waits on it: the exports ship either way, and
  any launcher that reaches this framework by a working route can drive it today.

## 1.4.9 - 2026-09-01 - working
(author pad confirm 22:36: "the controller nav works properly including selecting a slider with
a"; pane crossing both ways, the return landing and three auto-input switches all observed live
over DevBench)

### Fixed
- Navigation could not cross from the mod list INTO the options - only back the other way
  (author playtest: "neither the d-pad the left or the right stick will let me go from the left
  to the right pane"). ImGuiWindowFlags_NavFlattened, used in 1.4.8 to let navigation cross the
  pane border, is documented for child windows with NO scrolling; the options pane scrolls, and
  flattening it produced exactly that asymmetry. Navigation is contained in each pane again and
  the crossing is explicit: right in the list enters the options, left in the options returns,
  from the left stick, the D-pad or the arrow keys alike. It is ignored while a slider is being
  adjusted, so pushing left inside a slider changes the value instead of leaving the pane.
- Coming back from the options now lands on the entry whose page is open, not wherever the
  list's cursor was left (the author: "if I select settings and go right and I scroll to the
  bottom and then I go back left then it should take me back to the settings menu selector not
  to the bottom of the left pane").
- The knotwork themes (Vanilla, MO2 Skyrim) drew their frame art directly ON each box's rect,
  so the art's own hairlines landed on the pane borders and its 26px corner ornaments covered
  the first line of text - the window's version line read "pocrypha Menu Framework". The art is
  now drawn just OUTSIDE each box so it frames the border instead of covering it, the window's
  padding clears the ornament's width, and the two panes have room between them. Themes without
  the art (Untarnished) are unchanged. Author, comparing the two: "the Skyrim theme doesn't let
  them fully see all the corners and lines of a box with a border ... you might have to change
  the margin between those areas and the edge of the menu frame".

### Changed
- THE THEMES ARE NOW TWO (author, 2026-09-01: "we don't need the mo2 Skyrim or the vanilla
  versions anymore as they're extremely similar in colour and design"). "Vanilla" and
  "MO2 Skyrim" differed only in the tone of their text; they are merged into ONE theme named
  "Skyrim" - the knotwork frame, silver and gold lines, and the crisper warm off-white text -
  which is the default. "Untarnished", the framework's original identity, stays as the plain
  alternative. An INI naming a retired id ("vanilla", "mo2-skyrim") is migrated to "skyrim"
  rather than dropped, so nobody's saved choice is lost.
- EVERY theme now uses the same padding, so the two look alike in spacing and switching theme
  changes the colours and the art but never the geometry (the author: "edit the untarnished
  theme to have the same margin edits so they look similar in spacing"). The figure comes from
  the knotwork ornament's fixed 26px band; the plain theme simply shares it.

### Added
- "Detect input automatically" (bAutoInputMode, OFF by default): the menu follows whatever you
  last really used - a key press, a mouse button, real mouse movement or a stick past the
  navigation deadzone - and switches between keyboard and controller navigation on its own. Idle
  noise is ignored by design, because a mode that guesses wrong is what drives nav focus astray;
  the explicit toggle remains the rule and this simply steers it. The settings page shows what
  the detector currently reads, and how long ago, so its accuracy can be judged while playing.
- `amf.menu` gains a theme op (switch theme by id at runtime) and reports controllerMode,
  autoInputMode and lastDevice - so a two-theme visual comparison, and the auto-switch itself,
  can be tested in ONE game session.

## 1.4.8 - 2026-09-01 - working

### Added
- MENU-SHELL personalization (author verdict 2026-09-01), a presentation layer over the page
  registry - registered mods are untouched and know nothing about it:
  - ALIAS: rename any mod's menu entry from the Framework Settings page. The alias replaces the
    mod's name in the list and in the content pane's heading, and it is what alphabetical
    sorting uses.
  - ORDER: the list is alphabetical by default and EVERY entry always shows its position
    number. Typing a new number moves that entry there and every other number re-flows
    (insert-and-shift), so nobody has to number the whole list by hand. A mod installed later
    inserts at its alphabetical position within the existing sequence. "Reset to alphabetical"
    is one button. Repositioning/floating per-mod windows are deliberately NOT included.
  - Both live in AMF's own INI, in new [MenuAlias] and [MenuOrder] sections.
- CONTROLLER NAVIGATION scheme (author spec, 2026-08-31): the left stick moves through the list
  AND across into the options with no button press (both panes are nav-flattened, so ImGui's
  navigation is no longer trapped inside each child window); A takes hold of a slider and the
  RIGHT stick then moves it; A opens a drop-down, the sticks choose, A confirms. Both sticks are
  captured while the menu is open and exactly one is wired to ImGui's navigation axes per frame -
  the left while nothing is being edited, the right while something is - so adjusting a value can
  never also move the selection, and the idle stick is released so it cannot leave an axis stuck.
- `amf.menu` gains alias / move / resetorder ops and reports `customOrder` plus the player-facing
  `displayOrder` (position, mod, shown name), so the shell is drivable and assertable headlessly.

## 1.4.7 - 2026-09-01 - working

### Fixed
- CRASH changing the font or the text size (author playtest, 2026-08-31): the font-atlas
  rebuild ran AFTER ImGui_ImplDX11_NewFrame - the only place the DX11 backend recreates its
  device objects - so the rebuild destroyed the font texture with nothing left in the frame
  to recreate it, and the frame then rendered its draw data against a dead texture. The
  rebuild (invalidate + BuildFonts) now runs BEFORE the backend NewFrame, so the backend
  recreates the font texture in the same frame.

## 1.4.6 - 2026-08-30 - working
(capture observed in game 2026-08-30 - the Nexus banner pictures; keybind widget observed in
game 2026-08-31 - gate `amf-keybind-test.ps1` PASS: spliced K captured un-reserved, Tab
captured reserved, observe-only confirmed at the main menu)
### Added
- IN-PROCESS CAPTURE ported from the Overhaul line: `amf.process op=capture` saves the presented
  frame WITH the menu overlay to Data\SKSE\Plugins\ApocryphaMenuFramework\captures\<name>.png.
  Needed because native screenshots are pre-overlay; first use: real in-game pictures inside the
  Nexus banners (design decision, 2026-08-30).
- KEYBIND-CAPTURE WIDGET (`amf.keybind`, design decision 2026-08-31, queue L26): a reusable
  DevBench surface for testing binds without a mod's own settings page. `arm` records the next
  keyboard/gamepad press WITHOUT consuming it (observe-only - the game and menu still see it);
  `state` reports the captured key with its name, device and the framework's reserved-key
  verdict plus the current toggle key; `rebind` arms the real toggle-key rebind path; `cancel`
  disarms. Reuses the existing input-hook capture design and `SMF_GetReservedKeyCodes`.

## 1.4.5 - 2026-08-30 - working
### Removed
- Papyrus native-function scaffolding (AMF_Ping / AMF_SetTestValue / AMF_GetTestValue and the
  AMFTest.psc test script) - design decision, 2026-08-30: of the features beyond SKSE Menu
  Framework, "8 can be taken out, the rest are fine". The persistence channel, watchdog,
  DevBench tools, themes, fonts, controller nav and rebinding all stay.

## 1.4.4 - 2026-08-30 - working
### Changed
- REVERTED TO THE SMF SHAPE (design decision, 2026-08-30): AMF is a one-for-one replacement of
  SKSE Menu Framework plus user-friendly features that do not overhaul the game. The window is
  again a SIDE LIST (the framework's Settings / Controls / Help, then the registered mods) and a
  CONTENT pane for the selected mod's pages. The nested game menu - Quests / General / Stats /
  System top tabs, the Save / Load / Save and Quit / Quit actions, the live Stats page, the
  Mod-menus index - is gone from this project; it continues in the Apocrypha Framework Overhaul.
- `amf.menu select` paths are now `settings`, `controls`, `help` and `mod:<index>`; the old
  `system/...` paths are still accepted. `activate` is a no-op (no node carries an action).
### Removed
- Game-menu takeover code (console-command runner, system action panes, Stats/Quest/General panes).
### Known
- Observed on the Test Build (2026-08-30, headless): the menu opens, both registered mods list, every
  `select` path lands and holds - but on the FIRST open after a load the reported selection was one
  entry off (settings -> controls) once; repeated probes were stable. Watch for it with a mouse.

## 1.4.3 - 2026-08-28 - working
### Added
- HANG WATCHDOG + forced exit (the author: the game must be closable without Task Manager, even hung).
  A monitor thread watches the renderer's frame counter; if no frame is produced for `uSeconds`
  (default 120, `[Watchdog]` in the INI) the game is declared hung and the process terminates
  ITSELF. This works where taskkill/Task Manager fail: a wedged Skyrim's main thread is stuck in a
  kernel wait, but our watchdog thread is still scheduled, so `TerminateProcess(GetCurrentProcess())`
  from inside succeeds. Logged and flushed before terminating so the reason survives.
- `amf.process` DevBench tool - `op:"status"` (frames, seconds since the last frame, whether it is
  considered hung) and `op:"kill"` (force-exit on demand). It runs on devbench's LISTENER thread,
  which keeps answering while the main thread is wedged, so it can close a hung game deliberately.
- FONT PICKER on the settings page, separate from the theme selector (a theme sets colours; the
  face is an independent choice, so any font pairs with any theme). Lists the curated Windows faces
  that are present plus any .ttf/.otf dropped into
  `Data/SKSE/Plugins/ApocryphaMenuFramework/fonts`. Selecting one re-rasterises immediately.

## 1.4.2 - 2026-08-28 - working
### Fixed
- Menu text looked PIXELATED (the author). Cause was not the MO2-Skyrim theme carrying anything over - it
  was the font: AMF used ImGui's built-in ProggyClean, a 13px BITMAP face, and then magnified it with
  `FontGlobalScale = uiScale * textScale` (about 2.17x at 3200x1800). Magnifying a bitmap font is
  what produced the blocky edges. AMF now rasterises a real TrueType face at the NATIVE pixel size
  for the display (16px * uiScale * textScale) and keeps FontGlobalScale at 1.0, so nothing is
  stretched. Moving the text-size slider REBUILDS the atlas at the new size (crisp at any scale)
  instead of stretching it, done before NewFrame with the DX11 font texture invalidated.
### Added
- `sFontPath` in the INI (Display section): point the menu at any .ttf. Empty picks a clean system
  face automatically (Segoe UI, then Calibri, then Trebuchet). If none load, it falls back to the
  old built-in font rather than failing to render.

## 1.4.1 - 2026-08-28 - working
### Fixed
- External menu selection (the `amf.menu` DevBench tool) was overwritten every frame and snapped
  back to whatever tab ImGui thought was open - an automated sweep of all 14 menu nodes reported
  `tab=quests` for every one of them. Cause: the render loop copied its own selection state back to
  the shared globals unconditionally each frame, and ImGui's tab bar owns its selected tab
  internally, so it always won. Now the copy-back happens ONLY when a real UI interaction changed
  the selection, and a selection set from outside is flagged so that frame FORCES ImGui to the
  requested tab instead of adopting the tab bar's opinion. (Found by the framework's own automated
  pane sweep - the driving tool catching a bug in the thing it drives.)

## 1.4.0 - 2026-08-28 - working
### Changed
- MENU RESTRUCTURED to the real vanilla shape, corrected from the author's in-game screenshots: the game
  menu is THREE levels - TOP TABS across the top, a SIDE LIST belonging to the active tab, then a
  CONTENT pane - not the single vertical tree 1.3.8 shipped.
  * Top tabs: **Quests | General | Stats | System**.
  * The **System** side list: Save, Load, **Mod menus** (with each registered mod indented beneath
    it - the SkyUI/MCM equivalent's home), Settings, Controls, Help, Save and Quit, Quit.
  * Content pane renders the selected side entry: framework settings under System -> Settings, key
    bindings under System -> Controls, a Help page, a Mod menus index, live Stats, and each mod's
    pages (as tabs when a mod has several).
- `amf.menu` DevBench tool follows the new shape: `select` accepts `tab:<quests|general|stats|
  system>`, side paths like `system/controls`, and `mod:<index>`; a side path implies its tab, and
  `state` now reports the active `tab` alongside `selected`.

## 1.3.9 - 2026-08-28 - working
### Added
- `amf.mainmenu` DevBench driver (rule 64 applied to the GAME's start menu, the author's idea): the
  vanilla Main Menu can now be driven and inspected headlessly - op `state` (menu open, movie
  present), `userevent` (post the kUserEvent/BSUIMessageData message the menu's own buttons
  produce, e.g. text "New Game"), `invoke` (call an ActionScript method on the menu movie), and
  `getvar` (read an ActionScript variable). userevent/invoke queue to the main thread; state and
  getvar answer synchronously (2s main-thread wait). This is the exploration surface for pressing
  the REAL New Game button headlessly - the one start-menu action nothing could reach before
  (coc-from-menu is flaky by design; DevBench's cold load needed an existing save).
  VERIFIED live: `invoke path="_root.MenuHolder.Menu_mc.FadeOutAndCall" arg="StartNewGame"` (also
  reachable as op `delegate` name=StartNewGame) started a new game headlessly - lifecycle=newGame,
  playerLoaded=true, loaded into Tamriel. NOTE: calling the game's GameDelegate handler DIRECTLY
  (fxDelegate->Callback with fabricated args) crashes the game and was removed; the safe path is
  invoking the menu's own ActionScript so the game builds the delegate context itself.

## 1.3.8 - 2026-08-28 - working
### Added
- NESTED GAME-MENU MODEL (the author's directional project - build the model overnight): the menu is now
  a tree whose top-level categories mirror the vanilla game menu - Game Settings, Stats, Quest,
  General, and a System category (Save, Load, Mods, Save and Quit, Quit). The per-mod settings
  pages (the SkyUI/MCM equivalent) are nested under System -> Mods, exactly where Mod Configuration
  sits in SkyUI. "Game Settings" holds the framework's own settings (theme/controller/text/rebind);
  "Stats" shows a live read of player level/health/magicka/stamina (proof the categories can host
  real game data). Save/Load/Quit/Save-and-Quit are disabled placeholders: this is the STRUCTURE.
  Wiring the real game actions and intercepting Skyrim's own pause/journal menu are the next steps
  (PLANNED-MODS "AMF as a full nested game-menu replacement"). Built on 1.3.7, so it also carries
  the controller Start-closes-menu, left-stick nav, and live keybind-rebind fixes.

### Added (cont.)
- DevBench driving tool `amf.menu` (rule 31): the menu can now be opened, navigated, activated and
  read over DevBench REST (POST /api/tool/amf.menu {op:open|close|select|activate|state}), so it is
  testable HEADLESSLY - Claude can push the buttons itself. Vendors the MIT DevBenchAPI.
- System menu: Save and Quit-to-desktop are now WIRED (console exec via RE::Script), Quit behind a
  confirm. Load and Save-and-Quit stay placeholders (save enumeration / flush-before-quit sequencing).

## 1.3.7 - 2026-08-28 - untested
### Added
- Menu-toggle-key REBINDING on the Framework Settings page (the author): a "Rebind" button captures the
  next key pressed (Escape cancels) as the key that opens/closes the menu, saved immediately.
- Controller: the gamepad START button now CLOSES the menu in controller mode - the way out with a
  pad (the author: "no way to use the controller to leave the menu"). It only closes, never opens, so the
  game keeps its own Start button when the menu is down.
- Controller: the LEFT ANALOG STICK now drives menu navigation (fed as ImGui GamepadLStick analog
  nav with a deadzone). The live test showed gamepad button events arriving but no D-pad - the author was
  using the stick, which was not being captured, so he "couldn't switch menus".
### Observability (rule 31)
- Thumbstick x/y logged when controller mode is on, so the live test can confirm the stick reaches
  nav and whether the up/down axis needs flipping.

## 1.3.6 - 2026-08-28 - working
### Added
- New "Vanilla" theme (now the DEFAULT): the MO2 Skyrim look (knotwork frame + silver/gold) but
  with the cleaner, brighter text from the original Untarnished theme (#F5F2E9 warm white instead
  of #dddddd), which reads crisper on solid black. MO2 Skyrim and Untarnished remain selectable.
### Fixed
- Controller input mode did nothing when toggled on (the author pressed every button, no response).
  Cause: gamepad ImGui navigation needs the backend to advertise a gamepad - `io.BackendFlags |=
  ImGuiBackendFlags_HasGamepad` - which was never set, leaving NavEnableGamepad inert. Now set
  whenever controller mode is on (cleared otherwise).
### Observability (rule 31)
- Every gamepad event reaching the input apply-loop is now logged (code, down, controllerMode,
  mapped ImGuiKey), so a live DevBench-monitored test can distinguish "no gamepad events arrive"
  (a game/input-device-mode issue - e.g. Auto Input Switch absent) from "events arrive but nav is
  inert" (ImGui-side). Under live investigation with the minimap zoom-key regression report.

## 1.3.5 - 2026-08-28 - working
### Changed
- REBUILT the "MO2 Skyrim" theme from the real Trosski Skyrim style + a live MO2 screenshot.
  The first port had collapsed the whole style onto one grey (#b0b0b0 for text, border, and
  accents alike). It is now a layered palette: silver frame lines (#b0b0b0), brighter primary
  text (#dddddd), a dim secondary tone (#717171), and a GOLD accent (#a1912b) for selection
  rows, tabs, checkmarks, sliders and nav-highlight. Scrollbars, separators and tab states got
  their own graded tones instead of reusing one alpha ramp.
### Added
- The Nordic KNOTWORK frame that defines the MO2 Skyrim look: the style's border-image.png
  (78x78) is embedded (include/KnotworkBorder.h, RGBA) and drawn as a 9-slice frame around the
  AMF window - four ornate corner knots at fixed size, edges stretched between, transparent
  centre. Uploaded once to a texture on the game's device at init; a new per-theme `knotwork`
  flag gates it (on for MO2 Skyrim, off for Untarnished). The frame is drawn around
  every panel (outer window + both child panes), not just the outer window, matching the MO2
  style where each framed panel carries the ornament. Palette gained optional
  border/text/textDim/accent fields that fall back to `frame`, so Untarnished and INI-scanned
  themes are unchanged.

## 1.3.4 - 2026-08-28 - working
### Fixed
- Crash on EVERY successful save load (crash-2026-08-28-10-12-36.log, access violation in
  StripExtension constructing a std::string from address 0x1). Root cause: kPostLoadGame's
  message `data` is a BOOL (1 = load succeeded), not the save-name string the handler assumed;
  it dereferenced (void*)0x1. The earlier "data=0x0" seen on a failed load slipped past the null
  guard because false is 0x0. Fixed by using the correct SKSE payloads: the save name is now
  captured at kPreLoadGame (which really carries it), and kPostLoadGame is read as the bool
  success flag that restores (or, on new game / failure, clears) state.

## 1.3.3 - 2026-08-28 - failed (crashes on save load; number reclaimed by 1.3.4)
### Fixed
- 1.3.2's alias guard compared the module's own filename, but under MO2/usvfs the real DLL and
  the SKSEMenuFramework.dll alias resolve to one file (one module), and the hooked
  GetModuleFileNameW answered with the alias name for BOTH SKSEPlugin_Load calls - so AMF
  refused to load at all ("reported as incompatible during load", both handles). Replaced by a
  process-wide once-only guard (named event): the first SKSEPlugin_Load (always the real
  ApocryphaMenuFramework.dll) initialises, any later call is refused before registering anything.

## 1.3.2 - 2026-08-28 - failed (both loads refused; number reclaimed by 1.3.3)
### Fixed
- Crash on save load (crash-2026-08-28-09-55-35.log, EXCEPTION_ACCESS_VIOLATION in
  SKSEMenuFramework.dll+4C3C during preLoadGame): SKSE loaded this DLL a second time through the
  AMF-MO2-Plugin's SKSEMenuFramework.dll alias; that instance failed Load (hooks refused) but had
  already registered a message listener. SKSEPluginLoad now returns false immediately when the
  module's own filename is the alias - nothing registered, nothing hooked.
- Per-save state files (.amf-state.ini) were written to the real `Saves\` folder while the game
  under MO2 profile-local saves writes to `sLocalSavePath` (`__MO_Saves\`, VFS-mapped to the
  profile). The sibling path now follows `sLocalSavePath:General`, so it lands beside the .ess
  in every configuration.

## 1.3.1 - 2026-08-27 - untested

### Fixed
- kSaveGame/kPostLoadGame message handlers no longer construct a string_view over the message payload unchecked - a real in-game crash occurred during kPostLoadGame dispatch to this plugin while testing the persistence channel (loading a save with ~400 missing masters against a near-empty test mod list; root cause not conclusively isolated since Crash Logger wasn't in the minimal test list, but a null/mismatched data+dataLen is a real possibility this code never guarded against). Both handlers now check data && dataLen>0 before touching the payload, logging and no-oping otherwise, regardless of what the actual cause turns out to be - never dereference an SKSE message payload unchecked.

## 1.3.0 - 2026-08-27 - untested

### Added
- Theme registry (decisions doc S8/S10): AMF's original identity ships as the "Untarnished" theme; a new "MO2 Skyrim" theme, colours read directly from Mod Organizer 2's own real stylesheet (the Trosski "Transparent-Style-Skyrim" stylesheet, Transparent-Style-Skyrim-Trosski.qss - #b0b0b0 dominant grey, solid black background per the project's non-negotiable full-opacity rule), is now the DEFAULT for the current test per the author. Selectable live from the Framework Settings page; additively scans Data/SKSE/Plugins/ApocryphaMenuFramework/themes/*.ini for user-added themes, never overwriting another entry.
- AMF-owned per-save persistence channel (decisions doc S10): hooks kSaveGame/kPostLoadGame (the save's own filename is the message payload), writes/reads a plain-text sibling file next to the save mirroring co-save's SCOPING without its binary format. A shared key-value surface (SetValue/GetValue) any registered mod's page can use. A debug test harness on the Framework Settings page exercises the full round trip (set, save, quit, reload, confirm) without a Papyrus compiler.
- Papyrus native-function binding (decisions doc S3, Path A): AMF_Ping/AMF_SetTestValue/AMF_GetTestValue registered against the game's own Papyrus VM via RE::BSScript::IVirtualMachine::RegisterFunction, proving the native-binding path this project will use for AMF-hosted scripted events instead of embedding a second language.

### Notes
- Priority reset per the author 2026-08-27: other-mod-pipeline work (conversions, the rule-15 verdict backlog) is paused; this version is the direct build-out of the identity/persistence/scripting decisions from the same evening. A dedicated, isolated MO2 test instance (AMF-Test) was set up for this and future drastic-change testing, separate from Apostasy/SME.
- Papyrus round-trip verification is PARTIAL, updated: an adversarial sub-agent root-caused the "hang" - the compiler is a .NET Framework 2.0/CLR2 binary and this machine lacks .NET 3.5 (crashes instantly in the native hosting shim before any output is possible; looked like a hang under Start-Process -Wait). AMFTest.psc was successfully compiled to AMFTest.pex via a reflection-based bypass (loading the assembly into an already-running CLR4 host) and deployed into the test instance's Scripts folder. Durable fix recorded for next session: `DISM /Online /Enable-Feature /FeatureName:NetFx3 /All /NoRestart` (needs elevation, not available this session). What remains unverified: actually CALLING AMFTest.RunTest() in game - blocked by a second, unrelated finding: SendKeys cannot navigate Skyrim's own menus (DirectInput, not Win32 messages), so a fresh test instance's main menu could not be advanced past without a real play session.

## 1.2.1 - 2026-08-27 - untested

### Added
- M3 second half: the SMF-compatible export surface is LIVE - all 39 exports from the project's own export inventory (3. analyze mods\AMF export inventory\inventory.md), sized exactly to what this project's 11 SMF-integrated mods actually resolve, no more. AddSectionItem maps SMF's "Section/Item" path onto the native page registry (section = mod entry, item = page/tab). RegisterInpoutEvent/RegisterEventPriority (+ their Unregister siblings) implemented as real callback registries, wired into the input hook (SMF-compat input callbacks get first look at an event, ahead of ImGui) and a new compat::FireMenuEvent hook point for open/close/render events. Full cimgui text/layout/widget/query/draw-list forwarding to the embedded ImGui, including the one pOut case (igGetCursorScreenPos). GetMenuFrameworkVersion reports 1.2.

### Notes
- M3 is now functionally complete (registry + compat surface). NOT YET DONE: no existing mod has actually been pointed at AMF and tested - the pilot (Dragon's Eye Minimap's settings page, dual-resolve against both SMF and AMF, SMF disabled) is the next milestone action, not yet started.

## 1.2.0 - 2026-08-27 - untested

### Added
- M3 first half: the page registry is LIVE. AMF_RegisterPage accepts registrations (was an honest refusal since M0); the left pane lists one entry per registered mod; a mod with several pages renders them as TABS inside its one menu - the one-menu-per-mod rule implemented at the framework level. Thread-safe registration, render-thread snapshot iteration, duplicate and null-argument refusals logged.
- AMF API Demo menu (two pages, toggles/slider/button) registered through the public AMF_RegisterPage path itself - proves the registry end to end and gives gamepad navigation real content to select (the author, 1.1.2: nothing to select yet). Controlled by bShowApiDemo (INI, default 1).
- Second half of M3 (the SMF-compatible ig* export surface + dual-resolve client header) follows in the next versions, driven by the export inventory now being compiled.

## 1.1.3 - 2026-08-27 - untested

### Fixed
- Release-triggered game actions could fire from inside the menu (the author's 1.1.2 report: the shout command worked with the menu open and cascaded into another menu). Root cause: the stuck-key mitigation passed EVERY button release through to the game, and Skyrim's shout activates on RELEASE - so a button pressed inside the menu was consumed on the down-edge but completed as a shout on the up-edge. The hook now tracks which buttons the game actually saw go down; a release passes through only for a button held since before the menu opened, and both edges of anything pressed inside the menu are consumed.

## 1.1.2 - 2026-08-27 - untested

### Changed
- Window placement moved from free dragging to PRESET positions (the author: "preset positions, just like the minimap... standard position is in the center"). Position is centre-anchored from the display centre every frame (resolution-independent, ImGuiWindowFlags_NoMove); uWindowPreset INI key reserved (0 = centre) for the preset list to be worked out later. Size remains user-adjustable.

## 1.1.1 - 2026-08-27 - untested

### Fixed
- Software cursor flicker/jump/teleport (the author's 1.1.0 in-game report, suspected DPI): actually a two-source fight. The Win32 backend's fallback poll pushed the OS cursor position (which the game recentres at will) into ImGui's event queue every frame, while ours was only emitted on movement - so still frames teleported to the OS position, and event trickling let the two sources alternate across frames. Now the integrated position is emitted unconditionally every frame AFTER the backend (last writer wins) and ConfigInputTrickleEventQueue is off so all sources resolve within a single frame. DPI scaling stops mattering because the OS-space position never wins again.

## 1.1.0 - 2026-08-27 - untested

### Added
- M2 input capture (the user-validated top priority). One pattern-guarded call-hook at BSInputDeviceManager::PollInputDevices (SE 67315 / AE 68617 + 0x7B, twice-corroborated MIT prior art). While the menu is open: mouse movement, wheel, presses and thumbsticks are consumed - halting the camera, the scroll-zoom and movement - while button RELEASES pass through so a key held across the open transition can never stick down. Events are queued on the input thread and translated to ImGui on the render thread (1.87+ io.Add*Event API), with a software cursor integrated from mouse deltas. Escape closes the menu; the toggle key is handled inside the hook (the M1 event sink is retired).
- Framework settings page - the window's first real content, in the SMF two-pane structure the author specified (left pane lists menus - the framework itself is the only entry until M3's registry - right pane shows the selected page). Settings: explicit keyboard/controller input-mode toggle switch (the standing first-setting decision; wired live to ImGui nav flags), a Text size slider (live-applied), and the toggle-key readout. Settings persist to Data/SKSE/Plugins/ApocryphaMenuFramework.ini via plain file I/O (never the profile API); compiled defaults match the shipped INI exactly; uLogLevel honoured (trace default).
- Window now displays its own version string, sourced from the single CMake-declared version (the author read 1.0.2's version-less status text as a stale build; AMF_GetVersionString also stops hand-maintaining a literal, which had already drifted).

## 1.0.2 - 2026-08-27 - untested

### Changed
- Text 30% larger relative to the resolution scale (FontGlobalScale = uiScale * 1.30; widget geometry keeps the unboosted scale) and default window grown from 45%x60% to 55%x70% of the display - the author's 1.0.1 in-game feedback: "I want bigger text relative to the current size. And I want the overall size to be bigger."

## 1.0.1 - 2026-08-27 - untested

### Changed
- Default window size is now display-proportional (45% width x 60% height, FirstUseEver) instead of a fixed 520x340 box scaled by g_uiScale - the author's M1.1 in-game feedback: centred and bigger confirmed working, but "I want it to match the same size as the original" (SMF's large menu window). The exact proportion is a first calibration to be tuned against his next look.
- ImGui layout persistence moved from the default imgui.ini in the game CWD to a plugin-owned Data/SKSE/Plugins/ApocryphaMenuFramework_layout.ini (routed into MO2 overwrite by the VFS). Also guarantees the new default size isn't shadowed by previously saved geometry.

## 1.0.0 - 2026-08-27 - untested

### Added
- M1 render loop: ImGui embedded via the vcpkg port (dx11+win32 binding features), present hook on the 18-repo-corroborated site, and the DISPUTED D3D-init offset resolved by probing both candidates behind byte-pattern guards at runtime - the winner is logged per runtime. Full theme applied (true black, #F5F2E9, borders on every element, readable TextDisabled); game HUD opacity re-read per frame as one global multiplier; K toggles a display-only window. Every guard fails toward loaded-but-inert with the reason logged, never toward a crash. Corrections this milestone: vcpkg installs imgui backend headers FLAT (not backends/); CommonLibSSE-NG renamed BSRenderManager to BSGraphics::Renderer leaving a zero-byte tombstone header.
- M0 scaffold: original framework (not an SMF fork - licence rails in plan.md), CommonLibSSE-NG dual-runtime plugin skeleton, public C API with SMF_GetReservedKeyCodes as the first export, theme constants (true black / #F5F2E9 / borders everywhere) and the in-game-proven fHUDOpacity resolver ported from DEM. Rendering, input and the page registry are M1-M3.

### Changed
- Default window position is now centre-relative (display centre, centre pivot) - the same resolution-independence principle as LMU's map border, per the author after seeing the 16:35 capture where the unscaled window sat as a sliver in the top-left at 3200x1800. FirstUseEver, so player-moved windows keep their arrangement.
- Menu toggle moved from K to F1 (0x3B), decided during the first in-game smoke test: K collided with Dragon's Eye Minimap's rule-28 default the moment both ran, and F1 matches the established framework convention (SMF). Reserved-keys export updated to report F1. Rule-28 K/L defaults now read as mod-scoped; the framework is the arbiter and takes F1.
### Fixed
- M1.1, from the first smoke test: resolution-aware UI scaling. Font, style metrics and the default window size now scale by displayHeight/1080 (1.67x at 1800p), fixing the far-too-small window the author reported. Camera-still-moves is NOT fixed here - input capture is M2 and now its user-validated top priority.
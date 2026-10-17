#pragma once

#include <string>

// THE HELP BAR (2.1.5, the owner, 2026-10-07: the help text "should be limited ... within the right pane of AMF so that
// they can't leave that ... Or we should have a dedicated help text area in AMF that appears like below the bottom right
// pane but still inside AMF's menu" - he chose the bar, "a toggle just in case anybody doesn't like" it). A strip under
// the right pane, inside AMF's window, showing the highlighted option's help in the theme's help colour, wrapped to the
// pane's width - the role SkyUI's info line plays. Works the same for a mouse hover and a controller highlight. With
// [Display] bHelpBar=0 the help shows as a popup instead, wrapped to the right pane and kept inside it. Render thread only.

namespace helpbar
{
	// A page that has help to show asks for the bar while it draws; a_page names the page (a change clears the text).
	// The bar takes its room from the NEXT frame on, so asking every frame the page draws is the contract.
	void Want(const std::string& a_page);

	// The help of the item just submitted, shown when that item is hovered or holds the keyboard/controller highlight.
	// Call right after the item. a_help is shown as given (already translated); empty does nothing.
	void OfferForLastItem(const std::string& a_help);

	// 2.2.2: pages of SKSE mods (the SKSE Menu Framework API) do not call Want themselves - their help is a tooltip.
	// The Renderer brackets a registered mod page's render with EnterModPage / LeaveModPage; a page that did not ask for
	// the bar by itself (MCM, INI, TOML, FLICK and Prisma pages do) gets it here under a_pageKey, so its plain-text tooltips
	// (SetTooltip / SetItemTooltip) go to the bar instead of popping up. Same setting, same bar, same colour.
	void EnterModPage();
	void LeaveModPage(const std::string& a_pageKey);

	// Renderer: whether the content pane leaves room for the bar this frame (the setting on, and a page asked last frame),
	// the bar's own height, the right pane's rect (for the popup form), and drawing the bar at the cursor.
	bool Active();
	float Height();
	void NotePane(float a_minX, float a_minY, float a_maxX, float a_maxY);
	void Draw(float a_width);
}

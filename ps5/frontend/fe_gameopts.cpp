// fceuxs5 frontend: a game's options -- the ROM hack it starts with, its box art again, play (fe_menu.h).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_menu.h"

#include "fe_i18n.h"
#include "fe_patch.h"
#include "fe_text.h"
#include "fe_ui.h"

#include "OrbisPaths.h"
#include "ProsperoVideo.h"

#include <cstring>
#include <vector>

namespace fe
{
namespace
{
// The ROM hack row's choices: "auto" (the same-name patch when Auto-apply is on), "" (the original game), then
// every patch found for the game.
struct Choices
{
	std::vector<std::string> values;
	int current = 0;
};

Choices Load(const std::string& rom)
{
	Choices c;
	c.values.push_back("auto");
	c.values.push_back("");
	for (const std::string& p : FindPatches(rom))
		c.values.push_back(p);
	std::string chosen;
	if (GetPatchChoice(rom, &chosen))
	{
		for (size_t i = 0; i < c.values.size(); i++)
			if (c.values[i] == chosen)
				c.current = int(i);
		if (c.current == 0 && !chosen.empty())
		{
			c.values.push_back(chosen); // a patch chosen before that the search no longer finds (moved?)
			c.current = int(c.values.size() - 1);
		}
		else if (chosen.empty())
			c.current = 1;
	}
	return c;
}

std::string Label(const Choices& c, const std::string& rom)
{
	const std::string& v = c.values[size_t(c.current)];
	if (v == "auto")
	{
		const std::string p = PatchForGame(rom);
		return std::string(T("Auto")) + " (" + (p.empty() ? T("none found") : PatchLabel(p)) + ")";
	}
	if (v.empty())
		return T("Off: the original game");
	return PatchLabel(v);
}
} // namespace

GameAction GameOptions(const GameInfo& game, bool* refetch_cover)
{
	*refetch_cover = false;
	const std::vector<uint32_t> behind(ps5video::Surface(), ps5video::Surface() + size_t(ps5video::kWidth) * ps5video::kHeight);
	Choices ch = Load(game.path);
	enum
	{
		R_PLAY,
		R_HACK,
		R_FOUND,
		R_COVER,
		R_WHERE,
		R_COUNT
	};
	int sel = R_PLAY;
	ui::NavReader nav;
	for (;;)
	{
		const ui::Nav in = nav.Read();
		if (in.back || in.square)
			return GameAction::None;
		if (in.up)
			sel = (sel + R_COUNT - 1) % R_COUNT;
		if (in.down)
			sel = (sel + 1) % R_COUNT;
		if (sel == R_FOUND || sel == R_WHERE)
			sel = in.up ? sel - 1 : sel + 1; // information rows are skipped
		sel = (sel + R_COUNT) % R_COUNT;
		if (sel == R_HACK && (in.left || in.right || in.ok))
		{
			const int n = int(ch.values.size());
			ch.current = (ch.current + (in.left ? n - 1 : 1)) % n;
			SetPatchForGame(game.path, ch.values[size_t(ch.current)]);
			OrbisLog("[patch] %s: %s", game.path.c_str(), ch.values[size_t(ch.current)].c_str());
		}
		else if (in.ok && sel == R_PLAY)
			return GameAction::Play;
		else if (in.ok && sel == R_COVER)
		{
			*refetch_cover = true;
			return GameAction::None;
		}
		memcpy(ps5video::Surface(), behind.data(), behind.size() * 4);
		ps5video::DarkenRect(0, 0, ps5video::kWidth, ps5video::kHeight);
		std::vector<ui::Row> rows(R_COUNT);
		rows[R_PLAY] = {T("Play"), ""};
		rows[R_HACK] = {T("ROM hack"), Label(ch, game.path)};
		char found[96];
		snprintf(found, sizeof found, T("%d patch(es) found for this game"), int(ch.values.size()) - 2);
		rows[R_FOUND] = {found, "", false, true};
		rows[R_COVER] = {T("Download the box art again"), ""};
		rows[R_WHERE] = {T("Patches go next to the game, or in /data/fceuxs5/patches/"), "", false, true};
		ui::DrawOptionBox(game.title.c_str(), rows, sel, false);
		ui::Present();
	}
}
} // namespace fe

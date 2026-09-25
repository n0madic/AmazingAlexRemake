// The remake's own view dictionaries (docs/06 §4): views the original scene XML has no entry for, written
// in the same dictionary form so the views are built exactly like the imported ones.
#pragma once

#include "aa/data/json.h"

namespace aa::ui::remake {

// ButtonMusic of the main menu's SettingsSlider: a small toggle like ButtonAudio, the note sprites
// (BUTTON_SMALL_MUSIC / _OFF, tools/remake_ui.py).
aa::data::JsonDoc mainMenuMusicButton();

// ButtonMusic of the pause sidebar, under ButtonAudio (both keyed off SidebarBackground's centre like the
// original's buttons; the percentages are of the screen height).
aa::data::JsonDoc gameMusicButton();
// ButtonAudio's Relative.Y for the pause sidebar — the original's 32 % moved up to make room for the note.
constexpr float kGameAudioButtonY = 22.0f;

// The level tip in the game (docs/06 §3): the level's TEXT_LEVEL_TIP_* at the bottom left of the play field,
// left of the toolbox strip — a dark panel (TipPanel) holding the label (LabelTip: FONT_4, the `*highlighted*`
// words tinted yellow), and the small info button (ButtonTip) that shows it again. GameView places all three
// itself from the toolbox strip's rectangle.
aa::data::JsonDoc gameTipPanel();
aa::data::JsonDoc gameTipLabel();
aa::data::JsonDoc gameTipButton();

}  // namespace aa::ui::remake

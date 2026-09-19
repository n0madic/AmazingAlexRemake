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

}  // namespace aa::ui::remake

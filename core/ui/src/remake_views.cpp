#include "aa/ui/remake_views.h"

namespace aa::ui::remake {

namespace {

constexpr const char* kMusicImages =
    R"("ImageBackground": "BUTTON_SMALL_BASE", "ImageStateNormalOn": "BUTTON_SMALL_MUSIC", "ImageStateNormalOff": "BUTTON_SMALL_MUSIC_OFF")";

}  // namespace

aa::data::JsonDoc mainMenuMusicButton() {
    return aa::data::JsonDoc(std::string("{") + kMusicImages + "}", "MainMenuScene:ButtonMusic");
}

aa::data::JsonDoc gameMusicButton() {
    return aa::data::JsonDoc(std::string(R"({"Relative": {"X": 0.0, "Y": 36.0}, "Anchor": {)"
                                         R"("H": {"Self": "HCENTER", "View": {"Name": "SidebarBackground", "Target": "HCENTER"}}, )"
                                         R"("V": {"Self": "VCENTER", "View": {"Name": "SidebarBackground", "Target": "VCENTER"}}}, )") +
                                 kMusicImages + "}",
                             "GameScene:ButtonMusic");
}

aa::data::JsonDoc gameTipPanel() { return aa::data::JsonDoc(R"({"BackgroundColor": {"R": 0, "G": 0, "B": 0, "A": 110}})", "GameScene:TipPanel"); }

aa::data::JsonDoc gameTipLabel() {
    return aa::data::JsonDoc(R"({"Font": "FONT_4", "FontAnchorH": "LEFT", "AutoResizeH": true, "HilightColor": {"R": 255, "G": 214, "B": 0, "A": 255}})",
                             "GameScene:LabelTip");
}

aa::data::JsonDoc gameTipButton() {
    return aa::data::JsonDoc(R"({"ImageBackground": "BUTTON_SMALL_BASE", "ImageStateNormal": "BUTTON_SMALL_INFO"})", "GameScene:ButtonTip");
}

}  // namespace aa::ui::remake

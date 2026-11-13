#include "ArchivesPage.h"

#include "SystemDialogs.h"
#include "Text.h"

#include <format>

namespace scriptroom::gui {

ArchivesPage::ArchivesPage(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    Body = Ui.frame(Parent, {.Fill = Stretch::Both, .Expand = true});
    const Node Scroll = Body.add(Node::create("scroll"), {.Fill = Stretch::Both, .Expand = true});
    const Node Content = Ui.frame(Scroll, {.Fill = Stretch::X}, std::nullopt, 20);

    Ui.label(Content, {.Fill = Stretch::X}, "玩家存档", {.Scale = TitleText, .Bold = true});
    Ui.label(Content, {.Fill = Stretch::X, .PadTop = 5}, "共用一套魔兽，每位玩家独立保存",
             {.Color = &Palette::Muted, .Scale = SmallText});

    Details = Ui.frame(Content, {.Fill = Stretch::X, .PadTop = 20});
    for (std::size_t Player = 0; Player < Players.size(); ++Player) {
        build_player(Details, Player);
    }
    Ui.button(Details, {.Side = Edge::Top, .Width = 150, .Height = 26}, "打开存档目录", "FILE_FOLDER", [&Model] {
        if (const std::optional<Config> Settings = Model.resolved_config()) {
            open_in_explorer(Settings->Archives);
        }
    });
}

void ArchivesPage::build_player(Node Parent, std::size_t Player) {
    const Builder &Ui = Services.Ui;
    const Node Card = Ui.card(Parent, {.Fill = Stretch::X, .PadBottom = 12}, 18);
    const Node Title = Ui.frame(Card, {.Fill = Stretch::X, .PadBottom = 14});
    Ui.label(Title, {.Side = Edge::Left, .PadRight = 6}, "", {.Color = &Palette::Accent}).str("icon", "PACKAGE");
    Ui.label(Title, {.Side = Edge::Left}, std::format("玩家 {}", Player), {.Bold = true});

    Ui.label(Card, {.Fill = Stretch::X}, "客户端 INI", {.Color = &Palette::Muted, .Scale = SmallText});
    Card.add(Players[Player].ClientIni.node(), {.Fill = Stretch::X, .PadBottom = 8});
    Ui.label(Card, {.Fill = Stretch::X}, "云端存档", {.Color = &Palette::Muted, .Scale = SmallText});
    Card.add(Players[Player].CloudArchive.node(), {.Fill = Stretch::X});
    Ui.style().watch([this, Player](const Palette &Colors) {
        Players[Player].ClientIni.set_color(Colors.Text);
        Players[Player].CloudArchive.set_color(Colors.Text);
    });
}

void ArchivesPage::sync(const std::optional<Config> &Resolved) {
    Details.set_visible(Resolved.has_value());
    if (!Resolved) {
        return;
    }
    for (std::size_t Player = 0; Player < Players.size(); ++Player) {
        const fs::path &Archives = Resolved->Archives;
        Players[Player].ClientIni.set_text(
            path_to_utf8(Archives / L"clients" / std::format(L"player-{}.ini", Player)));
        Players[Player].CloudArchive.set_text(path_to_utf8(Archives / std::format(L"slot-{}.archive", Player)));
    }
}

} // namespace scriptroom::gui

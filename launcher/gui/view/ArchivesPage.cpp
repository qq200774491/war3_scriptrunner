#include "ArchivesPage.h"

#include "Geometry.h"
#include "SystemDialogs.h"
#include "Text.h"

#include <format>

namespace scriptroom::gui {

ArchivesPage::ArchivesPage(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    Body = Ui.frame(Parent, {.Fill = Stretch::Both, .Expand = true});

    const Node Toolbar = Ui.frame(Body, {.Side = Edge::Top, .Fill = Stretch::X, .Height = geometry::ToolbarHeight});
    Ui.label(Toolbar, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 20}, "玩家存档", {.Scale = TitleText, .Bold = true});
    OpenFolder = Ui.pill(Toolbar, {.Side = Edge::Right, .PadRight = 16}, "打开存档目录", Tone::Secondary, [&Model] {
        if (const std::optional<Config> Settings = Model.resolved_config()) {
            open_in_explorer(Settings->Archives);
        }
    });

    const Node Scroll = Body.add(Node::create("scroll"), {.Fill = Stretch::Both, .Expand = true});
    const Node Content = Ui.frame(Scroll, {.Fill = Stretch::X}, std::nullopt, 24);
    Details = Ui.frame(Content, {.Fill = Stretch::X});
    for (std::size_t Player = 0; Player < Players.size(); ++Player) {
        build_player(Details, Player);
    }
    Unresolved = Ui.label(Content, {.Fill = Stretch::X}, "填好存档目录后，这里显示每位玩家的存档位置",
                          {.Color = &Palette::Text3});
}

void ArchivesPage::build_player(Node Parent, std::size_t Player) {
    const Builder &Ui = Services.Ui;
    const Node Section = Ui.frame(Parent, {.Fill = Stretch::X, .PadBottom = 26});
    Sections[Player] = Section;
    Ui.label(Section, {.Fill = Stretch::X, .PadBottom = 10}, std::format("玩家 {}", Player), {.Bold = true});
    Ui.label(Section, {.Fill = Stretch::X, .PadBottom = 2}, "客户端 INI", {.Color = &Palette::Text3});
    Section.add(Players[Player].ClientIni.node(), {.Fill = Stretch::X, .PadBottom = 12});
    Ui.label(Section, {.Fill = Stretch::X, .PadBottom = 2}, "云端存档", {.Color = &Palette::Text3});
    Section.add(Players[Player].CloudArchive.node(), {.Fill = Stretch::X});
    Ui.style().watch([this, Player](const Palette &Colors) {
        Players[Player].ClientIni.set_color(Colors.Text);
        Players[Player].CloudArchive.set_color(Colors.Text);
    });
}

void ArchivesPage::sync(const std::optional<Config> &Resolved) {
    Details.set_visible(Resolved.has_value());
    Unresolved.set_visible(!Resolved.has_value());
    OpenFolder.set_enabled(Resolved.has_value());
    if (!Resolved) {
        return;
    }
    for (std::size_t Player = 0; Player < Players.size(); ++Player) {
        Sections[Player].set_visible(Player < Resolved->Clients);
        const fs::path &Archives = Resolved->Archives;
        Players[Player].ClientIni.set_text(
            path_to_utf8(Archives / L"clients" / std::format(L"player-{}.ini", Player)));
        Players[Player].CloudArchive.set_text(path_to_utf8(Archives / std::format(L"slot-{}.archive", Player)));
    }
}

} // namespace scriptroom::gui

#include "SettingsPage.h"

#include "AccentPicker.h"
#include "DisplayPath.h"
#include "Geometry.h"
#include "SystemDialogs.h"
#include "Text.h"

namespace scriptroom::gui {

/// 一行路径的描述。
struct SettingsPage::PathRow {
    PathField Field;
    const char *Label;
    bool Directory;
};

namespace {

constexpr int LabelWidth = 110;

fs::path start_folder_of(const std::string &Text) {
    std::error_code Ignored;
    const fs::path Current = path_from_utf8(Text);
    if (fs::is_directory(Current, Ignored)) {
        return Current;
    }
    return Current.parent_path();
}

} // namespace

SettingsPage::SettingsPage(const ViewServices &Context, Node Parent) : Services(Context) {
    const Builder &Ui = Services.Ui;
    Body = Ui.frame(Parent, {.Fill = Stretch::Both, .Expand = true});
    build_toolbar();

    const Node Scroll = Body.add(Node::create("scroll"), {.Fill = Stretch::Both, .Expand = true});
    const Node Content = Ui.frame(Scroll, {.Fill = Stretch::X}, std::nullopt, 24);

    const auto build_paths = [&](std::initializer_list<PathRow> Rows) {
        const Node Group = group(Content);
        for (const PathRow &Spec : Rows) {
            build_path_row(Group, Spec);
        }
    };
    build_paths({{PathField::Map, "地图文件", false},
                 {PathField::LocalEntry, "客户端入口", false},
                 {PathField::CloudScripts, "云脚本目录", true}});
    build_paths({{PathField::Archives, "存档目录", true}});
    build_paths({{PathField::Editor, "KKWE 编辑器", true},
                 {PathField::SourceGame, "原魔兽目录", true},
                 {PathField::Game, "共用测试魔兽", true}});
    build_limits(Content);
    build_appearance(Content);

    ConfigPathLabel = Ui.label(Content, {.Fill = Stretch::X, .PadTop = 4}, "", {.Color = &Palette::Text3});
    SeenRevision = Services.Model.draft_revision();
}

void SettingsPage::build_toolbar() {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Toolbar = Ui.frame(Body, {.Side = Edge::Top, .Fill = Stretch::X, .Height = geometry::ToolbarHeight});
    Ui.label(Toolbar, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 20}, "设置", {.Scale = TitleText, .Bold = true});
    Ui.pill(Toolbar, {.Side = Edge::Right, .PadRight = 16}, "保存配置", Tone::Primary, [&Model] { Model.save_config(); });
    Ui.pill(Toolbar, {.Side = Edge::Right, .PadRight = 8}, "载入配置…", Tone::Secondary, [this] { load_dialog(); });
    Ui.pill(Toolbar, {.Side = Edge::Right, .PadRight = 4}, "另存为…", Tone::Secondary, [this] { save_as_dialog(); });
}

Node SettingsPage::group(Node Content) const {
    return Services.Ui.frame(Content, {.Fill = Stretch::X, .PadBottom = 22});
}

void SettingsPage::build_path_row(Node Group, const PathRow &Spec) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const auto Index = static_cast<std::size_t>(Spec.Field);
    const Node Row = Ui.frame(Group, {.Fill = Stretch::X, .PadBottom = 10});
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = LabelWidth}, Spec.Label, {.Color = &Palette::Text2});
    Ui.icon_button(Row, {.Side = Edge::Right, .PadLeft = 6}, "FILE_FOLDER", std::string("浏览") + Spec.Label,
                   [this, Field = Spec.Field, Title = Spec.Label, Directory = Spec.Directory] {
                       browse(Field, Title, Directory);
                   });
    PathFields[Index] = Ui.text_field(
        Row, {.Side = Edge::Left, .Fill = Stretch::X, .Expand = true, .Height = geometry::FieldHeight},
        display_path(Model.draft().path(Spec.Field)), [&Model, Field = Spec.Field](const std::string &Text) {
            Model.draft().path(Field) = edited_path(Text, Model.draft().path(Field));
        });
}

void SettingsPage::build_limits(Node Content) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const Node Group = group(Content);
    const Node Row = Ui.frame(Group, {.Fill = Stretch::X, .PadBottom = 10});
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = LabelWidth}, "服务时长", {.Color = &Palette::Text2});
    SecondsField = Ui.text_field(Row, {.Side = Edge::Left, .Width = 100, .Height = geometry::FieldHeight},
                                 Model.draft().Seconds, [&Model](const std::string &Text) { Model.draft().Seconds = Text; });
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 8, .PadRight = 32}, "秒", {.Color = &Palette::Text3});
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y, .PadRight = 12}, "启动超时", {.Color = &Palette::Text2});
    TimeoutField = Ui.text_field(Row, {.Side = Edge::Left, .Width = 100, .Height = geometry::FieldHeight},
                                 Model.draft().Timeout, [&Model](const std::string &Text) { Model.draft().Timeout = Text; });
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 8}, "秒", {.Color = &Palette::Text3});

    const Node Clients = Ui.frame(Group, {.Fill = Stretch::X});
    Ui.label(Clients, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = LabelWidth}, "客户端个数", {.Color = &Palette::Text2});
    ClientsPicker = Segmented(
        Ui, Clients, {.Side = Edge::Left}, count_labels(MinClients, MaxClients),
        [&Model] { return Model.draft().Clients - MinClients; },
        [&Model](std::size_t Index) { Model.set_clients(MinClients + Index); }, 44);
    Ui.label(Clients, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 12}, "玩家 0 建房，其余依次加入",
             {.Color = &Palette::Text3});
}

void SettingsPage::build_appearance(Node Content) {
    const Builder &Ui = Services.Ui;
    const Node Row = Ui.frame(group(Content), {.Fill = Stretch::X});
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = LabelWidth}, "主题色", {.Color = &Palette::Text2});
    add_accent_picker(Ui, Row, {.Side = Edge::Left}, Services.Model);
    ThemeName = Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y, .PadLeft = 12}, "");
}

HWND SettingsPage::owner() const {
    return Services.Owner ? Services.Owner() : nullptr;
}

void SettingsPage::browse(PathField Field, const char *Title, bool Directory) {
    FileDialogOptions Options;
    Options.Title = Title;
    Options.StartFolder = start_folder_of(Services.Model.draft().path(Field));
    const std::optional<fs::path> Chosen =
        Directory ? pick_folder(owner(), Options) : pick_file(owner(), Options);
    if (Chosen) {
        const std::string Text = path_to_utf8(*Chosen);
        Services.Model.draft().path(Field) = Text;
        PathFields[static_cast<std::size_t>(Field)].str("value", display_path(Text));
    }
}

void SettingsPage::load_dialog() {
    FileDialogOptions Options;
    Options.Title = "载入配置";
    Options.FilterLabel = "JSON";
    Options.FilterExtension = "json";
    Options.StartFolder = Services.Model.config_path().parent_path();
    if (const auto Chosen = pick_file(owner(), Options)) {
        Services.Model.load_config(*Chosen);
    }
}

void SettingsPage::save_as_dialog() {
    FileDialogOptions Options;
    Options.Title = "另存为";
    Options.InitialName = "本地测试配置.json";
    Options.FilterLabel = "JSON";
    Options.FilterExtension = "json";
    Options.StartFolder = Services.Model.config_path().parent_path();
    if (const auto Chosen = pick_save_file(owner(), Options)) {
        Services.Model.save_config_as(*Chosen);
    }
}

void SettingsPage::push_draft_to_controls() {
    const ConfigDraft &Draft = Services.Model.draft();
    for (std::size_t Index = 0; Index < PathFieldCount; ++Index) {
        PathFields[Index].str("value", display_path(Draft.Paths[Index]));
    }
    SecondsField.str("value", Draft.Seconds);
    TimeoutField.str("value", Draft.Timeout);
}

void SettingsPage::sync() {
    const AppModel &Model = Services.Model;
    if (SeenRevision != Model.draft_revision()) {
        SeenRevision = Model.draft_revision();
        push_draft_to_controls();
    }
    ThemeName.set_text(theme_label(Model.theme()));
    ConfigPathLabel.set_text("配置文件  " + display_path(path_to_utf8(Model.config_path())));
    if (ShownClients != Model.draft().Clients) {
        ShownClients = Model.draft().Clients;
        ClientsPicker.refresh();
    }
}

} // namespace scriptroom::gui

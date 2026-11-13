#include "SettingsPage.h"

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

constexpr int FieldHeight = 26;

void group_title(const Builder &Ui, Node Parent, std::string_view Text, int Top) {
    Ui.label(Parent, {.Fill = Stretch::X, .PadTop = Top, .PadBottom = 7}, Text, {.Scale = SmallText, .Bold = true});
}

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
    AppModel &Model = Services.Model;
    Body = Ui.frame(Parent, {.Fill = Stretch::Both, .Expand = true});

    // 保存 / 载入 / 另存为固定在页面底部，表单内容较长时只滚动表单。
    const Node Actions = Ui.frame(Body, {.Side = Edge::Bottom, .Fill = Stretch::X}, std::nullopt, 16);
    const Node Buttons = Ui.frame(Actions, {.Fill = Stretch::X});
    Ui.button(Buttons, {.Side = Edge::Left, .PadRight = 8, .Width = 100, .Height = FieldHeight}, "保存配置", "FILE_TICK",
              [&Model] { Model.save_config(); });
    Ui.button(Buttons, {.Side = Edge::Left, .PadRight = 8, .Width = 116, .Height = FieldHeight}, "载入配置…", "FILE_FOLDER",
              [this] { load_dialog(); });
    Ui.button(Buttons, {.Side = Edge::Left, .Width = 100, .Height = FieldHeight}, "另存为…", "EXPORT",
              [this] { save_as_dialog(); });
    ConfigPathLabel = Ui.label(Actions, {.Fill = Stretch::X, .PadTop = 8}, "", {.Color = &Palette::Muted, .Scale = SmallText});
    Ui.divider(Body, {.Side = Edge::Bottom, .Fill = Stretch::X});

    const Node Scroll = Body.add(Node::create("scroll"), {.Fill = Stretch::Both, .Expand = true});
    const Node Content = Ui.frame(Scroll, {.Fill = Stretch::X}, std::nullopt, 20);

    Ui.label(Content, {.Fill = Stretch::X}, "路径与配置", {.Scale = TitleText, .Bold = true});
    Ui.label(Content, {.Fill = Stretch::X, .PadTop = 4}, "地图、脚本与存档的运行位置",
             {.Color = &Palette::Muted, .Scale = SmallText});

    const auto build_group = [&](std::string_view Title, int Top, std::initializer_list<PathRow> Rows,
                                 const char *Hint) {
        group_title(Ui, Content, Title, Top);
        const Node Card = Ui.card(Content, {.Fill = Stretch::X}, 12);
        std::size_t Remaining = Rows.size();
        for (const PathRow &Spec : Rows) {
            build_path_row(Card, Spec, --Remaining == 0 && Hint == nullptr);
        }
        if (Hint != nullptr) {
            Ui.label(Card, {.Fill = Stretch::X}, Hint, {.Color = &Palette::Muted, .Scale = SmallText});
        }
    };
    build_group("地图与脚本", 18,
                {{PathField::Map, "地图文件", false},
                 {PathField::LocalEntry, "客户端入口", false},
                 {PathField::CloudScripts, "云脚本目录", true}},
                nullptr);
    build_group("存档", 12, {{PathField::Archives, "存档目录", true}}, "按玩家分别保存，重新启动后继续读取");
    build_group("运行环境", 12,
                {{PathField::Editor, "KKWE 编辑器", true},
                 {PathField::SourceGame, "原魔兽目录", true},
                 {PathField::Game, "共用测试魔兽", true}},
                nullptr);

    group_title(Ui, Content, "运行时限", 12);
    const Node Limits = Ui.card(Content, {.Fill = Stretch::X}, 12);
    const Node Row = Ui.frame(Limits, {.Fill = Stretch::X});
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y}, "服务时长（秒）");
    SecondsField = Ui.text_field(Row, {.Side = Edge::Left, .PadLeft = 8, .PadRight = 20, .Width = 90, .Height = FieldHeight},
                                 Model.draft().Seconds, [&Model](const std::string &Text) { Model.draft().Seconds = Text; });
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y}, "启动超时（秒）");
    TimeoutField = Ui.text_field(Row, {.Side = Edge::Left, .PadLeft = 8, .Width = 90, .Height = FieldHeight},
                                 Model.draft().Timeout, [&Model](const std::string &Text) { Model.draft().Timeout = Text; });

    SeenRevision = Model.draft_revision();
}

void SettingsPage::build_path_row(Node Card, const PathRow &Spec, bool Last) {
    const Builder &Ui = Services.Ui;
    AppModel &Model = Services.Model;
    const auto Index = static_cast<std::size_t>(Spec.Field);
    const Node Row = Ui.frame(Card, {.Fill = Stretch::X, .PadBottom = Last ? 0 : 8});
    Ui.label(Row, {.Side = Edge::Left, .Fill = Stretch::Y, .Width = 96}, Spec.Label);
    Ui.button(Row, {.Side = Edge::Right, .PadLeft = 8, .Width = 34, .Height = FieldHeight}, "", "FILE_FOLDER",
              [this, Field = Spec.Field, Title = Spec.Label, Directory = Spec.Directory] { browse(Field, Title, Directory); })
        .str("description", std::string("浏览") + Spec.Label);
    PathFields[Index] = Ui.text_field(
        Row, {.Side = Edge::Left, .Fill = Stretch::X, .Expand = true, .Height = FieldHeight}, Model.draft().path(Spec.Field),
        [&Model, Field = Spec.Field](const std::string &Text) { Model.draft().path(Field) = Text; });
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
        PathFields[static_cast<std::size_t>(Field)].str("value", Text);
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
        PathFields[Index].str("value", Draft.Paths[Index]);
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
    ConfigPathLabel.set_text(path_to_utf8(Model.config_path()));
}

} // namespace scriptroom::gui

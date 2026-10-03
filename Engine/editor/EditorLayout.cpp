#include "EditorLayout.h"

#include <cstdlib>  
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// ════════════════════════════════════════════════════════════════════
// このファイルの中だけで使うもの
// ════════════════════════════════════════════════════════════════════
namespace {

#ifdef USE_IMGUI

	/// <summary>enum を配列添字に変換する（キャストを毎回書かないため）</summary>
	constexpr uint32_t ToIndex(EditorPanel panel) {
		return static_cast<uint32_t>(panel);
	}

	// すべてのパネルに共通のフラグ
	//   NoBringToFrontOnFocus を入れないと、クリックのたびに重なり順が変わる
	constexpr ImGuiWindowFlags kPanelFlags =
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBringToFrontOnFocus;

	// パネルの見出し（EditorPanel の並び順と一致させること）
	const char* kPanelTitles[] = {
		"ツールバー",     // Toolbar
		"ビュー",         // View
		"オブジェクト",   // Hierarchy
		"詳細",           // Inspector
		"アセット",       // Asset  
		"デバッグ",       // Debug
		"設定",           // Settings
	};

	static_assert(
		_countof(kPanelTitles) == static_cast<uint32_t>(EditorPanel::kMaxCount),
		"kPanelTitles の数が EditorPanel と一致していません");

	// ────────────────────────────────────────────────────────────────
	// パネルごとの追加フラグ
	// ────────────────────────────────────────────────────────────────
	ImGuiWindowFlags GetExtraFlags(EditorPanel panel) {
		switch (panel) {
		case EditorPanel::Toolbar:
			// 見出しもスクロールも要らない、1行だけの横長バーにする
			return ImGuiWindowFlags_NoTitleBar
				| ImGuiWindowFlags_NoScrollbar
				| ImGuiWindowFlags_NoScrollWithMouse;

		default:
			return ImGuiWindowFlags_None;
		}
	}

#endif

}   // namespace

// ════════════════════════════════════════════════════════════════════
// 全パネルの矩形を計算する
//   中央（ビュー）は「残り全部」なので、リサイズに自動で追従する
// ════════════════════════════════════════════════════════════════════
void EditorLayout::Update() {
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiStyle& style = ImGui::GetStyle();

	const float originX = viewport->WorkPos.x;
	const float originY = viewport->WorkPos.y;
	const float totalWidth = viewport->WorkSize.x;
	const float totalHeight = viewport->WorkSize.y;

	// ツールバーはタイトルバーを消しているので、1行分 ＋ 上下パディング
	toolbarHeight_ = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;

	// ── 行1：ツールバー（全幅）
	rects_[ToIndex(EditorPanel::Toolbar)] = { originX, originY, totalWidth, toolbarHeight_ };

	// ── 行3：下段（全幅を 3 分割。中央が残り幅）
	const float bottomY = originY + totalHeight - kBottomHeight;
	const float debugWidth = totalWidth - kAssetWidth - kSettingsWidth;

	rects_[ToIndex(EditorPanel::Asset)] =
	{ originX, bottomY, kAssetWidth, kBottomHeight };
	rects_[ToIndex(EditorPanel::Debug)] =
	{ originX + kAssetWidth, bottomY, debugWidth, kBottomHeight };
	rects_[ToIndex(EditorPanel::Settings)] =
	{ originX + kAssetWidth + debugWidth, bottomY, kSettingsWidth, kBottomHeight };

	// ── 行2：中段（ツールバーと下段のあいだを 3 分割。中央が残り幅）
	const float middleY = originY + toolbarHeight_;
	const float middleHeight = totalHeight - toolbarHeight_ - kBottomHeight;
	const float viewWidth = totalWidth - kHierarchyWidth - kInspectorWidth;

	rects_[ToIndex(EditorPanel::Hierarchy)] =
	{ originX, middleY, kHierarchyWidth, middleHeight };
	rects_[ToIndex(EditorPanel::View)] =
	{ originX + kHierarchyWidth, middleY, viewWidth, middleHeight };
	rects_[ToIndex(EditorPanel::Inspector)] =
	{ originX + kHierarchyWidth + viewWidth, middleY, kInspectorWidth, middleHeight };
}

// ════════════════════════════════════════════════════════════════════
// パネルを開く
// ════════════════════════════════════════════════════════════════════
bool EditorLayout::BeginPanel(EditorPanel panel) {
#ifdef USE_IMGUI

	const uint32_t index = static_cast<uint32_t>(panel);
	const Rect& rect = rects_[index];

	// 位置とサイズを毎フレーム指定する
	//   SetNextWindowPos / Size は「次に Begin するウィンドウ」に効く
	ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
	ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height));

	// 共通フラグ + パネルごとの追加フラグ
	return ImGui::Begin(kPanelTitles[index], nullptr,
		kPanelFlags | GetExtraFlags(panel));

#else
	(void)panel;
	return false;
#endif
}

// ════════════════════════════════════════════════════════════════════
// パネルを閉じる
// ════════════════════════════════════════════════════════════════════
void EditorLayout::EndPanel() {
#ifdef USE_IMGUI
	ImGui::End();
#endif
}

// ════════════════════════════════════════════════════════════════════
// 点がパネルの中にあるか
//   ImGui に関係なく成立する純粋な矩形判定なので、#ifdef は要らない
// ════════════════════════════════════════════════════════════════════
bool EditorLayout::IsInsidePanel(EditorPanel panel, float x, float y) const {

	const Rect& rect = rects_[static_cast<uint32_t>(panel)];

	// 左と上は「以上」、右と下は「未満」
	//   こうしないと、隣り合うパネルが境界線を取り合う
	return x >= rect.x && x < rect.x + rect.width
		&& y >= rect.y && y < rect.y + rect.height;
}
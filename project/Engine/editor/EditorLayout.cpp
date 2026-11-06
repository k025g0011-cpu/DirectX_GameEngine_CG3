#include "EditorLayout.h"

#include <cstdlib>
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
//   DockBuilder（既定レイアウトの構築）は内部 API 側にある。
//   公式に安定を保証しない区分だが、既定配置を組む標準的な手段
#include "externals/imgui/imgui_internal.h"
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

	// ────────────────────────────────────────────────────────────────
	// ドックスペースの器になるウィンドウのフラグ
	//   中身を一切描かない。パネルを収める場所を用意するだけ
	// ────────────────────────────────────────────────────────────────
	constexpr ImGuiWindowFlags kHostFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |   // 常に最背面に置く
		ImGuiWindowFlags_NoNavFocus |              // キーボード操作の対象にしない
		ImGuiWindowFlags_NoDocking;                // 器自体はドッキングしない

	// ────────────────────────────────────────────────────────────────
	// 固定配置のパネル（ツールバーのみ）
	// ────────────────────────────────────────────────────────────────
	constexpr ImGuiWindowFlags kFixedPanelFlags =
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoDocking;   // ドックスペースに吸い込まれないようにする

	// ────────────────────────────────────────────────────────────────
	// ドッキング可能なパネル
	//   NoMove と NoResize を外すのが肝。
	//   どちらかが付いていると ImGui は「動かせないウィンドウ」と判断し、
	//   ドッキングもサイズ変更もできない
	// ────────────────────────────────────────────────────────────────
	constexpr ImGuiWindowFlags kDockablePanelFlags =
		ImGuiWindowFlags_NoCollapse;

	// パネルの見出し（EditorPanel の並び順と一致させること）
	//   imgui.ini と DockBuilder はこの文字列でパネルを識別する。
	//   名前を変えると、保存済みの配置と既定配置の両方が外れる
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
// ホストウィンドウとドックスペースを発行する
//   ツールバーの矩形だけは自分で決め、残りの配置は ImGui に任せる
// ════════════════════════════════════════════════════════════════════
void EditorLayout::Update() {
#ifdef USE_IMGUI
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiStyle& style = ImGui::GetStyle();

	// ツールバーはタイトルバーを消しているので、1行分 ＋ 上下パディング
	toolbarHeight_ = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;

	// ── ツールバーの矩形だけは、こちらで決める（全幅・最上段）
	rects_[ToIndex(EditorPanel::Toolbar)] =
	{ viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, toolbarHeight_ };

	// ────────────────────────────────────────────────────────────────
	// 残りの領域を覆うホストウィンドウ
	//   このウィンドウ自体は何も描かない。ドックスペースを置くための器
	// ────────────────────────────────────────────────────────────────
	const ImVec2 hostPosition(
		viewport->WorkPos.x, viewport->WorkPos.y + toolbarHeight_);
	const ImVec2 hostSize(
		viewport->WorkSize.x, viewport->WorkSize.y - toolbarHeight_);

	ImGui::SetNextWindowPos(hostPosition);
	ImGui::SetNextWindowSize(hostSize);

	//   器なので、丸み・枠線・余白を全部消して画面にぴったり合わせる
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::Begin("##EditorHost", nullptr, kHostFlags);

	//   スタイルは Begin の中で読まれるので、直後に戻してよい
	ImGui::PopStyleVar(3);   // Push したぶんだけ Pop する

	// ────────────────────────────────────────────────────────────────
	// ドックスペースを発行する
	//   これより後に Begin されたパネルが、この中に収まる
	// ────────────────────────────────────────────────────────────────
	const ImGuiID dockSpaceId = ImGui::GetID("EditorDockSpace");

	//   ノードが無い ＝ imgui.ini に配置が保存されていない ＝ 初回起動
	//   リセット要求があったときも、同じ処理で既定配置に戻す
	if (resetLayoutRequested_ || ImGui::DockBuilderGetNode(dockSpaceId) == nullptr) {
		BuildDefaultLayout(dockSpaceId, hostSize.x, hostSize.y);
		resetLayoutRequested_ = false;
	}

	ImGui::DockSpace(dockSpaceId);

	ImGui::End();
#endif
}

// ════════════════════════════════════════════════════════════════════
// 初回起動時の既定配置を組む
//   imgui.ini が無いときだけ通る。以降はユーザーが動かした配置が復元される
// ════════════════════════════════════════════════════════════════════
void EditorLayout::BuildDefaultLayout(uint32_t dockSpaceId, float width, float height) {
#ifdef USE_IMGUI

	const ImGuiID rootId = dockSpaceId;

	//   残骸があると分割がおかしくなるので、空の状態から作る
	ImGui::DockBuilderRemoveNode(rootId);
	ImGui::DockBuilderAddNode(rootId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(rootId, ImVec2(width, height));

	// ────────────────────────────────────────────────────────────────
	// 分割する
	//   DockBuilderSplitNode は「切り取った側の ID」を返し、
	//   第5引数に「残り側の ID」を書き戻す。
	//   centerId を渡しては受け取り直すことで、残りを削りながら進められる
	// ────────────────────────────────────────────────────────────────
	ImGuiID centerId = rootId;

	const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
		centerId, ImGuiDir_Down, kBottomRatio, nullptr, &centerId);
	const ImGuiID hierarchyId = ImGui::DockBuilderSplitNode(
		centerId, ImGuiDir_Left, kHierarchyRatio, nullptr, &centerId);
	const ImGuiID inspectorId = ImGui::DockBuilderSplitNode(
		centerId, ImGuiDir_Right, kInspectorRatio, nullptr, &centerId);

	//   下段をさらに3分割（左：アセット／中央：デバッグ／右：設定）
	ImGuiID bottomCenterId = bottomId;

	const ImGuiID assetId = ImGui::DockBuilderSplitNode(
		bottomCenterId, ImGuiDir_Left, kAssetRatio, nullptr, &bottomCenterId);
	const ImGuiID settingsId = ImGui::DockBuilderSplitNode(
		bottomCenterId, ImGuiDir_Right, kSettingsRatio, nullptr, &bottomCenterId);

	// ────────────────────────────────────────────────────────────────
	// どのパネルをどのノードに入れるか
	//   ウィンドウ名で指定するので、kPanelTitles と Begin の名前が一致していること
	// ────────────────────────────────────────────────────────────────
	ImGui::DockBuilderDockWindow(kPanelTitles[ToIndex(EditorPanel::View)], centerId);
	ImGui::DockBuilderDockWindow(kPanelTitles[ToIndex(EditorPanel::Hierarchy)], hierarchyId);
	ImGui::DockBuilderDockWindow(kPanelTitles[ToIndex(EditorPanel::Inspector)], inspectorId);
	ImGui::DockBuilderDockWindow(kPanelTitles[ToIndex(EditorPanel::Asset)], assetId);
	ImGui::DockBuilderDockWindow(kPanelTitles[ToIndex(EditorPanel::Debug)], bottomCenterId);
	ImGui::DockBuilderDockWindow(kPanelTitles[ToIndex(EditorPanel::Settings)], settingsId);

	ImGui::DockBuilderFinish(rootId);

#else
	(void)dockSpaceId;
	(void)width;
	(void)height;
#endif
}

// ════════════════════════════════════════════════════════════════════
// パネルを開く
//   ツールバーだけは固定、それ以外は ImGui に任せる
// ════════════════════════════════════════════════════════════════════
bool EditorLayout::BeginPanel(EditorPanel panel) {
#ifdef USE_IMGUI

	const uint32_t index = ToIndex(panel);
	bool opened = false;

	if (panel == EditorPanel::Toolbar) {
		// ────────────────────────────────────────────────────────────
		// ツールバーだけは固定配置
		//   SetNextWindowPos / Size は「次に Begin するウィンドウ」に効く
		// ────────────────────────────────────────────────────────────
		const Rect& rect = rects_[index];
		ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
		ImGui::SetNextWindowSize(ImVec2(rect.width, rect.height));

		opened = ImGui::Begin(kPanelTitles[index], nullptr,
			kFixedPanelFlags | GetExtraFlags(panel));

	} else {
		// ────────────────────────────────────────────────────────────
		// それ以外は位置もサイズも指定しない
		//   指定すると ImGui が「動かせないウィンドウ」と判断し、
		//   ドッキングもサイズ変更もできなくなる
		// ────────────────────────────────────────────────────────────
		opened = ImGui::Begin(kPanelTitles[index], nullptr,
			kDockablePanelFlags | GetExtraFlags(panel));
	}

	//   実際に置かれた矩形を記録する（End より前でないと取れない）
	RecordRect(panel, opened);

	return opened;

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
// いま開いているウィンドウの実際の矩形を記録する
//   ドロップ位置の判定（IsInsidePanel）がこれを使う
// ════════════════════════════════════════════════════════════════════
void EditorLayout::RecordRect(EditorPanel panel, bool isVisible) {
#ifdef USE_IMGUI

	Rect& rect = rects_[ToIndex(panel)];

	//   タブの裏に隠れている／畳まれているパネルは、当たり判定を無効にする
	//   見えていない場所に落とせてしまうと、ユーザーが混乱する
	if (!isVisible) {
		rect = Rect{};
		return;
	}

	const ImVec2 position = ImGui::GetWindowPos();
	const ImVec2 size = ImGui::GetWindowSize();

	rect = Rect{ position.x, position.y, size.x, size.y };

#else
	(void)panel;
	(void)isVisible;
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
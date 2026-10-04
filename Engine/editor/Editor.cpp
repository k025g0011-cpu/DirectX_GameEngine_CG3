#include "Editor.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "EngineContext.h"
#include "AssetDatabase.h" // AssetEntry / AssetType / GetTypeName を使う
#include "GameObject.h"
#include "GameTime.h"
#include "WinApp.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace {

	/// <summary>アセットの種類に対応する小包の名前。ドラッグできないなら nullptr</summary>
	const char* GetPayloadType(AssetType type) {
		switch (type) {
		case AssetType::Model:   return AssetPayload::kModel;
		case AssetType::Texture: return AssetPayload::kTexture;
		default:                 return nullptr;   // シーン・サウンドは今のところ掴めない
		}
	}

}   // namespace

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void Editor::Initialize(const EngineContext& context, Camera* gameCamera) {

	context_ = &context;
	gameCamera_ = gameCamera;

	// ════════════════════════════════════════
	// 編集用のカメラ
	// ════════════════════════════════════════
	debugCamera_.Initialize(context.input,
		WinApp::kClientWidth, WinApp::kClientHeight);

	// ════════════════════════════════════════
	// ビュー用のオフスクリーン（編集用とゲーム用で2枚）
	//   クリア色を変えておくと、どちらを見ているか一目で分かる
	// ════════════════════════════════════════
	RenderTexture::Desc desc{};
	desc.width = WinApp::kClientWidth;
	desc.height = WinApp::kClientHeight;
	desc.useDepth = true;

	desc.clearColor = { 0.15f, 0.15f, 0.18f, 1.0f };   // 編集：暗いグレー
	editRenderTexture_.Initialize(context.dxCommon, desc);

	desc.clearColor = { 0.1f, 0.25f, 0.5f, 1.0f };     // ゲーム：青
	gameRenderTexture_.Initialize(context.dxCommon, desc);
}

// ════════════════════════════════════════════════════════════════════
// 更新
//   ビューを先に描いて viewMode_ と isViewHovered_ を決め、
//   それからカメラを更新する
// ════════════════════════════════════════════════════════════════════
void Editor::Update() {

	// ════════════════════════════════════════
	// ドロップされたファイルを取り込む
	//  今はどこに落としても受け取る
	// ════════════════════════════════════════
	for (const std::string& droppedFile : context_->winApp->GetDroppedFiles()) {
		context_->assetDatabase->Import(droppedFile);
	}

#ifdef USE_IMGUI
	layout_.Update();       // ① 画面サイズから全パネルの矩形を計算
	ImportDroppedFiles();   // ② ①の矩形とドロップ位置を比べて取り込む
	DrawViewPanel();        // ③ ここで viewMode_ と isViewHovered_ が決まる
	DrawToolbar();
	DrawAssetPanel();
#endif

	// 編集タブの画像の上にマウスがあるときだけ、カメラ操作を受け付ける
	debugCamera_.SetInputEnabled(viewMode_ == ViewMode::Edit && isViewHovered_);
	debugCamera_.Update();
}

// ════════════════════════════════════════════════════════════════════
// いま描くべき描画先
// ════════════════════════════════════════════════════════════════════
RenderTexture& Editor::GetActiveRenderTexture() {
	return (viewMode_ == ViewMode::Edit) ? editRenderTexture_ : gameRenderTexture_;
}

// ════════════════════════════════════════════════════════════════════
// いま使うカメラのビュー×射影行列
// ════════════════════════════════════════════════════════════════════
const Matrix4x4& Editor::GetViewProjectionMatrix() const {
	return (viewMode_ == ViewMode::Edit)
		? debugCamera_.GetViewProjectionMatrix()
		: gameCamera_->GetViewProjectionMatrix();
}

// ════════════════════════════════════════════════════════════════════
// ツールバー（再生・コマ送り・速度・fps）
// ════════════════════════════════════════════════════════════════════
void Editor::DrawToolbar() {
#ifdef USE_IMGUI

	if (layout_.BeginPanel(EditorPanel::Toolbar)) {

		GameTime* gameTime = context_->gameTime;
		const bool isPaused = gameTime->IsPaused();

		// ───── 再生 / 一時停止（状態で表示が入れ替わる1つのボタン） ─────
		if (ImGui::Button(isPaused ? "再生" : "一時停止", ImVec2(90.0f, 0.0f))) {
			gameTime->SetPaused(!isPaused);
		}

		// ───── コマ送り（一時停止中だけ押せる） ─────
		ImGui::SameLine();
		ImGui::BeginDisabled(!isPaused);
		if (ImGui::Button("コマ送り", ImVec2(90.0f, 0.0f))) {
			gameTime->RequestStep();
		}
		ImGui::EndDisabled();

		// ───── 速度 ─────
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		float timeScale = gameTime->GetTimeScale();
		if (ImGui::DragFloat("速度", &timeScale, 0.01f, 0.0f, 3.0f, "%.2f")) {
			gameTime->SetTimeScale(timeScale);
		}

		// ───── fps（右端に寄せる） ─────
		ImGui::SameLine();
		const float fpsWidth = ImGui::CalcTextSize("000.0 fps").x;
		ImGui::SetCursorPosX(
			ImGui::GetWindowWidth() - fpsWidth - ImGui::GetStyle().WindowPadding.x);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("%.1f fps", gameTime->GetFPS());
	}
	layout_.EndPanel();

#endif
}

// ════════════════════════════════════════════════════════════════════
// ビューのパネル
//   編集 / ゲーム のタブを出し、選ばれている方のテクスチャを表示する
// ════════════════════════════════════════════════════════════════════
void Editor::DrawViewPanel() {
#ifdef USE_IMGUI

	isViewHovered_ = false;

	if (layout_.BeginPanel(EditorPanel::View)) {

		if (ImGui::BeginTabBar("ViewTabs")) {

			// ───── 編集タブ（デバッグカメラ） ─────
			if (ImGui::BeginTabItem("編集")) {
				viewMode_ = ViewMode::Edit;

				// 編集タブでだけカメラ操作を受け付ける
				isViewHovered_ = DrawViewImage(editRenderTexture_);

				ImGui::EndTabItem();
			}

			// ───── ゲームタブ（ゲームカメラ） ─────
			if (ImGui::BeginTabItem("ゲーム")) {
				viewMode_ = ViewMode::Game;

				// 戻り値は使わない（ゲーム画面ではカメラを動かさない）
				DrawViewImage(gameRenderTexture_);

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	layout_.EndPanel();

#endif
}

// ════════════════════════════════════════════════════════════════════
// ドロップされたファイルの取り込み
//   アセットパネルの上に落とされたときだけ受け取る
// ════════════════════════════════════════════════════════════════════
void Editor::ImportDroppedFiles() {

	const std::vector<std::string>& droppedFiles = context_->winApp->GetDroppedFiles();

	// このフレームにドロップが無ければ何もしない
	//   メッセージも消さない（直前の結果を読む時間を残す）
	if (droppedFiles.empty()) {
		return;
	}

	const Vector2 dropPosition = context_->winApp->GetDropPosition();

	// ────────────────────────────────────────────────────────────────
	// アセットパネルの外なら取り込まない
	//   どこに落としたか分かるよう、座標も出しておく
	// ────────────────────────────────────────────────────────────────
	if (!layout_.IsInsidePanel(EditorPanel::Asset, dropPosition.x, dropPosition.y)) {
		lastImportMessage_ =
			"アセットパネルに落としてください ("
			+ std::to_string(static_cast<int>(dropPosition.x)) + ", "
			+ std::to_string(static_cast<int>(dropPosition.y)) + ")";
		return;
	}

	// ────────────────────────────────────────────────────────────────
	// 取り込む。拡張子が未対応なら Import() が false を返す
	// ────────────────────────────────────────────────────────────────
	uint32_t successCount = 0;
	for (const std::string& droppedFile : droppedFiles) {
		if (context_->assetDatabase->Import(droppedFile)) {
			++successCount;
		}
	}

	lastImportMessage_ =
		"取り込み " + std::to_string(successCount)
		+ " / " + std::to_string(droppedFiles.size()) + " 件";
}

void Editor::DrawAssetPanel() {
#ifdef USE_IMGUI
	// BeginPanel は戻り値に関係なく EndPanel と対にする（ImGui::Begin と同じ規則）
	if (!layout_.BeginPanel(EditorPanel::Asset)) {
		layout_.EndPanel();
		return;
	}

	AssetDatabase* assetDatabase = context_->assetDatabase;

	// ── 1段目：更新ボタンと件数 ───────────────────────────
	if (ImGui::Button("更新")) {
		assetDatabase->Refresh();
	}
	ImGui::SameLine();
	ImGui::Text("%u 件", assetDatabase->GetAssetCount());

	// ── 2段目：種類フィルタ ───────────────────────────────
	// 並び順は AssetType の値（Unknown=0 を「すべて」の位置に使う）
	static const char* kFilterNames[] = {
		"すべて", "モデル", "テクスチャ", "シーン", "サウンド", "マテリアル",
	};
	ImGui::SetNextItemWidth(140.0f);
	ImGui::Combo("種類", &assetTypeFilter_, kFilterNames, IM_ARRAYSIZE(kFilterNames));

	// ── 直近のドロップ結果 ────────────────────────────────
	if (!lastImportMessage_.empty()) {
		ImGui::TextDisabled("%s", lastImportMessage_.c_str());
	}

	ImGui::Separator();

	// ── 3段目：一覧 ───────────────────────────────────────
	const AssetType filterType = static_cast<AssetType>(assetTypeFilter_);
	AssetType previousType = AssetType::Unknown;
	bool isFirstRow = true;

	for (uint32_t i = 0; i < assetDatabase->GetAssetCount(); ++i) {
		const AssetEntry& asset = assetDatabase->GetAsset(i);

		// フィルタ（0 = すべて なので素通し）
		if (assetTypeFilter_ != 0 && asset.type != filterType) {
			continue;
		}

		// assets_ は種類→名前の順にソート済みなので、
		// 種類が変わった行だけ見出しを出せばグループ分けになる
		if (isFirstRow || asset.type != previousType) {
			if (!isFirstRow) {
				ImGui::Spacing();
			}
			ImGui::TextDisabled("%s", AssetDatabase::GetTypeName(asset.type));
			previousType = asset.type;
			isFirstRow = false;
		}

		// 同名で拡張子違い（cube.obj / cube.png）があり得るので ID を分ける
		ImGui::PushID(static_cast<int>(i));

		const std::string label = asset.name + asset.extension;
		const bool isSelected = (asset.path == selectedAssetPath_);

		ImGui::Indent();
		if (ImGui::Selectable(label.c_str(), isSelected)) {
			selectedAssetPath_ = asset.path;
		}

		// 直前のアイテム（= 上の Selectable）をドラッグ元にする
		if (const char* payloadType = GetPayloadType(asset.type)) {
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {

				// 小包にパスを詰める。+1 は終端の '\0' の分
				ImGui::SetDragDropPayload(
					payloadType,
					asset.path.c_str(),
					asset.path.size() + 1);

				// ドラッグ中、カーソルに付いてくる見た目
				ImGui::Text("%s", label.c_str());

				ImGui::EndDragDropSource();
			}
		}

		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", asset.path.c_str());
		}
		ImGui::Unindent();

		ImGui::PopID();
	}

	layout_.EndPanel();
#endif
}

// ════════════════════════════════════════════════════════════════════
// オフスクリーンテクスチャを1枚表示する
//   縦横比を保ったまま、タブの中央に最大サイズで配置する
// ════════════════════════════════════════════════════════════════════
bool Editor::DrawViewImage(RenderTexture& renderTexture) {
#ifdef USE_IMGUI

	// タブの中で使える領域
	const ImVec2 available = ImGui::GetContentRegionAvail();

	// 縦横比を保ったまま、領域に収まる最大サイズを求める
	const float aspect = renderTexture.GetAspectRatio();

	float imageWidth = available.x;
	float imageHeight = imageWidth / aspect;

	if (imageHeight > available.y) {
		imageHeight = available.y;
		imageWidth = imageHeight * aspect;
	}

	// 領域が潰れているときは何もしない（ImGui がサイズ0でアサートする）
	if (imageWidth <= 1.0f || imageHeight <= 1.0f) {
		return false;
	}

	// 余白を半分ずつに分けて中央寄せ
	const ImVec2 imagePos = ImVec2(
		ImGui::GetCursorPosX() + (available.x - imageWidth) * 0.5f,
		ImGui::GetCursorPosY() + (available.y - imageHeight) * 0.5f);

	const ImVec2 imageSize = ImVec2(imageWidth, imageHeight);

	// ① 絵を描く
	ImGui::SetCursorPos(imagePos);
	ImTextureID textureId = (ImTextureID)renderTexture.GetSrvGpuHandle().ptr;
	ImGui::Image(textureId, imageSize);

	// ② 同じ場所に透明ボタンを重ねて、操作判定を取る
	ImGui::SetCursorPos(imagePos);
	ImGui::InvisibleButton("##view", imageSize,
		ImGuiButtonFlags_MouseButtonLeft |
		ImGuiButtonFlags_MouseButtonRight |
		ImGuiButtonFlags_MouseButtonMiddle);

	return (ImGui::IsItemHovered() || ImGui::IsItemActive())
		&& !ImGui::GetIO().WantTextInput;

#else
	(void)renderTexture;
	return false;
#endif
}

// ════════════════════════════════════════════════════════════════════
// オブジェクト一覧と詳細
//   オブジェクトやコンポーネントが増えても、このコードは変わらない
// ════════════════════════════════════════════════════════════════════
void Editor::DrawObjectPanels(
	const std::vector<std::unique_ptr<GameObject>>& gameObjects) {
#ifdef USE_IMGUI

	// ───────────────────────────────────────
	// オブジェクト一覧
	// ───────────────────────────────────────
	if (layout_.BeginPanel(EditorPanel::Hierarchy)) {

		for (size_t i = 0; i < gameObjects.size(); ++i) {
			GameObject* gameObject = gameObjects[i].get();

			// 同じ名前のオブジェクトがあっても別物として扱えるようにする
			ImGui::PushID(static_cast<int>(i));

			const bool isSelected = (selectedObjectIndex_ == static_cast<int32_t>(i));
			if (ImGui::Selectable(gameObject->GetName().c_str(), isSelected)) {
				selectedObjectIndex_ = static_cast<int32_t>(i);
			}

			ImGui::PopID();
		}

		ImGui::Separator();
		ImGui::Text("合計 : %zu", gameObjects.size());
	}
	layout_.EndPanel();

	// ───────────────────────────────────────
	// 詳細
	// ───────────────────────────────────────
	if (layout_.BeginPanel(EditorPanel::Inspector)) {

		if (selectedObjectIndex_ >= 0 &&
			selectedObjectIndex_ < static_cast<int32_t>(gameObjects.size())) {

			// ここだけ。中身は GameObject と各コンポーネントが描く
			gameObjects[selectedObjectIndex_]->DrawInspector();

		} else {
			ImGui::TextDisabled("(未選択)");
		}
	}
	layout_.EndPanel();

#else
	(void)gameObjects;
#endif
}
#include "GameScene.h"

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "Object3dPipeline.h"
#include "PrimitiveDrawer.h"
#include "Input.h"
#include "GameTime.h"
#include "Model.h"
#include "PrimitiveModel.h"
#include "WinApp.h"

// コンポーネントファイル
#include "MeshRenderer.h"
#include "Rotator.h"  

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// ════════════════════════════════════════════════════════════════════
// 初期化
//   このシーンに何を出すかを決める場所
// ════════════════════════════════════════════════════════════════════
void GameScene::Initialize(const EngineContext& context) {

	context_ = context;

	// ════════════════════════════════════════
	// カメラとエディタ
	//   エディタはゲームカメラを知る必要がある（ゲームタブで使うため）
	// ════════════════════════════════════════
	camera_.Initialize();
	editor_.Initialize(context_, &camera_);

	// ════════════════════════════════════════
	// モデルを用意する
	//   ModelManager が所有するので、ここはポインタを借りるだけ
	// ════════════════════════════════════════
	sphereModel_ = context_.modelManager->Create("Sphere16",
		PrimitiveModel::CreateSphere(16, "resources/uvChecker.png"));

	suzanneModel_ = context_.modelManager->Load("resources/suzanne.obj");

	// ════════════════════════════════════════
	// オブジェクトを配置する
	//   「箱を作る → 位置を決める → 機能を足す」の3手順
	// ════════════════════════════════════════
	sphereObject_ = CreateGameObject("Sphere");
	sphereObject_->GetTransform().translate = { -1.5f, 0.0f, 0.0f };
	sphereObject_->AddComponent<MeshRenderer>(sphereModel_);
	sphereObject_->AddComponent<Rotator>(Vector3{ 0.0f, 0.0f, 1.0f });

	suzanneObject_ = CreateGameObject("Suzanne");
	suzanneObject_->GetTransform().translate = { 1.5f, 0.0f, 0.0f };
	suzanneObject_->AddComponent<MeshRenderer>(suzanneModel_);
	suzanneObject_->AddComponent<Rotator>(Vector3{ 0.0f, 1.0f, 0.0f });

	// ════════════════════════════════════════
	// スプライト
	// ════════════════════════════════════════
	uint32_t uvCheckerTextureIndex =
		context_.textureManager->Load("resources/uvChecker.png");

	sprite_.Initialize(context_.dxCommon, context_.textureManager,
		uvCheckerTextureIndex, { 640.0f, 360.0f });

	// ════════════════════════════════════════
	// 平行光源（シーンに1つ。モデルとスプライトで共通）
	// ════════════════════════════════════════
	directionalLightResource_ =
		context_.dxCommon->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource_->Map(0, nullptr,
		reinterpret_cast<void**>(&directionalLightData_));

	directionalLightData_->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };  // 白
	directionalLightData_->direction = Vector3{ 0.0f, -1.0f, 0.0f };   // 真上から下へ
	directionalLightData_->intensity = 1.0f;
}

// ════════════════════════════════════════════════════════════════════
// GameObject を作ってシーンに登録する
// ════════════════════════════════════════════════════════════════════
GameObject* GameScene::CreateGameObject(const std::string& name) {

	std::unique_ptr<GameObject> gameObject = std::make_unique<GameObject>();
	gameObject->Initialize(context_, name);

	// move する前に生ポインタを取っておく
	GameObject* result = gameObject.get();

	gameObjects_.push_back(std::move(gameObject));

	return result;
}

// ════════════════════════════════════════════════════════════════════
// 更新
//   値を変えるだけ。描画コマンドは積まない
// ════════════════════════════════════════════════════════════════════
void GameScene::Update() {

	// ════════════════════════════════════════
	// ① エディタ（レイアウト・ビュー・ツールバー・編集カメラ）
	// ════════════════════════════════════════
	editor_.Update();

	// ② ゲームカメラ
	camera_.Update();

	// ════════════════════════════════════════
	// ③ このゲーム固有のパネル
	// ════════════════════════════════════════
	DrawSettingsPanel();
	DrawDebugPanel();

	// ④ オブジェクト一覧と詳細（中身はエディタが描く）
	editor_.DrawObjectPanels(gameObjects_);

	// ════════════════════════════════════════
	// ⑥ 全オブジェクトの更新
	// ════════════════════════════════════════
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		gameObject->Update();
	}

	// ════════════════════════════════════════
	// ⑦ UVTransform を反映する
	// ════════════════════════════════════════
	Matrix4x4 uvTransformMatrixModel = MakeAffineMatrix(
		uvTransformModel_.scale, uvTransformModel_.rotate, uvTransformModel_.translate);

	for (uint32_t i = 0; i < sphereModel_->GetMaterialCount(); ++i) {
		sphereModel_->GetMaterialData(i)->uvTransform = uvTransformMatrixModel;
	}

	Matrix4x4 uvTransformMatrixSprite = MakeAffineMatrix(
		uvTransformSprite_.scale, uvTransformSprite_.rotate, uvTransformSprite_.translate);

	sprite_.GetMaterialData()->uvTransform = uvTransformMatrixSprite;
}

// ════════════════════════════════════════════════════════════════════
// 描画
//   どこに・どのカメラで描くかはエディタが決める
// ════════════════════════════════════════════════════════════════════
void GameScene::Draw() {

	RenderTexture& target = editor_.GetActiveRenderTexture();

	target.PreDraw();
	DrawScene(editor_.GetViewProjectionMatrix());
	target.PostDraw();
}

// ════════════════════════════════════════════════════════════════════
// シーンの中身を描く
//   描画先は呼び出し側が設定済み。ここでは値を変えない
// ════════════════════════════════════════════════════════════════════
void GameScene::DrawScene(const Matrix4x4& viewProjectionMatrix) {

	ID3D12GraphicsCommandList* commandList = context_.dxCommon->GetCommandList().Get();

	// ① 線の描画（自前の RootSignature / PSO に切り替わる）
	context_.primitiveDrawer->Reset();
	DrawGrid();
	context_.primitiveDrawer->Draw(viewProjectionMatrix);

	// ② 3D用パイプラインに戻す
	context_.object3dPipeline->SetPipeline();

	// 平行光源はシーン共通なので、描画の最初に1回設定すればよい
	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterLight,
		directionalLightResource_->GetGPUVirtualAddress());

	// ③ 全オブジェクト
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		gameObject->Draw(viewProjectionMatrix);
	}

	// ④ スプライト
	sprite_.Draw();
}

// ════════════════════════════════════════════════════════════════════
// 基準グリッド（Blenderの床グリッド相当）
// ════════════════════════════════════════════════════════════════════
void GameScene::DrawGrid() {

	PrimitiveDrawer& drawer = *context_.primitiveDrawer;

	const Vector4 kGray = { 0.35f, 0.35f, 0.35f, 1.0f };
	const Vector4 kRed = { 0.75f, 0.25f, 0.25f, 1.0f };
	const Vector4 kGreen = { 0.25f, 0.65f, 0.35f, 1.0f };
	const Vector4 kBlue = { 0.25f, 0.45f, 0.80f, 1.0f };

	const float half = kGridHalfCount * kGridSpacing;

	for (int32_t n = -kGridHalfCount; n <= kGridHalfCount; ++n) {
		const float p = n * kGridSpacing;

		drawer.DrawLine3d({ p, 0.0f, -half }, { p, 0.0f, half }, (n == 0) ? kBlue : kGray);
		drawer.DrawLine3d({ -half, 0.0f, p }, { half, 0.0f, p }, (n == 0) ? kRed : kGray);
	}

	drawer.DrawLine3d({ 0.0f, 0.0f, 0.0f }, { 0.0f, half, 0.0f }, kGreen);
}

// ════════════════════════════════════════════════════════════════════
// 設定パネル（このゲーム固有）
//   枠はエディタから借りて、中身だけ書く
// ════════════════════════════════════════════════════════════════════
void GameScene::DrawSettingsPanel() {
#ifdef USE_IMGUI

	if (editor_.BeginPanel(EditorPanel::Settings)) {

		// ───── ゲームカメラ ─────
		Vector3 cameraTranslate = camera_.GetTranslate();
		if (ImGui::DragFloat3("camera translate", &cameraTranslate.x, 0.01f)) {
			camera_.SetTranslate(cameraTranslate);
		}
		Vector3 cameraRotate = camera_.GetRotate();
		if (ImGui::DragFloat3("camera rotate", &cameraRotate.x, 0.01f)) {
			camera_.SetRotate(cameraRotate);
		}
		ImGui::Separator();

		// ───── モデルの UV（Model 共有データなので詳細ではなくここ） ─────
		ImGui::DragFloat2("ModelUVTranslate", &uvTransformModel_.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("ModelUVScale", &uvTransformModel_.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("ModelUVRotate", &uvTransformModel_.rotate.z);
		ImGui::Separator();

		// ───── ライト ─────
		ImGui::DragFloat3("light direction", &directionalLightData_->direction.x, 0.01f);
		ImGui::DragFloat("light intensity", &directionalLightData_->intensity, 0.01f);
		ImGui::ColorEdit4("light color", &directionalLightData_->color.x);
		directionalLightData_->direction = Normalize(directionalLightData_->direction);
		ImGui::Separator();

		// ───── スプライト ─────
		Vector2 spritePosition = sprite_.GetPosition();
		if (ImGui::DragFloat2("sprite position", &spritePosition.x, 1.0f)) {
			sprite_.SetPosition(spritePosition);
		}
		float spriteRotation = sprite_.GetRotation();
		if (ImGui::SliderAngle("sprite rotate", &spriteRotation)) {
			sprite_.SetRotation(spriteRotation);
		}
		Vector2 spriteAnchor = sprite_.GetAnchorPoint();
		if (ImGui::DragFloat2("sprite anchor", &spriteAnchor.x, 0.01f, 0.0f, 1.0f)) {
			sprite_.SetAnchorPoint(spriteAnchor);
		}
		ImGui::DragFloat2("SpriteUVTranslate", &uvTransformSprite_.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("SpriteUVScale", &uvTransformSprite_.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("SpriteUVRotate", &uvTransformSprite_.rotate.z);
	}
	editor_.EndPanel();

#endif
}

// ════════════════════════════════════════════════════════════════════
// デバッグパネル（このゲーム固有）
// ════════════════════════════════════════════════════════════════════
void GameScene::DrawDebugPanel() {
#ifdef USE_IMGUI

	if (editor_.BeginPanel(EditorPanel::Debug)) {

		// ───── 時間 ─────
		ImGui::Text("dt       : %.4f s", context_.gameTime->GetUnscaledDeltaTime());
		ImGui::Text("game dt  : %.4f s", context_.gameTime->GetDeltaTime());
		ImGui::Text("paused   : %s%s",
			context_.gameTime->IsPaused() ? "YES" : "no",
			context_.gameTime->IsStepping() ? " (step)" : "");
		ImGui::Separator();

		// ───── 統計 ─────
		ImGui::Text("Lines    : %u", context_.primitiveDrawer->GetLineCount());
		ImGui::Text("Models   : %u   Textures : %u",
			context_.modelManager->GetModelCount(),
			context_.textureManager->GetTextureCount());
		ImGui::Separator();

		// ───── ビューと編集カメラ ─────
		const DebugCamera& debugCamera = editor_.GetDebugCamera();

		ImGui::Text("view     : %s",
			editor_.GetViewMode() == ViewMode::Edit ? "編集" : "ゲーム");
		ImGui::Text("hovered  : %s", editor_.IsViewHovered() ? "YES" : "no");
		ImGui::Text("cam input: %s", debugCamera.IsInputEnabled() ? "ENABLED" : "disabled");
		ImGui::Separator();

		ImGui::Text("pos      : %.2f, %.2f, %.2f",
			debugCamera.GetTranslation().x, debugCamera.GetTranslation().y,
			debugCamera.GetTranslation().z);
		ImGui::Text("target   : %.2f, %.2f, %.2f",
			debugCamera.GetTarget().x, debugCamera.GetTarget().y,
			debugCamera.GetTarget().z);
		ImGui::Text("dist     : %.2f", debugCamera.GetDistance());
		ImGui::Separator();

		// ───── 生の入力 ─────
		ImGui::Text("mouse    : %.0f, %.0f   wheel %.1f",
			context_.input->GetMouseMove().x, context_.input->GetMouseMove().y,
			context_.input->GetMouseWheel());
	}
	editor_.EndPanel();

#endif
}
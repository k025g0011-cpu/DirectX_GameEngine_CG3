#include "GameScene.h"

#include "DirectXCommon.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "Object3dPipeline.h"
#include "PrimitiveDrawer.h"
#include "Input.h"
#include "GameTime.h"
#include "Model.h"
#include "MeshRenderer.h"
#include "PrimitiveModel.h"
#include "WinApp.h"

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
	// カメラ
	// ════════════════════════════════════════
	camera_.Initialize();

	debugCamera_.Initialize(context_.input,
		WinApp::kClientWidth, WinApp::kClientHeight);

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

	suzanneObject_ = CreateGameObject("Suzanne");
	suzanneObject_->GetTransform().translate = { 1.5f, 0.0f, 0.0f };
	suzanneObject_->AddComponent<MeshRenderer>(suzanneModel_);

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
//   「作る → 初期化 → 登録 → ポインタを返す」をまとめた係
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

	const float deltaTime = context_.gameTime->GetDeltaTime();

	// ════════════════════════════════════════
	// ① カメラの切り替えと更新
	// ════════════════════════════════════════
	if (context_.input->TriggerKey(DIK_F1)) {
		useDebugCamera_ = !useDebugCamera_;
	}

	// 入力を受け付ける条件は「デバッグカメラが ON」かつ
	// 「ゲームビューの上にマウスがある」の2つだけ
	debugCamera_.SetInputEnabled(useDebugCamera_ && isGameViewActive_);
	debugCamera_.Update();

	camera_.Update();

	// ════════════════════════════════════════
	// ② ImGui（人の操作を受け付けるので、ゲームの動きより先）
	// ════════════════════════════════════════
	UpdateImGui();

	// ════════════════════════════════════════
	// ③ ゲームの動き
	//   「毎秒どれだけ」× 経過秒 で書く
	//   ※ いずれ RotatorComponent にして、ここから消える
	// ════════════════════════════════════════
	suzanneObject_->GetTransform().rotate.y += 1.0f * deltaTime;   // 毎秒1ラジアン

	// ════════════════════════════════════════
	// ④ 全オブジェクトの更新
	//   GameObject が自分のコンポーネントに配る
	// ════════════════════════════════════════
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		gameObject->Update();
	}

	// ════════════════════════════════════════
	// ⑤ UVTransform を反映する
	//   Model（共有データ）側のマテリアルを書き換えるので、
	//   そのモデルを使う全オブジェクトに効く
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
//   PreDraw と PostDraw の間で呼ばれる。ここでは値を変えない
// ════════════════════════════════════════════════════════════════════
void GameScene::Draw() {

	ID3D12GraphicsCommandList* commandList = context_.dxCommon->GetCommandList().Get();
	const Matrix4x4& viewProjectionMatrix = GetViewProjectionMatrix();

	// ════════════════════════════════════════
	// ① 線の描画（自前の RootSignature / PSO に切り替わる）
	// ════════════════════════════════════════
	context_.primitiveDrawer->Reset();
	DrawGrid();
	context_.primitiveDrawer->Draw(viewProjectionMatrix);

	// ════════════════════════════════════════
	// ② 3D用パイプラインに戻す
	//     線の描画で RootSignature が変わっているので、ここで必ず設定し直す
	// ════════════════════════════════════════
	context_.object3dPipeline->SetPipeline();   // 既定は BlendMode::None

	// 平行光源はシーン共通なので、描画の最初に1回設定すればよい
	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterLight,
		directionalLightResource_->GetGPUVirtualAddress());

	// ════════════════════════════════════════
	// ③ 全オブジェクト
	//   種類が増えてもこのループは変わらない
	// ════════════════════════════════════════
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		gameObject->Draw(viewProjectionMatrix);
	}

	// ════════════════════════════════════════
	// ④ スプライト（パイプラインはモデルと共通）
	// ════════════════════════════════════════
	sprite_.Draw();
}

// ════════════════════════════════════════════════════════════════════
// いま使うカメラのビュー×射影行列
// ════════════════════════════════════════════════════════════════════
const Matrix4x4& GameScene::GetViewProjectionMatrix() const {
	return useDebugCamera_
		? debugCamera_.GetViewProjectionMatrix()
		: camera_.GetViewProjectionMatrix();
}

// ════════════════════════════════════════════════════════════════════
// 基準グリッド（Blenderの床グリッド相当）
//   中心から ±kGridHalfCount マス、間隔 kGridSpacing
//   X軸=赤 / Y軸=緑 / Z軸=青、それ以外はグレー
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

		// Z方向に伸びる線（X = p の位置）
		drawer.DrawLine3d({ p, 0.0f, -half }, { p, 0.0f, half }, (n == 0) ? kBlue : kGray);

		// X方向に伸びる線（Z = p の位置）
		drawer.DrawLine3d({ -half, 0.0f, p }, { half, 0.0f, p }, (n == 0) ? kRed : kGray);
	}

	// Y軸（上向き）
	drawer.DrawLine3d({ 0.0f, 0.0f, 0.0f }, { 0.0f, half, 0.0f }, kGreen);
}

// ════════════════════════════════════════════════════════════════════
// ヒエラルキーとインスペクタ
//   エディタの原型。オブジェクトやコンポーネントが増えても
//     このコードは1文字も変わらない
// ════════════════════════════════════════════════════════════════════
void GameScene::DrawHierarchyAndInspector() {
#ifdef USE_IMGUI

	// ───────────────────────────────────────
	// ヒエラルキー（オブジェクトの一覧）
	// ───────────────────────────────────────
	ImGui::Begin("Hierarchy");

	for (size_t i = 0; i < gameObjects_.size(); ++i) {
		GameObject* gameObject = gameObjects_[i].get();

		// 同じ名前のオブジェクトがあっても別物として扱えるようにする
		ImGui::PushID(static_cast<int>(i));

		const bool isSelected = (selectedObjectIndex_ == static_cast<int32_t>(i));
		if (ImGui::Selectable(gameObject->GetName().c_str(), isSelected)) {
			selectedObjectIndex_ = static_cast<int32_t>(i);
		}

		ImGui::PopID();
	}

	ImGui::Text("objects : %zu", gameObjects_.size());
	ImGui::End();

	// ───────────────────────────────────────
	// インスペクタ（選択中のオブジェクトの中身）
	// ───────────────────────────────────────
	ImGui::Begin("Inspector");

	if (selectedObjectIndex_ >= 0 &&
		selectedObjectIndex_ < static_cast<int32_t>(gameObjects_.size())) {

		// ここだけ。中身は GameObject と各コンポーネントが描く
		gameObjects_[selectedObjectIndex_]->DrawInspector();

	} else {
		ImGui::TextDisabled("(no selection)");
	}

	ImGui::End();

#endif
}

// ════════════════════════════════════════════════════════════════════
// ImGui
// ════════════════════════════════════════════════════════════════════
void GameScene::UpdateImGui() {
#ifdef USE_IMGUI

	ImGui::Begin("Settings");

	// ───── 時間 ─────
	ImGui::Text("FPS: %.1f   dt: %.4f s",
		context_.gameTime->GetFPS(), context_.gameTime->GetUnscaledDeltaTime());

	float timeScale = context_.gameTime->GetTimeScale();
	if (ImGui::DragFloat("time scale", &timeScale, 0.01f, 0.0f, 3.0f)) {
		context_.gameTime->SetTimeScale(timeScale);
	}
	ImGui::Separator();

	// ───── カメラ ─────
	Vector3 cameraTranslate = camera_.GetTranslate();
	if (ImGui::DragFloat3("camera translate", &cameraTranslate.x, 0.01f)) {
		camera_.SetTranslate(cameraTranslate);
	}
	Vector3 cameraRotate = camera_.GetRotate();
	if (ImGui::DragFloat3("camera rotate", &cameraRotate.x, 0.01f)) {
		camera_.SetRotate(cameraRotate);
	}
	ImGui::Separator();

	// ───── モデルの UV（Model 共有データなのでインスペクタではなくここ） ─────
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
	ImGui::Separator();

	// ───── 統計 ─────
	ImGui::Text("Lines: %u", context_.primitiveDrawer->GetLineCount());
	ImGui::Text("Models: %u   Textures: %u",
		context_.modelManager->GetModelCount(), context_.textureManager->GetTextureCount());

	ImGui::End();

	// ════════════════════════════════════════
	// デバッグカメラの状態表示
	// ════════════════════════════════════════
	ImGui::Begin("DebugCamera");
	ImGui::Text("F1     : %s", useDebugCamera_ ? "ON" : "OFF");
	ImGui::Text("pos    : %.2f, %.2f, %.2f",
		debugCamera_.GetTranslation().x, debugCamera_.GetTranslation().y,
		debugCamera_.GetTranslation().z);
	ImGui::Text("target : %.2f, %.2f, %.2f",
		debugCamera_.GetTarget().x, debugCamera_.GetTarget().y, debugCamera_.GetTarget().z);
	ImGui::Text("dist   : %.2f", debugCamera_.GetDistance());
	ImGui::Separator();
	ImGui::Text("view   : %s", isGameViewActive_ ? "ACTIVE" : "inactive");
	ImGui::Text("input  : %s", debugCamera_.IsInputEnabled() ? "ENABLED" : "disabled");
	ImGui::End();

	// ════════════════════════════════════════
	// ヒエラルキーとインスペクタ
	// ════════════════════════════════════════
	DrawHierarchyAndInspector();

#endif
}
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
#include "ParticleEmitter.h"
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
	camera_.Initialize(context_.winApp->GetAspectRatio());
	editor_.Initialize(context_, &camera_);

	// ════════════════════════════════════════
	// モデルを用意する
	//   ModelManager が所有するので、ここはポインタを借りるだけ
	// ════════════════════════════════════════
	sphereModel_ = context_.modelManager->Create("Sphere16",  // このモデル変数ローカル変数でいいかも
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
	// パーティクル
	//   他のオブジェクトと同じ3手順：箱を作る → 位置を決める → 機能を足す
	// ════════════════════════════════════════
	particleObject_ = CreateGameObject("Particles");
	particleObject_->GetTransform().translate = { 0.0f, 2.0f, 0.0f };
	particleObject_->AddComponent<ParticleEmitter>("resources/particle.png", 128);


	// ════════════════════════════════════════
	// スプライト
	// ════════════════════════════════════════
	uint32_t uvCheckerTextureIndex =
		context_.textureManager->Load("resources/uvChecker.png");

	sprite_.Initialize(context_.dxCommon, context_.textureManager,
		uvCheckerTextureIndex, { 640.0f, 360.0f }, context_.winApp->GetClientSize());

	// ════════════════════════════════════════
	// 平行光源の定数バッファ（シーンに1つ。モデルとスプライトで共通）
	// ════════════════════════════════════════
	directionalLightResource_ =
		context_.dxCommon->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource_->Map(0, nullptr,
		reinterpret_cast<void**>(&directionalLightData_));

	directionalLightData_->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };  // 白
	directionalLightData_->direction = Vector3{ 0.0f, -1.0f, 0.0f };   // 真上から下へ
	directionalLightData_->intensity = 1.0f;

	// ════════════════════════════════════════
	// カメラの位置の定数バッファ（鏡面反射用。シーンに1つ）
	//   中身は毎フレーム DrawScene で書き換える
	// ════════════════════════════════════════
	cameraResource_ = context_.dxCommon->CreateBufferResource(sizeof(CameraForGPU));
	cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));

	cameraData_->worldPosition = Vector3{ 0.0f, 0.0f, 0.0f };

	
	// ════════════════════════════════════════
	// 点光源の定数バッファ
 	// ════════════════════════════════════════
	pointLightResource_ = context_.dxCommon->CreateBufferResource(sizeof(PointLight));
	pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightData_));
	pointLightData_->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	pointLightData_->position = Vector3{ 0.0f, 2.0f, 0.0f };
	pointLightData_->intensity = 1.0f;
	pointLightData_->radius = 10.0f;
	pointLightData_->decay = 1.0f;
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
	DrawScene(editor_.GetCameraData());
	target.PostDraw();
}

// ════════════════════════════════════════════════════════════════════
// シーンの中身を描く
//   描画先は呼び出し側が設定済み。ここでは値を変えない
// ════════════════════════════════════════════════════════════════════
void GameScene::DrawScene(const CameraData& camera) {

	// ① 線の描画（自前の RootSignature / PSO に切り替わる）
	//    Reset → 積む → Draw の順。Draw より後に積んでも間に合わない
	context_.primitiveDrawer->Reset();
	DrawGrid();

	// デバッグ表示は編集ビューのときだけ。ゲーム画面には出さない
	if (editor_.GetViewMode() == ViewMode::Edit) {
		DrawGizmos();
	}

	context_.primitiveDrawer->Draw(camera.viewProjectionMatrix);

	// ② 3D用パイプラインに戻す
	context_.object3dPipeline->SetPipeline();
	// シーン共通の定数（ライト・カメラ）をルートに設定する（PS b1 / PS b2 ｜　パイプラインをセットするたびにこの設定を行う）
	SetSceneConstants(camera);

	// ③ 全オブジェクト
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		gameObject->Draw(camera);
	}

	// ④ パーティクル（専用パス）
	//    パーティクルは別のパイプラインを使うので、③のループの中では描けない。
	//    ③の途中でパイプラインを切り替えると、後続の MeshRenderer が壊れる
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {

		if (!gameObject->IsActive()) {
			continue;
		}

		ParticleEmitter* emitter = gameObject->GetComponent<ParticleEmitter>();
		if (emitter == nullptr || !emitter->IsEnabled()) {
			continue;
		}

		emitter->DrawParticles(camera);
	}

	// ⑤ スプライトは Object3d のパイプラインを使うので、戻してから描く
	context_.object3dPipeline->SetPipeline();
	SetSceneConstants(camera);

	// ⑥ スプライト
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
// シーン共通の定数をルートに設定する
//   ルートシグネチャを設定し直すと、それまでの引数は全部無効になる
//     SetPipeline() を呼ぶたびに、この関数も呼び直す必要がある
// ════════════════════════════════════════════════════════════════════
void GameScene::SetSceneConstants(const CameraData& camera) {

	ID3D12GraphicsCommandList* commandList = context_.dxCommon->GetCommandList().Get();

	// カメラの位置を GPU へ送る（鏡面反射の視線計算に使う）
	cameraData_->worldPosition = camera.worldPosition;

	// ＜平行光源の定数バッファ＞アドレスをルート番号にセット
	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterLight,
		directionalLightResource_->GetGPUVirtualAddress());

	// ＜カメラの位置（world座標）の定数バッファ＞アドレスをルート番号にセット　（ 主に鏡面反射用 ）
	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterCamera,
		cameraResource_->GetGPUVirtualAddress());

	// ＜点光源の定数バッファ＞アドレスをルート番号にセット
	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterPointLight,
		pointLightResource_->GetGPUVirtualAddress());
}

// ════════════════════════════════════════════════════════════════════
// デバッグ用の線を集める
//   PrimitiveDrawer は線を溜めるだけなので、ここで呼んでも
//   パイプラインは切り替わらない（Draw は呼び出し側が1回だけ行う）
// ════════════════════════════════════════════════════════════════════
void GameScene::DrawGizmos() {

	PrimitiveDrawer& drawer = *context_.primitiveDrawer;

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		gameObject->DrawGizmos(drawer);
	}

// 点光源のギズモ -----------------------------------------------------------------

	//   点光源の位置を十字で表示し、影響半径を線で示す
	const Vector3& p = pointLightData_->position;
	const float r = pointLightData_->radius;
	const Vector4 lightColor{ 1.0f, 1.0f, 0.4f, 1.0f };

	//   位置を示す小さな十字（3軸）
	constexpr float kCrossSize = 0.3f;
	drawer.DrawLine3d({ p.x - kCrossSize, p.y, p.z }, { p.x + kCrossSize, p.y, p.z }, lightColor);
	drawer.DrawLine3d({ p.x, p.y - kCrossSize, p.z }, { p.x, p.y + kCrossSize, p.z }, lightColor);
	drawer.DrawLine3d({ p.x, p.y, p.z - kCrossSize }, { p.x, p.y, p.z + kCrossSize }, lightColor);

	//   影響半径を示す線（4方向）
	const Vector4 radiusColor{ 1.0f, 1.0f, 0.4f, 0.3f };
	drawer.DrawLine3d(p, { p.x + r, p.y, p.z }, radiusColor);
	drawer.DrawLine3d(p, { p.x - r, p.y, p.z }, radiusColor);
	drawer.DrawLine3d(p, { p.x, p.y, p.z + r }, radiusColor);
	drawer.DrawLine3d(p, { p.x, p.y, p.z - r }, radiusColor);
//--------------------------------------------------------------------------------

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

		if (ImGui::CollapsingHeader("PointLight")) {
			ImGui::ColorEdit4("color##point", &pointLightData_->color.x);
			ImGui::DragFloat3("position##point", &pointLightData_->position.x, 0.05f);
			ImGui::DragFloat("intensity##point", &pointLightData_->intensity, 0.01f, 0.0f, 10.0f);
			ImGui::DragFloat("radius##point", &pointLightData_->radius, 0.1f, 0.0f, 100.0f);
			ImGui::DragFloat("decay##point", &pointLightData_->decay, 0.01f, 0.0f, 10.0f);
		}
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
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
#include "Quaternion.h"
// コンポーネントファイル
#include "LightComponent.h"
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
	sphereObject_ = CreateGameObject("Sphere1");
	sphereObject_->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
	sphereObject_->AddComponent<MeshRenderer>(sphereModel_);

	sphereObject_ = CreateGameObject("Sphere2");
	sphereObject_->GetTransform().translate = { 0.0f, 3.0f, 0.0f };
	sphereObject_->AddComponent<MeshRenderer>(sphereModel_);
	// ════════════════════════════════════════
	// パーティクル
	//   他のオブジェクトと同じ3手順：箱を作る → 位置を決める → 機能を足す
	// ════════════════════════════════════════
	GameObject* testParticle = CreateGameObject("testParticle");
	testParticle->GetTransform().translate = { 0.0f, 5.0f, 0.0f };
	testParticle->AddComponent<ParticleEmitter>();

	// ════════════════════════════════════════
	// スプライト
	// ════════════════════════════════════════
	uint32_t uvCheckerTextureIndex =
		context_.textureManager->Load("resources/uvChecker.png");

	sprite_.Initialize(context_.dxCommon, context_.textureManager,
		uvCheckerTextureIndex, { 640.0f, 360.0f }, context_.winApp->GetClientSize());

	// ════════════════════════════════════════
	// カメラの位置の定数バッファ（鏡面反射用。シーンに1つ）
	//   中身は毎フレーム DrawScene で書き換える
	// ════════════════════════════════════════
	cameraResource_ = context_.dxCommon->CreateBufferResource(sizeof(CameraForGPU));
	cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));

	cameraData_->worldPosition = Vector3{ 0.0f, 0.0f, 0.0f };


	// ════════════════════════════════════════
	// ライトの個数を伝える定数バッファ
	// ════════════════════════════════════════
	lightCountResource_ = context_.dxCommon->CreateBufferResource(sizeof(LightCount));
	lightCountResource_->Map(0, nullptr, reinterpret_cast<void**>(&lightCountData_));

	// ════════════════════════════════════════
	// 平行光源の定数バッファ（シーンに1つ。モデルとスプライトで共通）
	// ════════════════════════════════════════
	directionalLightResource_ = context_.dxCommon->CreateBufferResource(
		sizeof(DirectionalLight) * kMaxDirectionalLights);
	directionalLightResource_->Map(0, nullptr,
		reinterpret_cast<void**>(&directionalLightData_));

	// ════════════════════════════════════════
	// 点光源の定数バッファ
 	// ════════════════════════════════════════
	pointLightResource_ = context_.dxCommon->CreateBufferResource(sizeof(PointLight) * kMaxPointLights);
	pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightData_));


	// ════════════════════════════════════════
	//   スポットライトの定数バッファ
	// ════════════════════════════════════════
	spotLightResource_ = context_.dxCommon->CreateBufferResource(sizeof(SpotLight) * kMaxSpotLights);
	spotLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&spotLightData_));

	// ════════════════════════════════════════
	// ライトを「オブジェクト」として置く
	//   他のオブジェクトと同じ3手順：箱を作る → 位置と向きを決める → 機能を足す
	// ════════════════════════════════════════

	//   太陽（真上から下へ）。+Z を下に向けるので X 軸まわりに 90度
	GameObject* sunObject = CreateGameObject("Sun");
	sunObject->GetTransform().translate = { 0.0f, 5.0f, 0.0f };
	sunObject->GetTransform().rotate = { kPi / 2.0f, 0.0f, 0.0f };
	sunObject->AddComponent<DirectionalLightComponent>();

	//   点光源
	GameObject* lampObject = CreateGameObject("Lamp");
	lampObject->GetTransform().translate = { 0.0f, 2.0f, 0.0f };
	lampObject->AddComponent<PointLightComponent>();

	//   スポットライト。真下を向かせると床に円が落ちて見やすい
	GameObject* spotObject = CreateGameObject("Spot");
	spotObject->GetTransform().translate = { 0.0f, 4.0f, 0.0f };
	spotObject->GetTransform().rotate = { kPi / 2.0f, 0.0f, 0.0f };
	spotObject->AddComponent<SpotLightComponent>();
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
// モデルの階層を GameObject ツリーに展開する
//   Blender で置いたオブジェクトが、そのままヒエラルキーに並ぶ
// ════════════════════════════════════════════════════════════════════
GameObject* GameScene::CreateObjectFromModel(const std::string& name, Model* model) {

	// ────────────────────────────────────────────────────────────────
	// 階層を持たないモデルは GameObject 1個で済ませる
	//   PrimitiveModel が作った球などは nodes が空
	//   MeshRenderer に Mesh を指定しなければ、モデル全体が描かれる
	// ────────────────────────────────────────────────────────────────
	if (model == nullptr || model->GetNodeCount() == 0) {
		GameObject* object = CreateGameObject(name);
		object->AddComponent<MeshRenderer>(model);
		return object;
	}

	// ────────────────────────────────────────────────────────────────
	// 階層があるときは、ルートノード（添字0）からたどる
	// ────────────────────────────────────────────────────────────────
	GameObject* root = BuildNodeObject(model, 0, nullptr);

	//   ルートノードの名前は "RootNode" など機械的なものが入っている
	//     ヒエラルキーで分かりやすいよう、呼び出し側が付けた名前に差し替える
	root->SetName(name);

	return root;
}

// ════════════════════════════════════════════════════════════════════
// ノード1つを GameObject にして、子を再帰で作る
// ════════════════════════════════════════════════════════════════════
GameObject* GameScene::BuildNodeObject(Model* model, uint32_t nodeIndex, GameObject* parent) {

	const ModelNode& node = model->GetNode(nodeIndex);

	GameObject* object = CreateGameObject(node.name);
	object->SetParent(parent);   // nullptr なら親なし

	// ────────────────────────────────────────────────────────────────
	// ノードの行列を Transform に移す
	//   GameObject は行列ではなく scale/rotate/translate を持つので分解が必要
	//   分解した値は、そのままインスペクタで編集できる
	// ────────────────────────────────────────────────────────────────
	object->GetTransform() = DecomposeAffineMatrix(node.localMatrix);

	// ────────────────────────────────────────────────────────────────
	// Mesh を持つノードだけ描画コンポーネントを足す
	//   持たないノード（ルートや Blender のエンプティ）は変換だけの箱になる
	// ────────────────────────────────────────────────────────────────
	if (!node.meshIndices.empty()) {
		MeshRenderer* renderer = object->AddComponent<MeshRenderer>(model);
		renderer->SetMeshIndices(node.meshIndices);
	}

	// ────────────────────────────────────────────────────────────────
	// 子を再帰で作る
	//	　　ここで node への参照を持ち越して大丈夫な理由
	//     伸びるのは gameObjects_（GameScene 側）で、model->nodes_ は変わらない
	//     しかも gameObjects_ は unique_ptr の配列なので、
	//     引っ越しても GameObject 本体は動かず、生ポインタは生き続ける
	// ────────────────────────────────────────────────────────────────
	for (const uint32_t childIndex : node.children) {
		BuildNodeObject(model, childIndex, object);
	}

	return object;
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
	// ⑥-a 全オブジェクトの更新
	// ════════════════════════════════════════
	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		gameObject->Update();
	}


	// ════════════════════════════════════════
	// ⑥-b ライトを集めて定数バッファに詰める
	//      オブジェクトの更新が終わった後でないと、位置が1フレーム古くなる
	// ════════════════════════════════════════
	CollectLights();

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

	//   グリッドと座標軸はエディタの備品。出すかどうかも Editor が判断する
	editor_.DrawEditorGizmos();

	// オブジェクトのデバッグ線はシーン側が積む。出すかどうかはエディタに訊く
	//   デバッグ表示は編集ビューのときだけ。ゲーム画面には出さない
	if (editor_.IsGizmosVisible() && editor_.GetViewMode() == ViewMode::Edit) {
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
	/*
	パーティクルは別のパイプラインを使うので、③のループの中では描けない。
	③の途中でパイプラインを切り替えると、後続の MeshRenderer が壊れる
	*/

	//	入れ物はループの外で1つ作って使い回す
	//	中で作ると、オブジェクトの数だけメモリ確保が起きる
	std::vector<ParticleEmitter*> emitters;

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
		//   親が止まっていれば、子のパーティクルも描かない
		if (!gameObject->IsActiveInHierarchy()) {
			continue;
		}

		//  ParticleEmitter を持つオブジェクトだけ、別ループで描く
		gameObject->GetComponents<ParticleEmitter>(emitters);
		
		//   1つも持っていないオブジェクトはスキップ
		for (ParticleEmitter* emitter : emitters) {

			if (!emitter->IsEnabled()) {
				continue;
			}

			emitter->DrawParticles(camera);
		}
	}

	// ⑤ スプライトは Object3d のパイプラインを使うので、戻してから描く
	context_.object3dPipeline->SetPipeline();
	SetSceneConstants(camera);

	// ⑥ スプライト
	sprite_.Draw();
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

	// --- CBV として渡す ----------------------------
	
	// ＜ライトの個数の定数バッファ＞アドレスをルート番号にセット
	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterLightCount,
		lightCountResource_->GetGPUVirtualAddress());

	// ＜カメラの位置（world座標）の定数バッファ＞アドレスをルート番号にセット　（ 主に鏡面反射用 ）
	commandList->SetGraphicsRootConstantBufferView(
		Object3dPipeline::kRootParameterCamera,
		cameraResource_->GetGPUVirtualAddress());

	// --- StructuredBuffer (配列） なので SRV として渡す ----------------------------

	// ＜平行光源の定数バッファ＞アドレスをルート番号にセット
	commandList->SetGraphicsRootShaderResourceView(
		Object3dPipeline::kRootParameterDirectionalLights,
		directionalLightResource_->GetGPUVirtualAddress());

	// ＜点光源の定数バッファ＞アドレスをルート番号にセット
	commandList->SetGraphicsRootShaderResourceView(
		Object3dPipeline::kRootParameterPointLights,
		pointLightResource_->GetGPUVirtualAddress());

	// ＜スポットライトの定数バッファ＞アドレスをルート番号にセット
	commandList->SetGraphicsRootShaderResourceView(
		Object3dPipeline::kRootParameterSpotLights,
		spotLightResource_->GetGPUVirtualAddress());
}

// ════════════════════════════════════════════════════════════════════
// シーン内のライトを集めて定数バッファに詰める
//   シェーダのスロットは種類ごとに1つなので、見つかった最後の1つを使う
//   1つも無ければ intensity = 0 で消灯する
// ════════════════════════════════════════════════════════════════════
void GameScene::CollectLights() {

	//   既定値のリセットは不要になった
	//     「使わない枠」は個数で切るので、中身がゴミでも読まれない
	uint32_t directionalCount = 0;
	uint32_t pointCount = 0;
	uint32_t spotCount = 0;

	for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {

		//   親が止まっていれば、そのライトも消える
		if (!gameObject->IsActiveInHierarchy()) {
			continue;
		}


		//   平行光源
		DirectionalLightComponent* directional =
			gameObject->GetComponent<DirectionalLightComponent>();
		if (directional != nullptr && directional->IsEnabled()
			&& directionalCount < kMaxDirectionalLights) {
			directionalLightData_[directionalCount] = directional->MakeGpuData();
			++directionalCount;
		}

		//   点光源
		PointLightComponent* point = gameObject->GetComponent<PointLightComponent>();
		if (point != nullptr && point->IsEnabled()
			&& pointCount < kMaxPointLights) {
			pointLightData_[pointCount] = point->MakeGpuData();
			++pointCount;
		}

		//   スポットライト
		SpotLightComponent* spot = gameObject->GetComponent<SpotLightComponent>();
		if (spot != nullptr && spot->IsEnabled()
			&& spotCount < kMaxSpotLights) {
			spotLightData_[spotCount] = spot->MakeGpuData();
			++spotCount;
		}
	}

	// 詰め終わってから個数を書く
	lightCountData_->directionalCount = directionalCount;
	lightCountData_->pointCount = pointCount;
	lightCountData_->spotCount = spotCount;
	lightCountData_->padding = 0;
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

		// ───── テクスチャの切り出し（ピクセル指定） ─────
		ImGui::Separator();
		const Vector2 textureResolution = sprite_.GetTextureResolution();
		ImGui::TextDisabled("切り出し（元画像 %.0f x %.0f）",
			textureResolution.x, textureResolution.y);

		Vector2 rectLeftTop = sprite_.GetTextureRectLeftTop();
		Vector2 rectSize = sprite_.GetTextureRectSize();

		//   どちらを動かしても反映したいので、|| ではなく | で両方評価する
		//   || だと左の DragFloat2 が true のとき右が描かれなくなる
		bool rectChanged = false;
		rectChanged |= ImGui::DragFloat2("切り出し 左上", &rectLeftTop.x, 1.0f, 0.0f, 0.0f, "%.0f");
		rectChanged |= ImGui::DragFloat2("切り出し サイズ", &rectSize.x, 1.0f, 0.0f, 0.0f, "%.0f");

		if (rectChanged) {
			sprite_.SetTextureRect(rectLeftTop, rectSize);
		}

		if (ImGui::Button("画像全体に戻す")) {
			sprite_.ResetTextureRect();
		}
		ImGui::SameLine();
		if (ImGui::Button("表示サイズを切り出しに合わせる")) {
			sprite_.AdjustTextureSize();
		}

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

		//   同じ getter を3回呼ばず、参照を1つ取って使う
		const Vector3& cameraPosition = debugCamera.GetWorldPosition();
		const Vector3& cameraTarget = debugCamera.GetTarget();

		ImGui::Text("pos      : %.2f, %.2f, %.2f",
			cameraPosition.x, cameraPosition.y, cameraPosition.z);
		ImGui::Text("target   : %.2f, %.2f, %.2f",
			cameraTarget.x, cameraTarget.y, cameraTarget.z);
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
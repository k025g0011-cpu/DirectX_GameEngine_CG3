#include <Windows.h>
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <format>
#include <d3d12.h>
#include <cassert>
#include <vector>
#include "MathUtility.h"
#include "Input.h"
#include "DebugCamera.h"
#include "PrimitiveDrawer.h"
#include "Logger.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "TextureManager.h" 
#include "Model.h"
#include "Object3dStructs.h"
#include "CrashHandler.h"
#include "ObjLoader.h"
#include "PrimitiveModel.h"
#include "Object3dPipeline.h"
#include "Sprite.h"
#include "Camera.h"
// imgui用のincludeと外部関数
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

// ComPtr（COMオブジェクトの参照カウントを自動で管理してくれるクラス）用
#include <wrl.h>



// ════════════════════════════════════════════════════════════════════
// TODO: ここに残っている Engine 側のコードを順に引っ越す
//   [ ] R-E  ModelInstance（複数体描画）
//   [ ] R-F  ModelManager（モデルの共有）
//   [ ] R-G  ゲームの中身                  → Game/GameScene
//   ────────── そのうち ──────────
//   [ ] 平行光源                          → Engine/3d/DirectionalLightObject
//   [ ] ImGui の初期化・終了               → Engine/editor/ImGuiManager
//   [ ] DrawGrid                          → Engine/editor/GridRenderer
// ════════════════════════════════════════════════════════════════════











// ════════════════════════════════════════
// Microsoft::WRL::ComPtr は毎回書くと長いので、ComPtr と短縮して使えるようにする
// ※ using namespace ではなく、必要な名前だけを引き込むこと（名前衝突を防ぐため）
// ════════════════════════════════════════
using Microsoft::WRL::ComPtr;


// ════════════════════════════════════════
// 基準グリッド（Blenderの床グリッド相当）の頂点を作る
//   中心から ±kGridHalfCount マス、間隔 kGridSpacing
//   X軸=赤 / Y軸=緑 / Z軸=青、それ以外はグレー
// ════════════════════════════════════════
const int32_t  kGridHalfCount = 10;     // 中心から何マス伸ばすか
const float    kGridSpacing = 1.0f;   // 1マスの大きさ

// ════════════════════════════════════════════════════════════════════
// グリッドを PrimitiveDrawer に予約する
//   旧 CreateGridVertices を「配列に書く」から「線を頼む」に書き換えたもの
// ════════════════════════════════════════════════════════════════════
void DrawGrid(PrimitiveDrawer& drawer) {
	const Vector4 kGray = { 0.35f, 0.35f, 0.35f, 1.0f };
	const Vector4 kRed = { 0.75f, 0.25f, 0.25f, 1.0f };
	const Vector4 kGreen = { 0.25f, 0.65f, 0.35f, 1.0f };
	const Vector4 kBlue = { 0.25f, 0.45f, 0.80f, 1.0f };

	const float half = kGridHalfCount * kGridSpacing;

	for (int32_t n = -kGridHalfCount; n <= kGridHalfCount; ++n) {
		const float p = n * kGridSpacing;

		// Z方向に伸びる線（X = p の位置）
		const Vector4 colorAlongZ = (n == 0) ? kBlue : kGray;
		drawer.DrawLine3d({ p, 0.0f, -half }, { p, 0.0f, half }, colorAlongZ);

		// X方向に伸びる線（Z = p の位置）
		const Vector4 colorAlongX = (n == 0) ? kRed : kGray;
		drawer.DrawLine3d({ -half, 0.0f, p }, { half, 0.0f, p }, colorAlongX);
	}

	// Y軸（上向き）
	drawer.DrawLine3d({ 0.0f, 0.0f, 0.0f }, { 0.0f, half, 0.0f }, kGreen);
}


// Windowsアプリでのエントリーポイント（main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// ════════════════════════════════════════
	// リークチェッカー
	// 一番最初に作る＝デストラクタが一番最後に呼ばれるので、
	// すべてのComPtrが解放された後にリークチェックが走る
	// ════════════════════════════════════════
	D3DResourceLeakChecker leakCheck;

	// クラッシュ時にダンプを出力するよう登録する（できるだけ早い段階で）
	RegisterCrashHandler();

	// ログのディレクトリを用意 (image_247d6d)
	std::filesystem::create_directory("logs");

	// 現在時刻を取得 (UTC時刻) (image_247d8c)
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	// ログファイルの名前にコンマ何秒はいらないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	// 日本時間 (PCの設定時間) に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	// formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	// 時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";
	// ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);


	// ════════════════════════════════════════════════════════════════
	// ウィンドウの生成
	// ════════════════════════════════════════════════════════════════
	WinApp winApp;
	winApp.Initialize();


	// 新しく作ったLog関数を使ってファイル出力とデバッグ出力を同時に行う
	Log(logStream, "hello DirectX");

	// ════════════════════════════════════════════════════════════════
	// DirectX 基盤の初期化
	// ════════════════════════════════════════════════════════════════
	DirectXCommon dxCommon;
	dxCommon.Initialize(&winApp, logStream);

	// ────────────────────────────────────────────────────────────────
	// 以降のコードを今まで通りの名前で書けるようにする別名
	//   実体は dxCommon の中。参照なのでコピーは起きない
	// ────────────────────────────────────────────────────────────────
	const ComPtr<ID3D12Device>& device = dxCommon.GetDevice();
	const ComPtr<ID3D12GraphicsCommandList>& commandList = dxCommon.GetCommandList();
	const ComPtr<ID3D12DescriptorHeap>& srvDescriptorHeap = dxCommon.GetSrvDescriptorHeap();


	// ════════════════════════════════════════════════════════════════
	// テクスチャマネージャの初期化
	// ════════════════════════════════════════════════════════════════
	TextureManager textureManager;
	textureManager.Initialize(&dxCommon);



	// ════════════════════════════════════════════════════════════════
	// 3D描画パイプラインの初期化
	//   RootSignature・InputLayout・シェーダー・PSO（ブレンド6種）を作る
	// ════════════════════════════════════════════════════════════════
	Object3dPipeline object3dPipeline;
	object3dPipeline.Initialize(&dxCommon);



	// ════════════════════════════════════════════════════
	// モデルを用意する
	// 使いたい方のコメントを外す（ModelDataを返すので以降のコードは共通）
	// ════════════════════════════════════════════════════
	//ModelData modelData = ObjLoader::Load("resources", "multiMaterial.obj");
	ModelData modelData = PrimitiveModel::CreateSphere(16,"resources/uvChecker.png");

	// ════════════════════════════════════════════════════
	// Model クラスに渡す
	//   頂点バッファ / インデックスバッファ / 行列CB / マテリアルCB / テクスチャ読み込み
	//   これら全部を Model が中でやってくれる
	//   ※ textureManager を使うので、その初期化（581行付近）より後でなければならない
	// ════════════════════════════════════════════════════
	Model model;
	model.Initialize(&dxCommon, &textureManager, modelData);

	// Transform変数を作る
	Transform transformModel{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	// ════════════════════════════════════════
	// カメラ
	//   既定で z = -5 の位置から z+ 方向を見る
	// ════════════════════════════════════════
	Camera camera;
	camera.Initialize();


	// ════════════════════════════════════════
	// 平行光源用のResourceを作る
	// シーンに1つだけ。球とSpriteで共通
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> directionalLightResource = dxCommon.CreateBufferResource(sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };  // 白
	directionalLightData->direction = Vector3{ 0.0f, -1.0f, 0.0f };   // 真上から下へ
	directionalLightData->intensity = 1.0f;


	Transform uvTransformModel{ // UV移動用
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f },
	};
	

	Transform uvTransformSprite{ // UV移動用
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f },
	};


	// ════════════════════════════════════════════════════════════════
	// Texture の読み込み
	//   パスを渡すと「SRV ヒープの何番に置いたか」が返る
	//   読み込み・VRAM への転送・SRV の生成は TextureManager がやってくれる
	// ════════════════════════════════════════════════════════════════
	uint32_t uvCheckerTextureIndex = textureManager.Load("resources/uvChecker.png");

	// ════════════════════════════════════════════════════════════════
	// スプライトを作る
	//   頂点・インデックス・定数バッファ・行列は Sprite が中で用意する
	//   ※ テクスチャ番号が要るので、Load より後でなければならない
	// ════════════════════════════════════════════════════════════════
	Sprite sprite;
	sprite.Initialize(&dxCommon, &textureManager, uvCheckerTextureIndex, { 640.0f, 360.0f });

	// ════════════════════════════════════════
	// ImGuiの初期化
	// ════════════════════════════════════════
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark(); // 他の色種類もある＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞試してみ　by　過去の自分
	ImGui_ImplWin32_Init(winApp.GetHwnd());

	// ImGui のフォントテクスチャ用に1つ確保する
	uint32_t imguiSrvIndex = dxCommon.AllocateSrv();

	ImGui_ImplDX12_Init(
		device.Get(),
		DirectXCommon::kBackBufferCount,          // swapChainDesc.BufferCount から変更
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,          // rtvDesc.Format から変更
		srvDescriptorHeap.Get(),
		dxCommon.GetSrvCpuDescriptorHandle(imguiSrvIndex),
		dxCommon.GetSrvGpuDescriptorHandle(imguiSrvIndex));
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif


	// ════════════════════════════════════════
	// 入力・デバッグカメラの初期化
	// ════════════════════════════════════════
	Input input;
	input.Initialize(winApp.GetHInstance(), winApp.GetHwnd());

	DebugCamera debugCamera;
	debugCamera.Initialize(&input, WinApp::kClientWidth, WinApp::kClientHeight);

	// デバッグカメラの有効／無効（F1キーで切り替え）
	bool useDebugCamera = false;

	// ════════════════════════════════════════════════════════════════
	// プリミティブドロワーの初期化
	// ════════════════════════════════════════════════════════════════
	PrimitiveDrawer primitiveDrawer;
	primitiveDrawer.Initialize(&dxCommon);


	// ウィンドウの×ボタンが押されるまでループ
	while (true) {
		if (winApp.ProcessMessage()) {
			break;
		}
		// ゲームの処理

		// ════════════════════════════════════════
		// 入力の更新（毎フレーム処理の先頭）
		// ════════════════════════════════════════
		input.Update();

		// ────────────────────────────────────────
		// ImGui開始処理
		// ────────────────────────────────────────
#ifdef USE_IMGUI
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
#endif

		// ════════════════════════════════════════
		// デバッグカメラの更新
		//   WantCaptureMouse は NewFrame() の中で計算されるので、必ずその後で読む
		// ════════════════════════════════════════
		// F1キーで通常カメラと切り替え
		if (input.TriggerKey(DIK_F1)) {
			useDebugCamera = !useDebugCamera;
		}

		// ImGuiを操作している最中はカメラを動かさない
#ifdef USE_IMGUI
		bool imguiUsingMouse = ImGui::GetIO().WantCaptureMouse;
#else
		bool imguiUsingMouse = false;
#endif
		if (useDebugCamera && !imguiUsingMouse) {
			debugCamera.Update();
		}

		// カメラの行列を更新する
		camera.Update();

		// カメラとスフィアの設定
#ifdef USE_IMGUI
		ImGui::Begin("Settings");
		Vector3 cameraTranslate = camera.GetTranslate();
		if (ImGui::DragFloat3("camera translate", &cameraTranslate.x, 0.01f)) {
			camera.SetTranslate(cameraTranslate);
		}
		Vector3 cameraRotate = camera.GetRotate();
		if (ImGui::DragFloat3("camera rotate", &cameraRotate.x, 0.01f)) {
			camera.SetRotate(cameraRotate);
		}
		ImGui::DragFloat3("sphere rotate", &transformModel.rotate.x, 0.01f);
		// マテリアルの数ぶんループ（今は1個）
		for (uint32_t i = 0; i < model.GetMaterialCount(); ++i) {
			ImGui::PushID(int(i));                                     // 同じラベルを区別する
			ImGui::ColorEdit4("model color", &model.GetMaterialData(i)->color.x);
			ImGui::PopID();
		}
		ImGui::DragFloat3("light direction", &directionalLightData->direction.x, 0.01f);
		ImGui::DragFloat("light intensity", &directionalLightData->intensity, 0.01f);
		ImGui::ColorEdit4("light color", &directionalLightData->color.x);
		directionalLightData->direction = Normalize(directionalLightData->direction);   // 毎フレーム正規化

		// ────────────────────────────────────────
		// SphereとSpriteのUVTransform
		// UV座標はxy平面なので、移動・拡縮はxyの2成分、回転はz軸まわりのみ
		// ────────────────────────────────────────

		// スプライトの位置・回転・アンカーポイント
		// ────────────────────────────────────────
		Vector2 spritePosition = sprite.GetPosition();
		if (ImGui::DragFloat2("sprite position", &spritePosition.x, 1.0f)) {
			sprite.SetPosition(spritePosition);
		}

		float spriteRotation = sprite.GetRotation();
		if (ImGui::SliderAngle("sprite rotate", &spriteRotation)) {
			sprite.SetRotation(spriteRotation);
		}

		Vector2 spriteAnchor = sprite.GetAnchorPoint();
		if (ImGui::DragFloat2("sprite anchor", &spriteAnchor.x, 0.01f, 0.0f, 1.0f)) {
			sprite.SetAnchorPoint(spriteAnchor);
		}
		ImGui::DragFloat2("SpriteUVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("SpriteUVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("SpriteUVRotate", &uvTransformSprite.rotate.z);

		ImGui::DragFloat2("ModelUVTranslate", &uvTransformModel.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("ModelUVScale", &uvTransformModel.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("ModelUVRotate", &uvTransformModel.rotate.z);
		ImGui::Text("Lines: %u", primitiveDrawer.GetLineCount());


		ImGui::Text("Materials: %u  Meshes: %u",
			model.GetMaterialCount(), model.GetMeshCount());

		ImGui::End();

		// ────────────────────────────────────────
		// デバッグカメラの状態表示
		// ────────────────────────────────────────
		ImGui::Begin("DebugCamera");
		ImGui::Text("F1 : %s", useDebugCamera ? "ON" : "OFF");
		ImGui::Text("pos    : %.2f, %.2f, %.2f",
			debugCamera.GetTranslation().x, debugCamera.GetTranslation().y, debugCamera.GetTranslation().z);
		ImGui::Text("target : %.2f, %.2f, %.2f",
			debugCamera.GetTarget().x, debugCamera.GetTarget().y, debugCamera.GetTarget().z);
		ImGui::Text("dist   : %.2f", debugCamera.GetDistance());
		ImGui::Separator();
		ImGui::Text("mouse  : %.0f, %.0f  wheel %.1f",
			input.GetMouseMove().x, input.GetMouseMove().y, input.GetMouseWheel());
		ImGui::End();
#endif


		// ────────────────────────────────────────
		// ＜3Dモデル＞     レンダ（クリップ空間まで変換）（３D）
		// ────────────────────────────────────────
		Matrix4x4 worldMatrixModel = MakeAffineMatrix(transformModel.scale, transformModel.rotate, transformModel.translate); // 拡縮、回転、移動
		// ────────────────────────────────────────
		// 使用するカメラを選ぶ（F1 で切り替え）
		// ────────────────────────────────────────
		const Matrix4x4& viewProjectionMatrix = useDebugCamera
			? debugCamera.GetViewProjectionMatrix()
			: camera.GetViewProjectionMatrix();


		// ────────────────────────────────────────
		// ＜3Dモデル＞　UVTransform行列を作る
		// 座標変換の基本どおり Scale → Rotate → Translate (SRT) の順に掛ける
		// ────────────────────────────────────────
		Matrix4x4 uvTransformMatrixModel = MakeAffineMatrix(uvTransformModel.scale, uvTransformModel.rotate, uvTransformModel.translate);
		// 全マテリアルに同じ UV 変換を適用する（ImGui のスライダーが1組だけなので）
		for (uint32_t i = 0; i < model.GetMaterialCount(); ++i) {
			model.GetMaterialData(i)->uvTransform = uvTransformMatrixModel;
		}


		// ────────────────────────────────────────
		// ＜スプライト＞　UVTransform行列を作る
		// 座標変換の基本どおり Scale → Rotate → Translate (SRT) の順に掛ける
		// ────────────────────────────────────────
		Matrix4x4 uvTransformMatrixSprite = MakeAffineMatrix(
			uvTransformSprite.scale, uvTransformSprite.rotate, uvTransformSprite.translate);
		sprite.GetMaterialData()->uvTransform = uvTransformMatrixSprite;

#ifdef USE_IMGUI
		// 開発用UIの処理
		ImGui::ShowDemoWindow();

		// ImGuiの内部コマンドを生成する（描画処理の前）
		ImGui::Render();
#endif



		// ────────────────────────────────────────
		// 描画前処理（バリア・クリア・各種設定）
		// ────────────────────────────────────────
		dxCommon.PreDraw();	


		// ════════════════════════════════════════════════════════════
		// PrimitiveDrawer（クラス） による描画
		// ════════════════════════════════════════════════════════════
		// 線の予約（毎フレーム、先頭でリセット）
		primitiveDrawer.Reset();
		DrawGrid(primitiveDrawer);
		primitiveDrawer.Draw(viewProjectionMatrix);



		// パイプラインを設定する（RootSignature + PSO + トポロジ）
		object3dPipeline.SetPipeline(); // 書き方（Object3dPipeline::BlendMode::Normalみたいな感じ）

		// 平行光源はシーン共通なので、描画の最初に1回設定すればよい
		commandList->SetGraphicsRootConstantBufferView(Object3dPipeline::kRootParameterLight, directionalLightResource->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[3]を介して、 directionalLightResourceアドレスを渡す


		// ────────────────────────────────────────
		// モデルを描画する
		//   マテリアル(root 0) / 座標変換(root 1) / テクスチャ(root 2) の設定と
		//   DrawCall は Model::Draw の中
		//   平行光源(root 3) はシーン共通なので上で1回設定済み
		// ────────────────────────────────────────
		model.Draw(worldMatrixModel, viewProjectionMatrix);

		// ────────────────────────────────────────
		// スプライトを描画する
		//   パイプラインはモデルと共通なので設定済み
		// ────────────────────────────────────────
		sprite.Draw();



		// ===== ImGuiの描画（これも一応BackBufferリソースに書き込む要素なのでbarrierのPRESENTへの遷移よりは上に書く） =====　
#ifdef USE_IMGUI
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get()); // ImGui::Renderのデータと使っているコマンドリストを引数に
#endif
		// ────────────────────────────────────────
		// 描画後処理（バリア・実行・Present・フェンス待ち）
		// ────────────────────────────────────────
		dxCommon.PostDraw();


	}

	// ════════════════════════════════════════
	// 終了処理
	// COMオブジェクトはComPtrが自動で解放するので、ここでやることはごくわずか
	// ════════════════════════════════════════
	// ─── ImGui（内部でdevice・srvDescriptorHeapを掴んでいるので、必ず先に終了させる）───
#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif


	// ─── ComPtrで扱えないものだけ、ここで後始末する ───
	// ════════════════════════════════════════════════════════════════
	// windows後片付け
	// ════════════════════════════════════════════════════════════════
	dxCommon.Finalize();
	winApp.Finalize();


	return 0;
}
#include <Windows.h>
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream> 
#include <chrono>
#include <cmath> 
#include <cstring> 
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxcapi.h>
#include <vector>
#include <numbers>
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
// imgui用のincludeと外部関数
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

// ComPtr（COMオブジェクトの参照カウントを自動で管理してくれるクラス）用
#include <wrl.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")


// ════════════════════════════════════════════════════════════════════
// TODO: ここに残っている Engine 側のコードを順に引っ越す
//   [ ] R-C  スプライト一式                → Engine/2d/Sprite
//   [ ] R-D  カメラ行列                    → Engine/3d/Camera
//   [ ] R-G  ゲームの中身                  → Game/GameScene
// ════════════════════════════════════════════════════════════════════











// ════════════════════════════════════════
// Microsoft::WRL::ComPtr は毎回書くと長いので、ComPtr と短縮して使えるようにする
// ※ using namespace ではなく、必要な名前だけを引き込むこと（名前衝突を防ぐため）
// ════════════════════════════════════════
using Microsoft::WRL::ComPtr;



// ════════════════════════════════════════
// 線描画用の頂点
//   位置と色だけ。テクスチャ座標も法線も使わない
// ════════════════════════════════════════
struct LineVertex {   // 全体＜28＞バイト
	Vector3 position;   // 座標 (float<3> * 3) = 12バイト  
	Vector4 color;      // 色   (float<4> * 4) = 16バイト
};




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


	// ════════════════════════════════════════
	// デバッグレイヤーの有効化
	// 必ずDevice生成より前に行う(後からでは適用されない)
	// ════════════════════════════════════════
#ifdef _DEBUG
	ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif


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
	// カメラのTransformを作る。z = -5 の位置で z+ 方向を向いているカメラ
	Transform cameraTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };




	// ════════════════════════════════════════════════════
	// スプライトの VertexResource を生成する
	// ════════════════════════════════════════════════════
	ComPtr<ID3D12Resource> vertexResourceSprite = dxCommon.CreateBufferResource(sizeof(VertexData) * 4);

	// ════════════════════════════════════════════════════
	// スプライトの VertexBufferView を作成する
	// ════════════════════════════════════════════════════
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	// リソースの先頭のアドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;
	// 1頂点あたりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);


	// ════════════════════════════════════════════════════
	// スプライトの IndexResource を生成する
	// ════════════════════════════════════════════════════
	ComPtr<ID3D12Resource> indexResourceSprite = dxCommon.CreateBufferResource(sizeof(uint32_t) * 6);

	// ════════════════════════════════════════════════════
	// スプライトの indexBufferView を生成する
	// ════════════════════════════════════════════════════
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;   // インデックス1つが32bit整数


	// ════════════════════════════════════════════════════
	// スプライトの頂点データを書き込む
	// ════════════════════════════════════════════════════
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// [0] 左下
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f };
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };
	// [1] 左上
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f };
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };
	// [2] 右下
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f };
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };
	// [3] 右上
	vertexDataSprite[3].position = { 640.0f, 0.0f, 0.0f };
	vertexDataSprite[3].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };

	// ２か所の頂点が重なってるのでデータを再利用することになる


	// ════════════════════════════════════════════════════
	// スプライトのインデックスデータを書き込む
	// ════════════════════════════════════════════════════
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;  indexDataSprite[1] = 1;  indexDataSprite[2] = 2;   // 1枚目：左下→左上→右下
	indexDataSprite[3] = 1;  indexDataSprite[4] = 3;  indexDataSprite[5] = 2;   // 2枚目：左上→右上→右下


	// ════════════════════════════════════════
	// Sprite用のTransformationMatrixのResourceとTransformを作る
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> transformationMatrixResourceSprite = dxCommon.CreateBufferResource(sizeof(TransformationMatrix));
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	transformationMatrixDataSprite->WVP = MakeIdentity4x4();
	transformationMatrixDataSprite->World = MakeIdentity4x4();
	// CPUで動かす用のTransform
	Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };



	// ════════════════════════════════════════
	// グリッドのWVP行列用（transformationMatrix）リソース
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> transformationMatrixResourceGrid =
		dxCommon.CreateBufferResource(sizeof(TransformationMatrix));

	TransformationMatrix* transformationMatrixDataGrid = nullptr;
	transformationMatrixResourceGrid->Map(0, nullptr,
		reinterpret_cast<void**>(&transformationMatrixDataGrid));
	transformationMatrixDataGrid->WVP = MakeIdentity4x4();
	transformationMatrixDataGrid->World = MakeIdentity4x4();


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
	// -----------------------------------------
	// スプライト用マテリアル
	// -----------------------------------------
	ComPtr<ID3D12Resource> spriteMaterialResource = dxCommon.CreateBufferResource(sizeof(Material));
	// マテリアルにデータを書き込む
	Material* spriteMaterialData = nullptr;
	// 書き込むためのアドレスを取得
	spriteMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&spriteMaterialData)); // CPUからでもデータを書ける住所をmaterialDataに受け取る
	// 今回は赤を書き込んでみる
	spriteMaterialData->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };   // 白
	spriteMaterialData->enableLighting = false;                      // Lightingしない
	spriteMaterialData->uvTransform = MakeIdentity4x4();

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

		// カメラとスフィアの設定
#ifdef USE_IMGUI
		ImGui::Begin("Settings");
		ImGui::DragFloat3("camera translate", &cameraTransform.translate.x, 0.01f);
		ImGui::DragFloat3("camera rotate", &cameraTransform.rotate.x, 0.01f);
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
		// ビュー行列・射影行列（通常カメラ / デバッグカメラの切り替え）
		// ────────────────────────────────────────
		Matrix4x4 viewMatrixModel;
		Matrix4x4 projectionMatrix;

		if (useDebugCamera) {
			// デバッグカメラが自前で計算済みのものを受け取るだけ
			viewMatrixModel = debugCamera.GetViewMatrix();
			projectionMatrix = debugCamera.GetProjectionMatrix();
		} else {
			Matrix4x4 cameraMatrixModel = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate); // ワールド基準で、カメラはどこにいるか
			viewMatrixModel = Inverse(cameraMatrixModel);  // カメラ基準で、ワールドはどこにあるか(ワールドをカメラに合わせる）
			/*
			  やりたいこと    ： カメラ基準の座標が欲しい
			  計算方法       ： 世界全体を、カメラが原点に来るようにずらす
			  結果          ： ずらした後の座標 ＝ カメラ基準の座標
			*/

			projectionMatrix = MakePerspectiveMatrix(0.45f, float(WinApp::kClientWidth) / float(WinApp::kClientHeight), 0.1f, 100.0f); // 透視投影 で遠いほど小さく見える処理を加え、２D上でも奥行を出す（ 視錐台切り取りカメラ ）
		}
		/*
		* MakePerspectiveMatrix(0.45f, 1280.0f / 720.0f, 0.1f, 100.0f);
								   ↑         ↑            ↑      ↑
								  fovY    aspectRatio   nearClip farClip


			引数	            値	                    意味
			fovY	        0.45（ラジアン、約26度）	縦方向の画角。カメラのレンズの広さ
			aspectRatio	    1.778（16:9）	        画面の横÷縦
			nearClip	    0.1	                    これより近いものは映さない
			farClip	        100.0	                これより遠いものは映さない
		*/



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
		// ＜スプライト＞　レンダ（クリップ空間まで変換）（２D）
		// ────────────────────────────────────────
		// Sprite用のWorldViewProjectionMatrixを作る
		Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
		Matrix4x4 viewMatrixSprite = MakeIdentity4x4();   // 2D用カメラ（原点・動かさない）
		Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 100.0f); // 平行投影 で完全にカメラから平行に映す処理（ 直方体カメラ＜0〜1280 × 0〜720 × 0〜100＞）
		transformationMatrixDataSprite->WVP = MakeWVPMatrix(worldMatrixSprite, viewMatrixSprite, projectionMatrixSprite);
		transformationMatrixDataSprite->World = worldMatrixSprite;

		// ────────────────────────────────────────
		// ＜スプライト＞　UVTransform行列を作る
		// 座標変換の基本どおり Scale → Rotate → Translate (SRT) の順に掛ける
		// ────────────────────────────────────────
		Matrix4x4 uvTransformMatrixSprite = MakeAffineMatrix(uvTransformSprite.scale, uvTransformSprite.rotate, uvTransformSprite.translate);
		spriteMaterialData->uvTransform = uvTransformMatrixSprite;
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

		primitiveDrawer.Draw(viewMatrixModel * projectionMatrix);


		// パイプラインを設定する（RootSignature + PSO + トポロジ）
		object3dPipeline.SetPipeline(); // Object3dPipeline::BlendMode::Normal


		// 平行光源はシーン共通なので、描画の最初に1回設定すればよい
		commandList->SetGraphicsRootConstantBufferView(Object3dPipeline::kRootParameterLight, directionalLightResource->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[3]を介して、 directionalLightResourceアドレスを渡す


		// ────────────────────────────────────────
		// モデルを描画する
		//   マテリアル(root 0) / 座標変換(root 1) / テクスチャ(root 2) の設定と
		//   DrawCall は Model::Draw の中
		//   平行光源(root 3) はシーン共通なので上で1回設定済み
		// ────────────────────────────────────────
		model.Draw(worldMatrixModel, viewMatrixModel* projectionMatrix);



		// ────────────────────────────────────────
		// スプライトを描画する
		// ────────────────────────────────────────

		commandList->SetGraphicsRootConstantBufferView(Object3dPipeline::kRootParameterMaterial, spriteMaterialResource->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[0]を介して、spriteMaterialResourceアドレスを渡す
		commandList->SetGraphicsRootDescriptorTable(
			Object3dPipeline::kRootParameterTexture, textureManager.GetSrvGpuHandle(uvCheckerTextureIndex));
		commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);   // スプライト用VBVを設定
		commandList->IASetIndexBuffer(&indexBufferViewSprite);            // スプライト用IBVを設定
		commandList->SetGraphicsRootConstantBufferView(Object3dPipeline::kRootParameterTransform, transformationMatrixResourceSprite->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[1]を介して、transformationMatrixResourceSpriteアドレスを渡す

		// 描画！(DrawCall/ドローコール)

		//commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);                   // DrawInstanced → DrawIndexedInstanced

#pragma region  MyRegion
		/*
		＜引数＞	                  ＜値＞	    ＜意味＞
		VertexCountPerInstance	    3	    1回の描画で使う頂点の数
		InstanceCount	            1	    同じものを何個描くか
		StartVertexLocation	        0	    頂点バッファの何番目から使うか
		StartInstanceLocation	    0	    インスタンス番号をいくつから数えるか
		*/
#pragma endregion
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

	// ─── COMオブジェクトの解放 ───
	// すべてComPtrで管理しているので、WinMainを抜けるときにデストラクタが
	// 「構築したのと逆の順番」で自動的にReleaseを呼んでくれる。
	// ここに Release() を書くと二重解放になるので絶対に書かないこと。


	// ─── ComPtrで扱えないものだけ、ここで後始末する ───
	// ════════════════════════════════════════════════════════════════
	// windows後片付け
	// ════════════════════════════════════════════════════════════════
	dxCommon.Finalize();
	winApp.Finalize();


	// ※ リソースリークチェックは D3DResourceLeakChecker のデストラクタで行う。
	//    ここで行うと、まだComPtrが解放されていないので「漏れている」と誤検出される。

	CoUninitialize();
	return 0;
}
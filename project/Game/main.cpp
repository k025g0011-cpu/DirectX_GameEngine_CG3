#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <assert.h>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "Logger.h"
#include "CrashHandler.h"
#include "GameTime.h"
#include "Input.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "AssetDatabase.h" 
#include "Object3dPipeline.h"
#include "ParticlePipeline.h"          
#include "PrimitiveDrawer.h"
#include "EngineContext.h"
#include "GameScene.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

// ════════════════════════════════════════════════════════════════════
// Windows アプリのエントリーポイント
//   エンジンの道具を用意し、シーンを作り、ループを回すだけ
//   ゲームの内容は GameScene にある
// ════════════════════════════════════════════════════════════════════
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// ════════════════════════════════════════
	// リークチェッカー
	//   一番最初に作る＝デストラクタが一番最後に呼ばれるので、
	//   すべての ComPtr が解放された後にチェックが走る
	// ════════════════════════════════════════
	D3DResourceLeakChecker leakCheck;

	// クラッシュ時にダンプを出力するよう登録する（できるだけ早い段階で）
	RegisterCrashHandler();

	// ════════════════════════════════════════
	// ログファイルの用意（logs/年月日_時分秒.log）
	// ════════════════════════════════════════
	std::filesystem::create_directory("logs");

	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	// ファイル名にコンマ何秒はいらないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	// 日本時間（PCの設定時間）に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	std::ofstream logStream(std::string("logs/") + dateString + ".log");

	//assert(false && "assertテストだよ");
	// ════════════════════════════════════════════════════════════════
	// エンジンの道具を用意する
	//   順序が重要。上から順に依存している
	// ════════════════════════════════════════════════════════════════
	WinApp winApp;
	winApp.Initialize(false);     // false → 1600×900 のウィンドウ | true → ボーダレス全画面

	Log(logStream, "hello DirectX");

	DirectXCommon dxCommon;
	dxCommon.Initialize(&winApp, logStream);

	TextureManager textureManager;
	textureManager.Initialize(&dxCommon);

	// ────────────────────────────────────────────────────────────────
	// アセットの一覧
	//   GPU を使わないので、どこで作っても構わない
	// ────────────────────────────────────────────────────────────────
	AssetDatabase assetDatabase;                          
	assetDatabase.Initialize("resources", logStream);     

	ModelManager modelManager;
	modelManager.Initialize(&dxCommon, &textureManager);

	Object3dPipeline object3dPipeline;
	object3dPipeline.Initialize(&dxCommon);

	ParticlePipeline particlePipeline;
	particlePipeline.Initialize(&dxCommon);

	PrimitiveDrawer primitiveDrawer;
	primitiveDrawer.Initialize(&dxCommon);

	Input input;
	input.Initialize(winApp.GetHInstance(), winApp.GetHwnd());

	GameTime gameTime;
	gameTime.Initialize();

	// ════════════════════════════════════════
	// ImGui の初期化
	// ════════════════════════════════════════
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigWindowsMoveFromTitleBarOnly = true;   // ウィンドウはタイトルバーでのみ移動

	// ────────────────────────────────────────────────────────────────
	// パネルのドッキング（結合・分割・サイズ変更）を許可する
	//   このフラグだけでは見た目は変わらない。
	//   いまのパネルは SetNextWindowPos / NoMove / NoResize で固定しているので、
	//   ImGui 側が「動かせないウィンドウ」と判断してドッキング対象にならない
	//     → 固定をやめるのは #16.8-2
	//
	//   ※ ViewportsEnable（パネルをウィンドウの外へ切り離す機能）は入れない
	//     DX12 ではパネルごとのスワップチェーン管理と
	//     UpdatePlatformWindows / RenderPlatformWindowsDefault の呼び出しが必要になる
	// ────────────────────────────────────────────────────────────────
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui::StyleColorsDark();

	// ────────────────────────────────────────────────────────────────
	// 日本語フォントを読み込む
	//   既定のフォントは ASCII のみ。日本語を出すには別途読む必要がある
	// ────────────────────────────────────────────────────────────────
	ImFont* japaneseFont = io.Fonts->AddFontFromFileTTF(
		"C:/Windows/Fonts/meiryo.ttc",          // メイリオ（Windows 標準）
		18.0f,                                   // ピクセルサイズ
		nullptr,                                 // フォント設定（既定でよい）
		io.Fonts->GetGlyphRangesJapanese());     // 用意する文字の範囲
	assert(japaneseFont != nullptr);

	ImGui_ImplWin32_Init(winApp.GetHwnd());

	// ────────────────────────────────────────────────────────────────
	// DX12 バックエンドの初期化
	//   新しい ImGui は実行中にテクスチャを増減するため、
	//   SRV を起動時に1枠渡すのではなく、必要なときにアプリへ要求してくる
	//     → 確保／解放の関数をこちらから渡す
	// ────────────────────────────────────────────────────────────────
	ImGui_ImplDX12_InitInfo initInfo{};
	initInfo.Device = dxCommon.GetDevice().Get();
	//   テクスチャの転送に使う。新しいバックエンドは自分で GPU へ送るので必須
	initInfo.CommandQueue = dxCommon.GetCommandQueue().Get();
	initInfo.NumFramesInFlight = DirectXCommon::kBackBufferCount;
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // RTV のフォーマットと一致させる
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;               // ImGui は深度を使わない
	initInfo.SrvDescriptorHeap = dxCommon.GetSrvDescriptorHeap().Get();

	//   コールバックに「誰に頼むか」を伝えるための置き場
	//     コールバックは生の関数ポインタなので、this のような文脈を持てない
	initInfo.UserData = &dxCommon;

	//   SRV を1枠ください、と言われたときの処理
	initInfo.SrvDescriptorAllocFn =
		[](ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle) {

				DirectXCommon* dx = static_cast<DirectXCommon*>(info->UserData);

				const uint32_t index = dx->AllocateSrv();
				*outCpuHandle = dx->GetSrvCpuDescriptorHandle(index);
				*outGpuHandle = dx->GetSrvGpuDescriptorHandle(index);
		};

	//   SRV を返します、と言われたときの処理
	initInfo.SrvDescriptorFreeFn =
		[](ImGui_ImplDX12_InitInfo*,
			D3D12_CPU_DESCRIPTOR_HANDLE,
			D3D12_GPU_DESCRIPTOR_HANDLE) {

				// いまの DirectXCommon は SRV を前から順に配るだけで、返却の仕組みがない
				//   何もしなくても破綻はしない（使い終わった枠が再利用されないだけ）
				//   ※ 実行中にテクスチャを大量に作り直すようになったら、空き管理が必要
		};

	ImGui_ImplDX12_Init(&initInfo);
#endif

	// ════════════════════════════════════════════════════════════════
	// 道具を束ねてシーンに渡す
	// ════════════════════════════════════════════════════════════════
	EngineContext context{};
	context.winApp = &winApp;
	context.dxCommon = &dxCommon;
	context.textureManager = &textureManager;
	context.modelManager = &modelManager;
	context.assetDatabase = &assetDatabase;
	context.object3dPipeline = &object3dPipeline;
	context.particlePipeline = &particlePipeline;
	context.primitiveDrawer = &primitiveDrawer;
	context.input = &input;
	context.gameTime = &gameTime;

	GameScene gameScene;
	gameScene.Initialize(context);

	// ════════════════════════════════════════════════════════════════
	// メインループ
	// ════════════════════════════════════════════════════════════════
	while (true) {

		// ウィンドウの×ボタンが押されたら終了
		if (winApp.ProcessMessage()) {
			break;
		}

		// ───── 更新 ─────
		gameTime.Update();
		input.Update();

		// ───── ESC で終了 ─────
		//   全画面には×ボタンが無いので、これが唯一の終了手段になる
		bool requestQuit = input.TriggerKey(DIK_ESCAPE);

#ifdef USE_IMGUI
		// ImGui の入力欄に文字を打っている最中は無視する
		//   オブジェクト名を打っている途中で落ちると困る
		if (ImGui::GetIO().WantTextInput) {
			requestQuit = false;
		}
#endif

		if (requestQuit) {
			break;
		}

#ifdef USE_IMGUI
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
#endif
		gameScene.Update();

#ifdef USE_IMGUI
		// ImGui の内部コマンドを生成する（描画より前）
		ImGui::Render();
#endif

		// ────────────────────────────────────────────────────
		// 描画
		// ────────────────────────────────────────────────────
		dxCommon.PreDraw();

		// ① シーンをオフスクリーンに描く（描画先の選択も GameScene が行う）
		gameScene.Draw();

		// ② 描画先をスワップチェーンに戻して ImGui を描く
		dxCommon.SetSwapChainRenderTarget();

#ifdef USE_IMGUI
		// ImGui もバックバッファに書き込むので、PostDraw のバリアより前
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon.GetCommandList().Get());
#endif

		dxCommon.PostDraw();
	}

	// ════════════════════════════════════════════════════════════════
	// 終了処理
	//   COM オブジェクトは ComPtr が「構築の逆順」で自動解放する。
	//   ここに Release() を書くと二重解放になるので絶対に書かないこと
	// ════════════════════════════════════════════════════════════════
#ifdef USE_IMGUI
	// ImGui は内部で device と SRV ヒープを掴んでいるので、必ず先に終了させる
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	// ComPtr で扱えないもの（イベントハンドル・ウィンドウ）だけ後始末する
	dxCommon.Finalize();
	winApp.Finalize();

	// ※ リソースリークチェックは D3DResourceLeakChecker のデストラクタが行う。
	//    ここで行うと、まだ ComPtr が解放されていないので誤検出になる
	return 0;
}
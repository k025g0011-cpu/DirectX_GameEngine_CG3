#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "RenderTexture.h"
#include "Logger.h"
#include "CrashHandler.h"
#include "GameTime.h"
#include "Input.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "Object3dPipeline.h"
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

	// ════════════════════════════════════════════════════════════════
	// エンジンの道具を用意する
	//   順序が重要。上から順に依存している
	// ════════════════════════════════════════════════════════════════
	WinApp winApp;
	winApp.Initialize();

	Log(logStream, "hello DirectX");

	DirectXCommon dxCommon;
	dxCommon.Initialize(&winApp, logStream);

	TextureManager textureManager;
	textureManager.Initialize(&dxCommon);

	// ────────────────────────────────────────────────────────────────
	// オフスクリーン描画先
	//   3D はここに描き、ImGui::Image で画面に表示する
	// ────────────────────────────────────────────────────────────────
	RenderTexture renderTexture;
	{
		RenderTexture::Desc desc{};
		desc.width = WinApp::kClientWidth;
		desc.height = WinApp::kClientHeight;
		desc.clearColor = { 0.1f, 0.25f, 0.5f, 1.0f };   // 青。スワップチェーンの黒と区別するため
		desc.useDepth = true;
		renderTexture.Initialize(&dxCommon, desc);
	}

	ModelManager modelManager;
	modelManager.Initialize(&dxCommon, &textureManager);

	Object3dPipeline object3dPipeline;
	object3dPipeline.Initialize(&dxCommon);

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
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(winApp.GetHwnd());

	// ImGui のフォントテクスチャ用に SRV を1つ確保する
	uint32_t imguiSrvIndex = dxCommon.AllocateSrv();

	ImGui_ImplDX12_Init(
		dxCommon.GetDevice().Get(),
		DirectXCommon::kBackBufferCount,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,          // RTV のフォーマットと一致させる
		dxCommon.GetSrvDescriptorHeap().Get(),
		dxCommon.GetSrvCpuDescriptorHandle(imguiSrvIndex),
		dxCommon.GetSrvGpuDescriptorHandle(imguiSrvIndex));

	ImGui::GetIO().Fonts->Build();
#endif

	// ════════════════════════════════════════════════════════════════
	// 道具を束ねてシーンに渡す
	// ════════════════════════════════════════════════════════════════
	EngineContext context{};
	context.dxCommon = &dxCommon;
	context.textureManager = &textureManager;
	context.modelManager = &modelManager;
	context.object3dPipeline = &object3dPipeline;
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

#ifdef USE_IMGUI
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
#endif

		// ────────────────────────────────────────────────────
		// ゲーム画面を ImGui ウィンドウに表示する
		//   ※ gameScene.Update() より前に置くこと。
		//     ここで求めたホバー状態を同じフレームの更新で使うため
		//   ※ STEP 5 で Engine/editor/ のクラスに移す
		// ────────────────────────────────────────────────────
		bool isGameViewActive = false;
#ifdef USE_IMGUI
		{
			ImGui::Begin("Game");

			// 画像を置く位置を覚えておく（あとで透明ボタンを重ねるため）
			ImVec2 cursorPos = ImGui::GetCursorPos();

			ImTextureID gameTextureId = (ImTextureID)renderTexture.GetSrvGpuHandle().ptr;
			ImVec2 imageSize = ImVec2(
				static_cast<float>(renderTexture.GetWidth()) * 0.5f,
				static_cast<float>(renderTexture.GetHeight()) * 0.5f);

			// ① 絵を描く
			ImGui::Image(gameTextureId, imageSize);

			// ② 描画位置を絵の左上に戻して、③ 同じ大きさの透明ボタンを重ねる
			ImGui::SetCursorPos(cursorPos);
			ImGui::InvisibleButton("##gameView", imageSize,
				ImGuiButtonFlags_MouseButtonLeft |
				ImGuiButtonFlags_MouseButtonRight |
				ImGuiButtonFlags_MouseButtonMiddle);

			// 画像の上にいる or 画像の上で押し始めてまだ離していない
			isGameViewActive = ImGui::IsItemHovered() || ImGui::IsItemActive();

			ImGui::End();

			// ImGui の入力欄に文字を打っている最中は WASD を奪わない
			isGameViewActive = isGameViewActive && !ImGui::GetIO().WantTextInput;
		}
#endif

		// カメラ操作を許可するかをシーンに伝える
		gameScene.SetGameViewActive(isGameViewActive);

		gameScene.Update();

#ifdef USE_IMGUI
		// ImGui の内部コマンドを生成する（描画より前）
		ImGui::Render();
#endif

		// ────────────────────────────────────────────────────
		// 描画
		// ────────────────────────────────────────────────────
		dxCommon.PreDraw();

		// ① 3D シーンをオフスクリーンテクスチャに描く
		renderTexture.PreDraw();
		gameScene.Draw();
		renderTexture.PostDraw();

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
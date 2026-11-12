#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <cassert>

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
#include "ImGuiManager.h"


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
	// ImGui（初期化の中身は ImGuiManager が持つ）
	// ════════════════════════════════════════
	ImGuiManager imGuiManager;
	imGuiManager.Initialize(&winApp, &dxCommon);

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

		// ImGui の入力欄に文字を打っている最中は無視する
		//   オブジェクト名を打っている途中で落ちると困る
		if (imGuiManager.IsTextInputActive()) {
			requestQuit = false;
		}

		if (requestQuit) {
			break;
		}

		imGuiManager.Begin();
		gameScene.Update();
		imGuiManager.End();

		// ────────────────────────────────────────────────────
		// 描画
		// ────────────────────────────────────────────────────
		dxCommon.PreDraw();

		// ① シーンをオフスクリーンに描く（描画先の選択も GameScene が行う）
		gameScene.Draw();

		// ② 描画先をスワップチェーンに戻して ImGui を描く
		dxCommon.SetSwapChainRenderTarget();

		// ImGui の描画コマンドを積む
		imGuiManager.Draw();

		dxCommon.PostDraw();
	}

	// ════════════════════════════════════════════════════════════════
	// 終了処理
	//   COM オブジェクトは ComPtr が「構築の逆順」で自動解放する。
	//   ここに Release() を書くと二重解放になるので絶対に書かないこと
	// ════════════════════════════════════════════════════════════════
	// ImGui は内部で device と SRV ヒープを掴んでいるので、必ず先に終了させる
	imGuiManager.Finalize();

	// ComPtr で扱えないもの（イベントハンドル・ウィンドウ）だけ後始末する
	dxCommon.Finalize();
	winApp.Finalize();

	// ※ リソースリークチェックは D3DResourceLeakChecker のデストラクタが行う。
	//    ここで行うと、まだ ComPtr が解放されていないので誤検出になる
	return 0;
}
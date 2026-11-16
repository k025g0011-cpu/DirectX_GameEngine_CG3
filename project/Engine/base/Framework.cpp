#include "Framework.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <string>

#include "CrashHandler.h"
#include "Logger.h"

namespace {

	// ────────────────────────────────────────────────────────────────
	// logs/年月日_時分秒.log を開く
	//   ファイル名を決めるだけの処理なので、関数に出して Initialize を短くする
	// ────────────────────────────────────────────────────────────────
	std::ofstream OpenLogFile() {

		std::filesystem::create_directory("logs");

		const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

		//   ファイル名にコンマ何秒はいらないので、削って秒にする
		const std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
			nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

		//   日本時間（PC の設定時間）に変換
		const std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

		return std::ofstream(
			"logs/" + std::format("{:%Y%m%d_%H%M%S}", localTime) + ".log");
	}

}   // namespace

// ════════════════════════════════════════════════════════════════════
// 起動から終了まで
//   この関数だけが「順番」を知っている
// ════════════════════════════════════════════════════════════════════
void Framework::Run() {

	Initialize();

	while (true) {

		// ① ウィンドウのメッセージ・時間・入力。ここで終了が決まる
		if (ProcessFrameStart()) {
			break;
		}

		// ② ImGui のフレーム開始 → ゲームの更新 → 内部コマンドの生成
		//    Begin と End は必ず対で呼ぶ。間で抜けてはいけない
		imGuiManager_.Begin();
		Update();
		imGuiManager_.End();

		// ③ 描画
		dxCommon_.PreDraw();

		//    シーンをオフスクリーンに描く（描画先の選択はゲーム側）
		Draw();

		//    描画先をスワップチェーンに戻して ImGui を描く
		dxCommon_.SetSwapChainRenderTarget();
		imGuiManager_.Draw();

		dxCommon_.PostDraw();
	}

	Finalize();
}

// ════════════════════════════════════════════════════════════════════
// フレームの頭の処理
//   終了判定をここに集めるのは、ImGui の Begin / End が
//   必ず対になるようにするため
//     Begin のあとで抜けると、NewFrame したまま終わって壊れる
// ════════════════════════════════════════════════════════════════════
bool Framework::ProcessFrameStart() {

	//   ウィンドウの×ボタン
	if (winApp_.ProcessMessage()) {
		return true;
	}

	gameTime_.Update();
	input_.Update();

	//   ESC で終了
	//     全画面には×ボタンが無いので、これが唯一の終了手段になる
	//     ImGui の入力欄に文字を打っている最中は無視する
	//     （オブジェクト名を打っている途中で落ちると困る）
	if (input_.TriggerKey(DIK_ESCAPE) && !imGuiManager_.IsTextInputActive()) {
		return true;
	}

	//   ゲーム側からの終了要求
	return isEndRequest_;
}

// ════════════════════════════════════════════════════════════════════
// 初期化
//   順序が重要。上から順に依存している
// ════════════════════════════════════════════════════════════════════
void Framework::Initialize() {

	//   クラッシュ時にダンプを出力するよう登録する（できるだけ早い段階で）
	RegisterCrashHandler();

	logStream_ = OpenLogFile();

	winApp_.Initialize(false);   // false → 1600×900 のウィンドウ | true → ボーダレス全画面

	Log(logStream_, "hello DirectX");

	dxCommon_.Initialize(&winApp_, logStream_);
	textureManager_.Initialize(&dxCommon_);

	//   アセットの一覧。GPU を使わないので、どこで作っても構わない
	assetDatabase_.Initialize("resources", logStream_);

	modelManager_.Initialize(&dxCommon_, &textureManager_);

	object3dPipeline_.Initialize(&dxCommon_);
	particlePipeline_.Initialize(&dxCommon_);
	primitiveDrawer_.Initialize(&dxCommon_);

	input_.Initialize(winApp_.GetHInstance(), winApp_.GetHwnd());
	gameTime_.Initialize();

	imGuiManager_.Initialize(&winApp_, &dxCommon_);

	// ════════════════════════════════════════
	// 道具を束ねる。これを派生がシーンに渡す
	// ════════════════════════════════════════
	context_.winApp = &winApp_;
	context_.dxCommon = &dxCommon_;
	context_.textureManager = &textureManager_;
	context_.modelManager = &modelManager_;
	context_.assetDatabase = &assetDatabase_;
	context_.object3dPipeline = &object3dPipeline_;
	context_.particlePipeline = &particlePipeline_;
	context_.primitiveDrawer = &primitiveDrawer_;
	context_.input = &input_;
	context_.gameTime = &gameTime_;
}

// ════════════════════════════════════════════════════════════════════
// 終了処理
//   COM オブジェクトは ComPtr が「構築の逆順」で自動解放する
//   ここに Release() を書くと二重解放になるので絶対に書かないこと
//
//   ここで畳むのは、ComPtr で扱えないものだけ
//     ImGui のコンテキスト / フェンスのイベントハンドル / ウィンドウ
// ════════════════════════════════════════════════════════════════════
void Framework::Finalize() {

	//   ImGui は内部で device と SRV ヒープを掴んでいるので、必ず先に終了させる
	imGuiManager_.Finalize();

	dxCommon_.Finalize();
	winApp_.Finalize();

	// ※ リソースリークチェックは leakChecker_ のデストラクタが行う
	//    ここで行うと、まだ ComPtr が解放されていないので誤検出になる
}
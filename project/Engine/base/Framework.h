#pragma once

#include <fstream>

#include "AssetDatabase.h"
#include "DirectXCommon.h"
#include "EngineContext.h"
#include "GameTime.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "ModelManager.h"
#include "Object3dPipeline.h"
#include "ParticlePipeline.h"
#include "PrimitiveDrawer.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "CrashHandler.h"

// 前方宣言（ポインタとしてしか使わないので #include 不要）
class RenderTexture;

// ════════════════════════════════════════════════════════════════════
// ゲームの骨格
//   エンジンの道具を全部持ち、起動 → ループ → 終了の順番を握る
//   ゲームの中身は派生クラスが書く
//
//   ── 呼ばれる順番 ──
//     Run()
//       Initialize()                        1回
//       ┌ ウィンドウのメッセージ・入力・終了判定
//       │ ImGui のフレーム開始
//       │ Update()                          毎フレーム
//       │ ImGui の内部コマンド生成
//       │ PreDraw → Draw() → ImGui → PostDraw
//       └ （終了要求が来るまで繰り返す）
//       Finalize()                          1回
//
//   ── 派生の作法 ──
//     Initialize() は Framework::Initialize() を「先に」呼ぶ
//       道具が無い状態でゲームを組み立てられないため
//     Finalize()   は Framework::Finalize()   を「最後に」呼ぶ
//       ゲームが道具を使い終わってから、道具を畳むため
//     建てるのは下から、壊すのは上から
// ════════════════════════════════════════════════════════════════════
class Framework {
public:
	// unique_ptr<Framework> で持つので仮想デストラクタは必須
	virtual ~Framework() = default;

	// コピー禁止（エンジンの道具一式を複製する意味が無い）
	Framework(const Framework&) = delete;
	Framework& operator=(const Framework&) = delete;

	/// <summary>起動から終了まで。これ1つ呼べばゲームが動く</summary>
	void Run();

protected:
	// 直接 new させない。派生クラスからだけ構築できる
	Framework() = default;

	// ────────────────────────────────────────────────────────────────
	// 派生が埋めるもの
	// ────────────────────────────────────────────────────────────────

	/// <summary>エンジンの道具を作る。派生は必ず Framework::Initialize() を先に呼ぶ</summary>
	virtual void Initialize();

	/// <summary>毎フレームの更新。値を変えるだけで、描画コマンドは積まない</summary>
	virtual void Update() = 0;

	/// <summary>描画。PreDraw / PostDraw と ImGui は Run が面倒を見る</summary>
	virtual void Draw() = 0;

	/// <summary>終了処理。派生は自分の後始末をしてから Framework::Finalize() を呼ぶ</summary>
	virtual void Finalize();

	/// <summary>画面に出すオフスクリーン。nullptr ならコピーしない</summary>
	///   ImGui があるビルドでは、ゲーム画面は ImGui のパネルの中に出るので使われない
	///   どのテクスチャに描いているかはゲームしか知らないので、ここで訊く
	virtual RenderTexture* GetPresentTexture() { return nullptr; }
	// ────────────────────────────────────────────────────────────────
	// 派生が使える機能
	// ────────────────────────────────────────────────────────────────

	/// <summary>エンジンの道具一式。シーンに渡す</summary>
	const EngineContext& GetContext() const { return context_; }

	/// <summary>終了を要求する。次のフレームの頭でループを抜ける</summary>
	///   ゲーム内のメニューから「終了」を押したときなどに使う
	void RequestEnd() { isEndRequest_ = true; }

private:
	/// <summary>フレームの頭の処理。true なら終了</summary>
	///   ImGui の Begin / End が必ず対になるよう、終了判定はこの中で済ませる
	bool ProcessFrameStart();

	// ────────────────────────────────────────────────────────────────
	// エンジンの道具
	//
	//   ここに書いた順に作られ、「逆順」に壊れる
	//   いま main.cpp が「上から順に書く」ことで守っている依存関係を、
	//   宣言の順番で表現し直している
	// ────────────────────────────────────────────────────────────────

	//   いちばん最初に作る ＝ いちばん最後に壊れる
	//     すべての ComPtr が解放されたあとに検査が走る
	D3DResourceLeakChecker leakChecker_;

	//   dxCommon_ と assetDatabase_ が参照を握るので、それらより前
	std::ofstream logStream_;

	WinApp           winApp_;
	DirectXCommon    dxCommon_;
	TextureManager   textureManager_;
	AssetDatabase    assetDatabase_;
	ModelManager     modelManager_;
	Object3dPipeline object3dPipeline_;
	ParticlePipeline particlePipeline_;
	PrimitiveDrawer  primitiveDrawer_;
	Input            input_;
	GameTime         gameTime_;

	//   dxCommon_ より後に宣言する ＝ dxCommon_ より先に壊れる
	//     ImGui は内部で device と SRV ヒープを掴んでいる
	ImGuiManager imGuiManager_;

	//   上の道具のポインタを束ねたもの
	EngineContext context_{};

	bool isEndRequest_ = false;
};
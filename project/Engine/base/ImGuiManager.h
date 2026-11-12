#pragma once

// 前方宣言（ポインタとしてしか使わないので #include 不要）
class DirectXCommon;
class WinApp;

// ════════════════════════════════════════════════════════════════════
// ImGui の初期化・フレーム制御・終了をまとめる
//
//   ── このクラスの役目 ──
//   USE_IMGUI の #ifdef を全部この中に閉じ込めること
//     呼ぶ側は分岐を書かなくてよくなる
//     USE_IMGUI が無いビルドでは、全部の関数が空になるだけ
//
//   ── 呼ぶ順番 ──
//     Initialize()        起動時に1回
//     ┌ Begin()           毎フレーム。ゲームの更新より前
//     │   （ここで ImGui::Begin などを呼ぶ）
//     ├ End()             描画より前。ImGui の内部コマンドを作る
//     └ Draw()            バックバッファに切り替えたあと
//     Finalize()          終了時に1回。dxCommon より先に呼ぶこと
// ════════════════════════════════════════════════════════════════════
class ImGuiManager {
public:
	ImGuiManager() = default;
	~ImGuiManager() = default;

	// ImGui はグローバルな状態を持つので、複製させない
	ImGuiManager(const ImGuiManager&) = delete;
	ImGuiManager& operator=(const ImGuiManager&) = delete;

	/// <summary>初期化</summary>
	/// <param name="winApp">ウィンドウ。メッセージの横取りに使う</param>
	/// <param name="dxCommon">DirectX の基盤。デバイスと SRV ヒープを借りる</param>
	void Initialize(WinApp* winApp, DirectXCommon* dxCommon);

	/// <summary>終了処理。dxCommon.Finalize() より先に呼ぶこと</summary>
	///   ImGui は内部で device と SRV ヒープを掴んでいる
	void Finalize();

	/// <summary>このフレームの ImGui を始める</summary>
	void Begin();

	/// <summary>ImGui の内部コマンドを生成する。描画より前</summary>
	void End();

	/// <summary>描画コマンドを積む。描画先をバックバッファに戻したあと</summary>
	void Draw();

	/// <summary>ImGui の入力欄に文字を打っている最中か</summary>
	///   キー入力をゲーム側が横取りしないようにするための判定
	bool IsTextInputActive() const;

private:
	// 借りているもの（所有していない）
	DirectXCommon* dxCommon_ = nullptr;
};
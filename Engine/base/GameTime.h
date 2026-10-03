#pragma once

#include <chrono>
#include <cstdint>

// ════════════════════════════════════════════════════════════════════
// 時間の管理
//   毎フレーム Update() を1回だけ呼び、GetDeltaTime() で経過秒を得る
//
//   「1フレームにどれだけ進めるか」ではなく
//   「1秒にどれだけ進めるか × 経過秒」で書くと、PC の性能に依存しなくなる
// ════════════════════════════════════════════════════════════════════
class GameTime {
public:
	/// <summary>1フレームの経過時間の上限（秒）</summary>
	///   ブレークポイントで止めたり、ウィンドウを掴んで動かしたりすると
	///   前フレームから数秒経つことがある。そのまま使うと物がワープするので打ち切る
	static constexpr float kMaxDeltaTime = 0.1f;

	/// <summary>初期化。計測開始</summary>
	void Initialize();

	/// <summary>毎フレームの先頭で1回だけ呼ぶ</summary>
	void Update();

	// ────────────────────────────────────────────────────────────────
	// 経過時間
	// ────────────────────────────────────────────────────────────────
	/// <summary>ゲーム時間の経過秒。一時停止中は 0 を返す</summary>
	float GetDeltaTime() const;

	/// <summary>実時間の経過秒。一時停止中も進む（エディタ用）</summary>
	float GetUnscaledDeltaTime() const { return deltaTime_; }

	/// <summary>開始からの合計秒（timeScale の影響を受ける）</summary>
	float GetTotalTime() const { return totalTime_; }

	/// <summary>起動からのフレーム数</summary>
	uint64_t GetFrameCount() const { return frameCount_; }

	/// <summary>直近0.5秒の平均フレームレート（表示用）</summary>
	float GetFPS() const { return fps_; }

	// ────────────────────────────────────────────────────────────────
	// 再生制御
	// ────────────────────────────────────────────────────────────────

	/// <summary>一時停止しているか</summary>
	bool IsPaused() const { return isPaused_; }
	void SetPaused(bool paused) { isPaused_ = paused; }

	/// <summary>一時停止中に1フレームだけ進める（コマ送り）</summary>
	void RequestStep() { stepRequested_ = true; }

	/// <summary>このフレームがコマ送りで動いているか（ImGui 表示用）</summary>
	bool IsStepping() const { return isStepping_; }

	// ────────────────────────────────────────────────────────────────
	// 時間の倍率
	//   1.0 = 等速 / 0.0 = 停止（ポーズ）/ 0.5 = スロー / 2.0 = 倍速
	// ────────────────────────────────────────────────────────────────
	float GetTimeScale() const { return timeScale_; }
	void SetTimeScale(float timeScale) { timeScale_ = timeScale; }


private:
	// 型が長いので別名を付ける
	using Clock = std::chrono::steady_clock;

	Clock::time_point lastTime_{};

	float    deltaTime_ = 0.0f;   // スケールを掛ける前の経過秒
	float    totalTime_ = 0.0f;
	uint64_t frameCount_ = 0;
	float    timeScale_ = 1.0f;

	// FPS 計測用（毎フレーム更新すると数字が暴れて読めないので平均をとる）
	float    fpsAccumulatedTime_ = 0.0f;
	uint32_t fpsFrameCount_ = 0;
	float    fps_ = 0.0f;

	bool isPaused_ = false;   // 止まっているか
	bool stepRequested_ = false;   // 次のフレームで1回だけ進めたい
	bool isStepping_ = false;   // このフレームがコマ送りか
};
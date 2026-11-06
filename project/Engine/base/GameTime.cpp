#include "GameTime.h"

// ════════════════════════════════════════════════════════════════════
// ゲーム時間の経過秒
//   一時停止中は 0。ただしコマ送りのフレームだけは通常どおり進む
// ════════════════════════════════════════════════════════════════════
float GameTime::GetDeltaTime() const {

	if (isPaused_ && !isStepping_) {
		return 0.0f;
	}

	return deltaTime_ * timeScale_;
}

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void GameTime::Initialize() {
	lastTime_ = Clock::now();

	deltaTime_ = 0.0f;
	totalTime_ = 0.0f;
	frameCount_ = 0;
	timeScale_ = 1.0f;

	fpsAccumulatedTime_ = 0.0f;
	fpsFrameCount_ = 0;
	fps_ = 0.0f;
}

// ════════════════════════════════════════════════════════════════════
// 更新（毎フレームの先頭で1回）
// ════════════════════════════════════════════════════════════════════
void GameTime::Update() {

	// ────────────────────────────────────────
	// ① 前回からの経過時間を測る
	// ────────────────────────────────────────
	Clock::time_point now = Clock::now();

	// duration<float> にすると、秒を float で取り出せる
	std::chrono::duration<float> elapsed = now - lastTime_;
	deltaTime_ = elapsed.count();

	lastTime_ = now;

	// ────────────────────────────────────────
	// ② 上限で打ち切る
	//     デバッグ中に止めた後、1フレームで何秒も進むのを防ぐ
	// ────────────────────────────────────────
	if (deltaTime_ > kMaxDeltaTime) {
		deltaTime_ = kMaxDeltaTime;
	}

	// ────────────────────────────────────────────────────────────────
	// コマ送り
	//   前フレームにボタンが押されていたら、このフレームだけ時間を進める
	//   ※ GetDeltaTime より前に決める必要がある
	// ────────────────────────────────────────────────────────────────
	isStepping_ = stepRequested_;
	stepRequested_ = false;

	// ────────────────────────────────────────
	// ③ 積算
	// ────────────────────────────────────────
	totalTime_ += GetDeltaTime();   // ポーズ中は進まない
	++frameCount_;

	// ────────────────────────────────────────
	// ④ FPS を0.5秒ごとに更新する
	//     毎フレーム 1/deltaTime を出すと数字が激しく揺れて読めない
	// ────────────────────────────────────────
	fpsAccumulatedTime_ += deltaTime_;
	++fpsFrameCount_;

	if (fpsAccumulatedTime_ >= 0.5f) {
		fps_ = static_cast<float>(fpsFrameCount_) / fpsAccumulatedTime_;
		fpsAccumulatedTime_ = 0.0f;
		fpsFrameCount_ = 0;
	}
}
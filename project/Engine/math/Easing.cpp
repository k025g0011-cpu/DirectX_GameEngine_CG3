#include "Easing.h"

#include <algorithm>   // std::clamp
#include <cmath>

#include "MathUtility.h"   // kPi

namespace {

	// ────────────────────────────────────────────────────────────────
	// Back（行き過ぎてから戻る）のはみ出し量
	//   1.70158 は「約10%はみ出す」値。CSS など各所で使われている定番
	// ────────────────────────────────────────────────────────────────
	constexpr float kBackOvershoot = 1.70158f;

	// ────────────────────────────────────────────────────────────────
	// Bounce（跳ねる）の形を決める値
	//   区間を4つに分け、それぞれに放物線を置いて、だんだん小さく跳ねさせる
	// ────────────────────────────────────────────────────────────────
	constexpr float kBounceBase = 7.5625f;
	constexpr float kBounceSplit = 2.75f;

	// ────────────────────────────────────────────────────────────────
	// Elastic（ばねのように揺れる）の周期
	// ────────────────────────────────────────────────────────────────
	constexpr float kElasticPeriod = (2.0f * kPi) / 3.0f;

	// ────────────────────────────────────────────────────────────────
	// Bounce は分岐が多いので、関数に切り出す
	//   switch の中に4段の if を直接書くと読めなくなるため
	// ────────────────────────────────────────────────────────────────
	float CalcOutBounce(float t) {

		if (t < 1.0f / kBounceSplit) {
			return kBounceBase * t * t;
		}
		if (t < 2.0f / kBounceSplit) {
			t -= 1.5f / kBounceSplit;
			return kBounceBase * t * t + 0.75f;
		}
		if (t < 2.5f / kBounceSplit) {
			t -= 2.25f / kBounceSplit;
			return kBounceBase * t * t + 0.9375f;
		}
		t -= 2.625f / kBounceSplit;
		return kBounceBase * t * t + 0.984375f;
	}

} // namespace

// ════════════════════════════════════════════════════════════════════
// カーブの適用
// ════════════════════════════════════════════════════════════════════
float ApplyEase(EaseType type, float t) {

	// ────────────────────────────────────────────────────────────────
	// 入口で1回だけ丸める
	//   呼ぶ側の elapsed / duration は、1フレーム飛んだだけで 1 を超える
	//   0〜1 の外側ではカーブが暴れる（OutElastic(1.5) は大きく跳ね上がる）
	//   ここで丸めておけば、下の式は 0〜1 だけを考えればよくなる
	// ────────────────────────────────────────────────────────────────
	t = std::clamp(t, 0.0f, 1.0f);

	switch (type) {

	case EaseType::Linear:
		return t;

		// ───── Sine ─────
	case EaseType::InSine:
		return 1.0f - std::cos((t * kPi) / 2.0f);

	case EaseType::OutSine:
		return std::sin((t * kPi) / 2.0f);

	case EaseType::InOutSine:
		return -(std::cos(kPi * t) - 1.0f) / 2.0f;

		// ───── Quad（2乗） ─────
	case EaseType::InQuad:
		return t * t;

	case EaseType::OutQuad: {
		const float u = 1.0f - t;
		return 1.0f - u * u;
	}

	case EaseType::InOutQuad: {
		if (t < 0.5f) {
			return 2.0f * t * t;
		}
		const float u = -2.0f * t + 2.0f;
		return 1.0f - (u * u) / 2.0f;
	}

							// ───── Cubic（3乗） ─────
	case EaseType::InCubic:
		return t * t * t;

	case EaseType::OutCubic: {
		const float u = 1.0f - t;
		return 1.0f - u * u * u;
	}

	case EaseType::InOutCubic: {
		if (t < 0.5f) {
			return 4.0f * t * t * t;
		}
		const float u = -2.0f * t + 2.0f;
		return 1.0f - (u * u * u) / 2.0f;
	}

							 // ───── Back（通り越してから戻る） ─────
	case EaseType::InBack:
		return (kBackOvershoot + 1.0f) * t * t * t - kBackOvershoot * t * t;

	case EaseType::OutBack: {
		const float u = t - 1.0f;
		return 1.0f + (kBackOvershoot + 1.0f) * u * u * u + kBackOvershoot * u * u;
	}

						  // ───── 演出向き ─────
	case EaseType::OutBounce:
		return CalcOutBounce(t);

	case EaseType::OutElastic:
		//   両端は式が不安定になるので、先に確定させる
		if (t <= 0.0f) { return 0.0f; }
		if (t >= 1.0f) { return 1.0f; }
		return std::pow(2.0f, -10.0f * t)
			* std::sin((t * 10.0f - 0.75f) * kElasticPeriod) + 1.0f;

	case EaseType::kMaxCount:
		//   個数を表すだけの番兵。カーブではないので、何も返さず下へ抜ける
		break;
	}

	//   enum の範囲外を static_cast で作られたときの保険
	//   等速として扱えば、動かなくなることはない
	return t;
}

// ════════════════════════════════════════════════════════════════════
// 名前
//   ImGui のコンボに並べるために使う
// ════════════════════════════════════════════════════════════════════
const char* GetEaseName(EaseType type) {

	switch (type) {
	case EaseType::Linear:     return "Linear";

	case EaseType::InSine:     return "InSine";
	case EaseType::OutSine:    return "OutSine";
	case EaseType::InOutSine:  return "InOutSine";

	case EaseType::InQuad:     return "InQuad";
	case EaseType::OutQuad:    return "OutQuad";
	case EaseType::InOutQuad:  return "InOutQuad";

	case EaseType::InCubic:    return "InCubic";
	case EaseType::OutCubic:   return "OutCubic";
	case EaseType::InOutCubic: return "InOutCubic";

	case EaseType::InBack:     return "InBack";
	case EaseType::OutBack:    return "OutBack";

	case EaseType::OutBounce:  return "OutBounce";
	case EaseType::OutElastic: return "OutElastic";

	case EaseType::kMaxCount:
		break;
	}

	return "Unknown";
}
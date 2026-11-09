#pragma once

#include <cstdint>

// ════════════════════════════════════════════════════════════════════
// イージング（変化の速さのカーブ）
//   0〜1 の「経過の割合」を、0〜1 の「見かけの進み具合」に変換する
//
//     t = 0.5 のとき
//       Linear   → 0.5    （等速）
//       OutQuad  → 0.75   （最初が速く、終わりが遅い）
//       InQuad   → 0.25   （最初が遅く、終わりが速い）
//
//   使い方
//     const float rate = ApplyEase(EaseType::OutCubic, elapsed / duration);
//     position = Lerp(start, end, rate);
//
//   「値そのもの」ではなく「カーブだけ」を返す理由：
//     補間する型（float / Vector3 / 色）を選ばずに済む
//     混ぜるのは呼ぶ側の仕事にして、ここは形だけを担当する
// ════════════════════════════════════════════════════════════════════
enum class EaseType : uint32_t {
	Linear,

	// Sine：いちばん穏やか。UI の動きなど
	InSine,
	OutSine,
	InOutSine,

	// Quad：2乗。素直な加速・減速
	InQuad,
	OutQuad,
	InOutQuad,

	// Cubic：3乗。Quad より効きが強い
	InCubic,
	OutCubic,
	InOutCubic,

	// Back：目標を一度通り越してから戻る
	InBack,
	OutBack,

	// 演出向き
	OutBounce,
	OutElastic,

	// 個数。ImGui のコンボや範囲チェックに使う。カーブではない
	kMaxCount,
};

/// <summary>カーブを適用する</summary>
/// <param name="type">カーブの種類</param>
/// <param name="t">経過の割合。0〜1 の外は丸める</param>
/// <returns>見かけの進み具合。Back と Elastic は 0〜1 を少しはみ出す</returns>
float ApplyEase(EaseType type, float t);

/// <summary>種類の名前。ImGui の表示用</summary>
const char* GetEaseName(EaseType type);


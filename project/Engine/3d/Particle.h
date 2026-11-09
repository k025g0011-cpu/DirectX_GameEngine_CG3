#pragma once

#include "MathUtility.h" // Transform / Vector3

// ════════════════════════════════════════════════════════════════════
// 粒1個ぶんのデータ
//   ParticleSystem が持ち、将来 Emitter が生成し、Field が書き換える
//   「ただのデータの入れ物」なので class ではなく struct
// ════════════════════════════════════════════════════════════════════
struct Particle {
	Transform transform{
		{ 1.0f, 1.0f, 1.0f },   // scale
		{ 0.0f, 0.0f, 0.0f },   // rotate
		{ 0.0f, 0.0f, 0.0f },   // translate
	};

	// 1秒あたりの移動量（ワールド座標）
	Vector3 velocity{ 0.0f,0.0f,0.0f };

	// 粒そのものの色。寿命によるフェードはこれに「掛け算」される
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

	// 寿命（秒）
	float lifeTime = 1.0f;

	// 生まれてからの経過時間（秒）
	float currentTime = 0.0f;

	/// <summary>まだ生きているか</summary>
	bool IsAlive() const { return currentTime < lifeTime; }
};

// ════════════════════════════════════════════════════════════════════
// GPU の StructuredBuffer に渡す1要素
//   Particle.VS.hlsl の ParticleForGPU と
//   「メンバの順番・型・サイズ」を完全に一致させること
//
//   Object3d の TransformationMatrix を流用しない理由：
//   あれはモデルとスプライトも使っている。色を足すと
//   全オブジェクトの定数バッファが太り、Object3d.VS.hlsl も巻き込まれる
// ════════════════════════════════════════════════════════════════════
struct ParticleForGPU {         // 全体 80 バイト
	Matrix4x4 WVP;              // クリップ空間へ変換する行列     64バイト
	Vector4   color;            // 粒ごとの色（αはフェード込み）   16バイト
};
#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "CameraData.h"
#include "Component.h"
#include "MathUtility.h"
#include "Particle.h"
#include "ParticleSystem.h"

// ════════════════════════════════════════════════════════════════════
// 粒を射出するコンポーネント
//   ・粒の「発生」を担当し、描画は ParticleSystem に任せる
//   ・発生位置は GameObject の Transform から取る（動かせる）
//
//   Draw を override していない理由：
//     パーティクルは専用パイプラインを使うので、他のオブジェクトと
//     同じループで描くとパイプラインが途中で切り替わって壊れる。
//     描画は DrawParticles() として、別パスから呼んでもらう
// ════════════════════════════════════════════════════════════════════
class ParticleEmitter : public Component {
public:
	/// <summary>粒に貼るテクスチャと、同時に描ける最大数を決めて作る</summary>
	explicit ParticleEmitter(
		const std::string& texturePath = "resources/particle.png",
		uint32_t maxParticleCount = 128)
		: texturePath_(texturePath), maxParticleCount_(maxParticleCount) {}

	// ───── Component の実装 ─────
	void Initialize() override;
	void Update() override;
	void DrawInspector() override;
	const char* GetTypeName() const override { return "ParticleEmitter"; }

	// ───── 自分固有の機能 ─────

	/// <summary>粒を描く。パイプラインが切り替わるので専用パスから呼ぶ</summary>
	void DrawParticles(const CameraData& camera);

	/// <summary>いまの設定で count 個まとめて出す</summary>
	void Burst(uint32_t count);

	/// <summary>出ている粒を全部消す</summary>
	void Clear() { particleSystem_.Clear(); }

private:
	/// <summary>設定に従って粒を1個組み立てる</summary>
	Particle MakeParticle();

	// 描画担当。この Emitter 専用の GPU バッファを持つ
	ParticleSystem particleSystem_;

	// 乱数エンジン。Emitter ごとに独立している
	std::mt19937 randomEngine_;

	// ───── コンストラクタで受け取り、Initialize で使う値 ─────
	//   コンストラクタの時点では GetContext() が使えないので、ここに溜めておく
	std::string texturePath_;
	uint32_t maxParticleCount_ = 128;

	// ───── 発生の設定 ─────
	bool    isPlaying_ = true;        // 継続的に出すか
	float   emitRate_ = 20.0f;        // 1秒あたりの発生数
	float   emitAccumulator_ = 0.0f;  // 1個に満たない端数の持ち越し
	int32_t burstCount_ = 16;         // 「まとめて出す」の個数

	// ───── 生まれる粒の初期値の幅 ─────
	Vector3 emitRange_{ 1.0f, 1.0f, 1.0f };      // Emitter を中心とした AABB の半径
	Vector3 velocityRange_{ 1.0f, 1.0f, 1.0f };  // ±この範囲でランダム
	float   lifeTimeMin_ = 1.0f;
	float   lifeTimeMax_ = 3.0f;
	float   particleScale_ = 0.6f;
};


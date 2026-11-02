#pragma once

#include "MathUtility.h"   // Vector3

// 前方宣言（ポインタ・参照でしか使わない）
struct Particle;

// ════════════════════════════════════════════════════════════════════
// 軸平行境界ボックス（Axis-Aligned Bounding Box）
//   傾かない直方体。中に入っているかの判定が3回の比較で済む
// ════════════════════════════════════════════════════════════════════
struct AABB {
	Vector3 min{ -1.0f, -1.0f, -1.0f };
	Vector3 max{ 1.0f, 1.0f, 1.0f };

	/// <summary>点がこの箱の中にあるか</summary>
	bool Contains(const Vector3& point) const;
};

// ════════════════════════════════════════════════════════════════════
// 場（Field）の基底
//   「効果範囲」と「効果」を持ち、範囲内の粒に影響を与える
//
//   種類が複数あると最初から分かっているので、基底から作る
// ════════════════════════════════════════════════════════════════════
class Field {
public:
	// unique_ptr で派生を持つので仮想デストラクタは必須
	virtual ~Field() = default;

	// コピー禁止（unique_ptr で持つので複製禁止）
	Field(const Field&) = delete;
	Field& operator=(const Field&) = delete;

	// ────────────────────────────────────────────────────────────────
	// 派生が実装するもの
	// ────────────────────────────────────────────────────────────────


	/// <summary>粒に効果を与える。範囲内かの判定は呼ぶ側が済ませている</summary>
	/// <param name="particle">書き換える粒</param>
	/// <param name="deltaTime">経過秒数</param>
	virtual void Apply(Particle& particle, float deltaTime) const = 0;

	/// <summary>インスペクタの見出しに出す名前。実装必須</summary>
	virtual const char* GetTypeName() const = 0;

	/// <summary>インスペクタに自分固有の設定を描く</summary>
	virtual void DrawInspector() {}

	/// <summary>この位置が効果範囲内か。形を変えたい派生は上書きする</summary>
	virtual bool IsInside(const Vector3& position) const { return area_.Contains(position); }

	// ────────────────────────────────────────────────────────────────
	// 共通の機能
	// ────────────────────────────────────────────────────────────────
	bool IsEnabled() const { return isEnabled_; }
	void SetEnabled(bool enabled) { isEnabled_ = enabled; }

	const AABB& GetArea() const { return area_; }
	AABB& GetArea() { return area_; }
protected:
	// 直接 new させない。派生クラスからだけ構築できる
	Field() = default;
private:
	AABB area_{};        // 効果範囲
	bool isEnabled_ = true;
};

// ════════════════════════════════════════════════════════════════════
// 加速度を与える場（風・重力など）
//   範囲内の粒の速度を、一定の加速度で変化させ続ける
// ════════════════════════════════════════════════════════════════════
class AccelerationField : public Field {
public:
	explicit AccelerationField(const Vector3& acceleration = { 10.0f, 0.0f, 0.0f })
		: acceleration_(acceleration) {}

	void Apply(Particle& particle, float deltaTime) const override;
	void DrawInspector() override;
	const char* GetTypeName() const override { return "AccelerationField"; }

private:
	// 1秒あたりに速度をどれだけ変化させるか
	Vector3 acceleration_{ 10.0f, 0.0f, 0.0f };
};

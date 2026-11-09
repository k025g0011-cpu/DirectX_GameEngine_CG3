#pragma once

#include "MathUtility.h"   // Vector3

// 前方宣言（ポインタ・参照でしか使わない）
struct Particle;
class PrimitiveDrawer;

// ════════════════════════════════════════════════════════════════════
// 軸平行境界ボックス（Axis-Aligned Bounding Box）
//   傾かない直方体。中に入っているかの判定が3回の比較で済む
// ════════════════════════════════════════════════════════════════════
struct AABB {
	Vector3 min{ -1.0f, -1.0f, -1.0f };
	Vector3 max{ 1.0f, 1.0f, 1.0f };

	/// <summary>点がこの箱の中にあるか</summary>
	bool Contains(const Vector3& point) const;

	/// <summary>min と max が逆転していたら入れ替える</summary>
	void Normalize();
};

/// <summary>AABB を12本の線で描く（ワイヤーフレーム）</summary>
///   AABB と一緒に置いてある。将来 AABB を MathUtility へ移すときは一緒に移す
void DrawAABBWireframe(PrimitiveDrawer& drawer, const AABB& area, const Vector4& color);

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

	// ────────────────────────────────────────────────────────────────
	// 外から呼ぶ入口（あえて virtual にしない）
	//   どの場にも共通のルールをここで済ませ、
	//   形ごとに違う部分だけを protected の virtual に任せる
	//     → 派生が共通ルールを書き忘れる余地がなくなる
	// ────────────────────────────────────────────────────────────────

	/// <summary>この位置に効果が届くか</summary>
	bool IsInside(const Vector3& position) const {
		//   範囲で絞らない設定なら、形に関係なくどこでも効く
		if (!isAreaLimited_) {
			return true;
		}
		return IsInsideArea(position);
	}

	/// <summary>効果範囲を線で描く</summary>
	void DrawGizmos(PrimitiveDrawer& drawer) const {
		if (!isGizmoVisible_) {
			return;
		}
		DrawAreaGizmos(drawer);
	}

	/// <summary>インスペクタを描く（共通の設定 ＋ 派生固有の設定）</summary>
	void DrawInspector();

	// ────────────────────────────────────────────────────────────────
	// 共通の機能
	// ────────────────────────────────────────────────────────────────
	bool IsEnabled() const { return isEnabled_; }
	void SetEnabled(bool enabled) { isEnabled_ = enabled; }

	/// <summary>効果範囲で絞るか。false ならどこにいる粒にも効く</summary>
	bool IsAreaLimited() const { return isAreaLimited_; }
	void SetAreaLimited(bool limited) { isAreaLimited_ = limited; }

	/// <summary>この場のギズモを描くか</summary>
	bool IsGizmoVisible() const { return isGizmoVisible_; }
	void SetGizmoVisible(bool visible) { isGizmoVisible_ = visible; }

	const AABB& GetArea() const { return area_; }
	AABB& GetArea() { return area_; }

protected:
	// 直接 new させない。派生クラスからだけ構築できる
	Field() = default;

	// ────────────────────────────────────────────────────────────────
	// 派生が上書きできるもの（形ごとに違う部分だけ）
	// ────────────────────────────────────────────────────────────────

	/// <summary>形による範囲判定。球などにしたい派生が上書きする</summary>
	virtual bool IsInsideArea(const Vector3& position) const { return area_.Contains(position); }

	/// <summary>範囲の形を線で描く。形を変えた派生が上書きする</summary>
	virtual void DrawAreaGizmos(PrimitiveDrawer& drawer) const;

	/// <summary>派生固有の設定を描く。共通の設定は基底が描く</summary>
	virtual void DrawOwnInspector() {}

private:
	AABB area_{};                  // 効果範囲
	bool isEnabled_ = true;        // 効果そのもののオンオフ
	bool isAreaLimited_ = true;    // false なら範囲を無視して世界全体に効く
	bool isGizmoVisible_ = true;   // この場のギズモを描くか
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
	const char* GetTypeName() const override { return "AccelerationField"; }
protected:
	void DrawOwnInspector() override;

private:
	// 1秒あたりに速度をどれだけ変化させるか
	Vector3 acceleration_{ 10.0f, 0.0f, 0.0f };
};

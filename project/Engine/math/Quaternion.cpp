#include "Quaternion.h"

#include <algorithm>   // std::clamp
#include <cmath>

#include "MathUtility.h"   // Normalize(Vector3) / Cross / Dot(Vector3)

namespace {

	// ────────────────────────────────────────────────────────────────
	// Slerp で線形補間に切り替える閾値
	//   内積がこれより大きい＝2つの向きがほぼ同じ
	//   このとき sin(θ) が 0 に近づき、割り算が不安定になる
	// ────────────────────────────────────────────────────────────────
	constexpr float kSlerpLinearThreshold = 0.9995f;

	// 長さ0とみなす閾値
	constexpr float kEpsilon = 1e-6f;

	// ────────────────────────────────────────────────────────────────
	// 成分ごとの線形結合（補間の中だけで使う）
	//
	//   Quaternion に operator+ や operator* (スカラー) を公開しないのは、
	//   「2つの回転を足す」という操作に意味が無いから
	//   q1 + q2 は「両方の回転」ではない。ただの壊れた値になる
	//   補間の途中計算でだけ必要なので、この .cpp の中に閉じ込める
	// ────────────────────────────────────────────────────────────────
	Quaternion Blend(const Quaternion& a, float scaleA, const Quaternion& b, float scaleB) {
		return {
			a.x * scaleA + b.x * scaleB,
			a.y * scaleA + b.y * scaleB,
			a.z * scaleA + b.z * scaleB,
			a.w * scaleA + b.w * scaleB,
		};
	}

}   // namespace

// ════════════════════════════════════════════════════════════════════
// 生成
// ════════════════════════════════════════════════════════════════════
Quaternion MakeIdentityQuaternion() {
	return Quaternion{ 0.0f, 0.0f, 0.0f, 1.0f };
}

Quaternion MakeRotateAxisAngleQuaternion(const Vector3& axis, float angle) {

	//   軸の長さが 0 だと方向が決まらない。回転なしとして扱う
	if (Length(axis) < kEpsilon) {
		return MakeIdentityQuaternion();
	}

	const Vector3 unitAxis = Normalize(axis);

	//   角度が半分なのは、回転が q * v * q⁻¹ の形で左右から2回効くため
	const float half = angle * 0.5f;
	const float s = std::sin(half);

	return Quaternion{
		unitAxis.x * s,
		unitAxis.y * s,
		unitAxis.z * s,
		std::cos(half),
	};
}

Quaternion MakeRotateXYZQuaternion(const Vector3& rotate) {

	//   MakeRotateXYZMatrix と同じ「X → Y → Z」の順
	//   operator* が「左が先」なので、行列版とまったく同じ並びで書ける
	return MakeRotateAxisAngleQuaternion({ 1.0f, 0.0f, 0.0f }, rotate.x)
		* MakeRotateAxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, rotate.y)
		* MakeRotateAxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, rotate.z);
}

// ════════════════════════════════════════════════════════════════════
// 合成
//   教科書のハミルトン積 H(a, b) は「b を先に適用」する
//   このエンジンは行列と揃えて「左が先」にしたいので、引数を入れ替えて呼ぶ
//     lhs * rhs  ＝  H(rhs, lhs)
// ════════════════════════════════════════════════════════════════════
Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs) {

	const Quaternion& a = rhs;   // 教科書でいう q1
	const Quaternion& b = lhs;   // 教科書でいう q2

	return Quaternion{
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
		a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
	};
}

// ════════════════════════════════════════════════════════════════════
// 共役・長さ・正規化・逆
// ════════════════════════════════════════════════════════════════════
Quaternion Conjugate(const Quaternion& q) {
	return Quaternion{ -q.x, -q.y, -q.z, q.w };
}

float Norm(const Quaternion& q) {
	return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

Quaternion Normalize(const Quaternion& q) {

	const float norm = Norm(q);

	//   長さ0は回転として無効。零を返すと以降すべて壊れるので、回転なしに倒す
	//   Normalize(Vector3) が零ベクトルを返すのと方針が違うのは、
	//   クォータニオンには「無害な零」が存在しないため
	if (norm < kEpsilon) {
		return MakeIdentityQuaternion();
	}

	const float inverseNorm = 1.0f / norm;

	return Quaternion{
		q.x * inverseNorm,
		q.y * inverseNorm,
		q.z * inverseNorm,
		q.w * inverseNorm,
	};
}

Quaternion Inverse(const Quaternion& q) {

	const float normSquared =
		q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;

	if (normSquared < kEpsilon) {
		return MakeIdentityQuaternion();
	}

	//   長さ1なら normSquared が 1 なので、共役がそのまま逆になる
	//   長さが1でない場合も扱えるよう、ここでは割っておく
	const Quaternion conjugate = Conjugate(q);
	const float inverseNormSquared = 1.0f / normSquared;

	return Quaternion{
		conjugate.x * inverseNormSquared,
		conjugate.y * inverseNormSquared,
		conjugate.z * inverseNormSquared,
		conjugate.w * inverseNormSquared,
	};
}

float Dot(const Quaternion& q1, const Quaternion& q2) {
	return q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
}

// ════════════════════════════════════════════════════════════════════
// ベクトルを回す
//   定義どおりなら q * v * q⁻¹ だが、展開した次の形のほうが速い
//   v' = v + 2w(u × v) + 2u × (u × v)       u = (x, y, z)
//   掛け算が約半分で済み、クォータニオンの積を2回組む必要もない
// ════════════════════════════════════════════════════════════════════
Vector3 RotateVector(const Vector3& v, const Quaternion& q) {

	const Vector3 u{ q.x, q.y, q.z };

	const Vector3 uCrossV = Cross(u, v);
	const Vector3 uCrossUCrossV = Cross(u, uCrossV);

	return v + uCrossV * (2.0f * q.w) + uCrossUCrossV * 2.0f;
}

// ════════════════════════════════════════════════════════════════════
// 回転行列に変換する
//   行ベクトル規約（v × M）なので、教科書によくある形の転置になる
//   MakeRotateXMatrix などと同じ並びになることを確認済み
// ════════════════════════════════════════════════════════════════════
Matrix4x4 MakeRotateMatrix(const Quaternion& q) {

	const float xx = q.x * q.x;
	const float yy = q.y * q.y;
	const float zz = q.z * q.z;

	const float xy = q.x * q.y;
	const float xz = q.x * q.z;
	const float yz = q.y * q.z;

	const float wx = q.w * q.x;
	const float wy = q.w * q.y;
	const float wz = q.w * q.z;

	Matrix4x4 result = MakeIdentity4x4();

	result.m[0][0] = 1.0f - 2.0f * (yy + zz);
	result.m[0][1] = 2.0f * (xy + wz);
	result.m[0][2] = 2.0f * (xz - wy);

	result.m[1][0] = 2.0f * (xy - wz);
	result.m[1][1] = 1.0f - 2.0f * (xx + zz);
	result.m[1][2] = 2.0f * (yz + wx);

	result.m[2][0] = 2.0f * (xz + wy);
	result.m[2][1] = 2.0f * (yz - wx);
	result.m[2][2] = 1.0f - 2.0f * (xx + yy);

	return result;
}

// ════════════════════════════════════════════════════════════════════
// 回転行列からクォータニオンを作る（シェパードの方法）
//
//   ── なぜ4つに分岐するのか ──
//   基本の式は「w を先に求めて、4w で割って x, y, z を出す」というもの。
//   ところが 180度回転のとき w = 0 になり、0 で割ることになる
//     例：X軸まわり180度 → q = (1, 0, 0, 0)
//
//   そこで x, y, z, w のうち「いちばん大きいもの」を先に求め、
//   それで割るようにする。いちばん大きいものは 0.5 以上あることが保証される
//     （4つの2乗和が 1 なので、最大のものは必ず 1/4 以上＝絶対値 0.5 以上）
//
//   どれが最大かは対角成分で判定できる
//     trace が大きい        → w が最大
//     m[0][0] が最大        → x が最大
//     m[1][1] が m[2][2] より大 → y が最大
//     それ以外              → z が最大
// ════════════════════════════════════════════════════════════════════
Quaternion MakeQuaternionFromMatrix(const Matrix4x4& rotation) {

	const float m00 = rotation.m[0][0];
	const float m11 = rotation.m[1][1];
	const float m22 = rotation.m[2][2];

	//   対角の和。展開すると 4*w*w - 1 になる
	const float trace = m00 + m11 + m22;

	Quaternion result{};

	if (trace > 0.0f) {
		//   w が最大。s = 4w
		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		result.w = 0.25f * s;
		result.x = (rotation.m[1][2] - rotation.m[2][1]) / s;
		result.y = (rotation.m[2][0] - rotation.m[0][2]) / s;
		result.z = (rotation.m[0][1] - rotation.m[1][0]) / s;

	} else if (m00 > m11 && m00 > m22) {
		//   x が最大。s = 4x
		const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
		result.w = (rotation.m[1][2] - rotation.m[2][1]) / s;
		result.x = 0.25f * s;
		result.y = (rotation.m[0][1] + rotation.m[1][0]) / s;
		result.z = (rotation.m[2][0] + rotation.m[0][2]) / s;

	} else if (m11 > m22) {
		//   y が最大。s = 4y
		const float s = std::sqrt(1.0f - m00 + m11 - m22) * 2.0f;
		result.w = (rotation.m[2][0] - rotation.m[0][2]) / s;
		result.x = (rotation.m[0][1] + rotation.m[1][0]) / s;
		result.y = 0.25f * s;
		result.z = (rotation.m[1][2] + rotation.m[2][1]) / s;

	} else {
		//   z が最大。s = 4z
		const float s = std::sqrt(1.0f - m00 - m11 + m22) * 2.0f;
		result.w = (rotation.m[0][1] - rotation.m[1][0]) / s;
		result.x = (rotation.m[2][0] + rotation.m[0][2]) / s;
		result.y = (rotation.m[1][2] + rotation.m[2][1]) / s;
		result.z = 0.25f * s;
	}

	//   平方根と割り算を通るので、長さが 1 から少しずれる
	//   回転として使う前にここで整えておく
	return Normalize(result);
}

// ════════════════════════════════════════════════════════════════════
// オイラー角（XYZ順）に直す
//   いったん行列を作って、既存の取り出し処理に渡すだけ
//   式を2箇所に書かないので、片方だけ直してずれる事故が起きない
// ════════════════════════════════════════════════════════════════════
Vector3 MakeEulerXYZFromQuaternion(const Quaternion& q) {
	return MakeEulerXYZFromMatrix(MakeRotateMatrix(q));
}

// ════════════════════════════════════════════════════════════════════
// 球面線形補間
//   2つの向きを、最短経路で・一定の角速度でつなぐ
// ════════════════════════════════════════════════════════════════════
Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t) {

	//   ApplyEase と同じ。割合として使う値は入口で縛る
	t = std::clamp(t, 0.0f, 1.0f);

	Quaternion start = q0;
	float dot = Dot(q0, q1);

	// ────────────────────────────────────────────────────────────────
	// q と -q は「まったく同じ回転」を表す（二重被覆）
	//   内積が負ということは、そのまま補間すると遠回りの弧を通るということ
	//   片方の符号を反転すれば、同じ回転のまま近いほうの弧を通れる
	//     これが無いと「359度回る」ような動きになる
	// ────────────────────────────────────────────────────────────────
	if (dot < 0.0f) {
		start = Quaternion{ -q0.x, -q0.y, -q0.z, -q0.w };
		dot = -dot;
	}

	// ────────────────────────────────────────────────────────────────
	// ほぼ同じ向きのときは、sin(θ) が 0 に近づいて割り算が壊れる
	//   角度が小さいので、線形補間しても見た目の差は出ない
	// ────────────────────────────────────────────────────────────────
	if (dot > kSlerpLinearThreshold) {
		return Normalize(Blend(start, 1.0f - t, q1, t));
	}

	//   なす角。dot は上の分岐で 0.9995 以下に収まっているので acos は安全
	const float theta = std::acos(dot);
	const float sinTheta = std::sin(theta);

	//   両端からの「球面上の距離」で重みを決める
	//     t に対して角度が一定の速さで進む
	const float scale0 = std::sin((1.0f - t) * theta) / sinTheta;
	const float scale1 = std::sin(t * theta) / sinTheta;

	return Blend(start, scale0, q1, scale1);
}
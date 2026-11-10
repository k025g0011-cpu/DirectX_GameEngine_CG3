#pragma once

#include "Matrix4x4.h"
#include "Vector3.h"

// ════════════════════════════════════════════════════════════════════
// クォータニオン（回転を4つの float で表す）
// Quaternion は各座標軸の回転情報とともに、自分の回転の向きも持っているので行列よりも扱いやすい場合がある
// ただし、行列のように平行移動や拡縮をまとめて扱うことはできない

/*
>>> 下記のような利点があるので、回転だけを扱う場合は Quaternion を使うと便利

1	ローカル軸まわりに回せる	
2	補間できる				
3	誤差が1行で直る			
4	軽い	64バイト → 16バイト
5	ジンバルロック	を防ぐ
*/
// ════════════════════════════════════════════════════════════════════
struct Quaternion {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;   // 既定は「回転なし」。全部0は回転として無効な値になる
};

// ============================================
// 生成
// ============================================

/// <summary>回転なし（単位クォータニオン）</summary>
Quaternion MakeIdentityQuaternion();

/// <summary>軸と角度から作る</summary>
/// <param name="axis">回転軸。長さ1でなくてよい（中で正規化する）</param>
/// <param name="angle">回転角（ラジアン）。右ねじの向き</param>
Quaternion MakeRotateAxisAngleQuaternion(const Vector3& axis, float angle);

/// <summary>オイラー角（XYZ順）から作る</summary>
///   MakeRotateXYZMatrix と同じ順序・同じ結果になる
Quaternion MakeRotateXYZQuaternion(const Vector3& rotate);

// ============================================
// 基本演算
// ============================================

/// <summary>回転の合成。左を適用してから右（行列の * と同じ順序）</summary>
Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs);

/// <summary>共役。虚部の符号を反転する</summary>
///   長さ1のクォータニオンでは、これが逆回転になる
Quaternion Conjugate(const Quaternion& q);

/// <summary>ノルム（長さ）。回転を表すものは必ず 1 になる</summary>
float Norm(const Quaternion& q);

/// <summary>長さ1にする。合成を繰り返した後に呼んで誤差を落とす</summary>
Quaternion Normalize(const Quaternion& q);

/// <summary>逆回転</summary>
///   長さ1なら Conjugate と同じ。長さが1でない場合にも対応する
Quaternion Inverse(const Quaternion& q);

/// <summary>内積。2つの向きがどれだけ近いかを表す</summary>
float Dot(const Quaternion& q1, const Quaternion& q2);

// ============================================
// 変換
// ============================================

/// <summary>ベクトルを回す</summary>
///   TransformNormal(v, MakeRotateMatrix(q)) と同じ結果になる
///   1本だけ回すならこちらのほうが速い（行列を作らなくて済む）
Vector3 RotateVector(const Vector3& v, const Quaternion& q);

/// <summary>回転行列に変換する</summary>
///   たくさんの頂点を回すときや、アフィン行列に組み込むときに使う
Matrix4x4 MakeRotateMatrix(const Quaternion& q);

/// <summary>回転行列からクォータニオンを作る</summary>
///   渡すのは「回転だけ」の行列。スケールが入っていると正しく取れない
///   DecomposeAffineMatrix のように、各軸をスケールで割ってから渡すこと
Quaternion MakeQuaternionFromMatrix(const Matrix4x4& rotation);

/// <summary>オイラー角（XYZ順）に直す</summary>
///   インスペクタに角度を表示するためのもの。計算にはそのまま使わない
///   同じ向きでも、元に入れた角度と違う組が返ることがある（ジンバルロック付近）
Vector3 MakeEulerXYZFromQuaternion(const Quaternion& q);

// ============================================
// 補間
// ============================================

/// <summary>球面線形補間。2つの向きの間を最短経路で一定速度でつなぐ</summary>
/// <param name="t">0 で q0、1 で q1。範囲外は丸める</param>
Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);
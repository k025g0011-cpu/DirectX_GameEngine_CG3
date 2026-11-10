#pragma once
#include "Matrix4x4.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Quaternion.h"
#include <numbers>


/// <summary>
/// 変換データ
/// </summary>
struct Transform {
    Vector3 scale{ 1.0f, 1.0f, 1.0f };
    Vector3 rotate{ 0.0f, 0.0f, 0.0f };
    Vector3 translate{ 0.0f, 0.0f, 0.0f };
};

/// <summary>
/// 変換データ（UV用）
/// </summary>
/*
3D の Transform を流用すると
 scale.z / rotate.x / rotate.y / translate.z が永久に使われない溜め分ける
*/
struct UVTransform{
	Vector2 scale{ 1.0f, 1.0f };
    float rotate = 0.0f; // Z軸まわり（ラジアン）
	Vector2 translate{ 0.0f, 0.0f };
};


// ════════════════════════════════════════
// 円周率と角度の変換
//   inline constexpr なので、ヘッダに書いても多重定義にならない
// ════════════════════════════════════════
inline constexpr float kPi = std::numbers::pi_v<float>;

inline constexpr float kDegToRad = kPi / 180.0f;   // 度 → ラジアン
inline constexpr float kRadToDeg = 180.0f / kPi;   // ラジアン → 度

// ============================================
// Vector3 演算子
// ============================================

// --- 算術演算子 ---

/// <summary>
/// ベクトルの加算 (v1 + v2)
/// </summary>
const Vector3 operator+(const Vector3& v1, const Vector3& v2);

/// <summary>
/// ベクトルの減算 (v1 - v2)
/// </summary>
const Vector3 operator-(const Vector3& v1, const Vector3& v2);

/// <summary>
/// スカラー倍 (v * s)
/// </summary>
const Vector3 operator*(const Vector3& v, float s);

// --- 代入演算子 ---

/// <summary>
/// 加算代入 (v1 += v2)
/// </summary>
Vector3& operator+=(Vector3& v1, const Vector3& v2);

/// <summary>
/// 減算代入 (v1 -= v2)
/// </summary>
Vector3& operator-=(Vector3& v1, const Vector3& v2);

/// <summary>
/// スカラー倍代入 (v *= s)
/// </summary>
Vector3& operator*=(Vector3& v, float s);

/// <summary>
/// スカラー除算代入 (v /= s)
/// </summary>
Vector3& operator/=(Vector3& v, float s);

// ============================================
// Vector3 関数
// ============================================

/// <summary>
/// 内積を計算
/// </summary>
float Dot(const Vector3& v1, const Vector3& v2);

/// <summary>
/// ベクトルの長さを計算
/// </summary>
float Length(const Vector3& v);

/// <summary>
/// ベクトルを正規化（長さ1に）
/// </summary>
Vector3 Normalize(const Vector3& v);

/// <summary>
/// 外積を計算
/// </summary>
Vector3 Cross(const Vector3& v1, const Vector3& v2);


// ============================================
// Matrix4x4 関数
// ============================================



// --- 1. 行列の加法 (operator+) ---
Matrix4x4 operator+(const Matrix4x4& m1, const Matrix4x4& m2);

// --- 2. 行列の減法 (operator-) ---
Matrix4x4 operator-(const Matrix4x4& m1, const Matrix4x4& m2);

// --- 3. 行列の積 (operator*) ---
Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2);

// --- 4. 逆行列 (Inverse) ---
Matrix4x4 Inverse(const Matrix4x4& m);

// --- 5. 転置行列 (Transpose) ---
Matrix4x4 Transpose(const Matrix4x4& m);

// --- 6. 単位行列の作成 (MakeIdentity) ---
Matrix4x4 MakeIdentity4x4();

// --- 7.平行移動行列の作成 ---
Matrix4x4 MakeTranslateMatrix(const Vector3& translate);

// --- 8.拡大縮小行列の作成 ---
Matrix4x4 MakeScaleMatrix(const Vector3& scale);

// 1. X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);
// 2. Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);
// 3. Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);

// --- 回転行列からオイラー角（XYZ順）を取り出す ---
//   MakeRotateXYZMatrix の逆。スケールを含まない「回転だけ」の行列を渡すこと
//   ※ ジンバルロック（Y が ±90度付近）では、元の値と違う組が返ることがある
//    向きとしては正しいが、数値は一意に決まらない
Vector3 MakeEulerXYZFromMatrix(const Matrix4x4& rotation);

// --- 3次元アフィン変換行列の作成（オイラー角版） ---
//   ※ Transform::rotate を Quaternion にしたら、最終的には下の版だけを残す
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

// --- 3次元アフィン変換行列の作成（クォータニオン版） ---
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);

// --- UV 変換行列の作成 ---
//   シェーダの gMaterial.uvTransform に渡すもの
Matrix4x4 MakeUVMatrix(const UVTransform& uv);

// --- アフィン変換行列を scale / rotate / translate に分解する ---
//   MakeAffineMatrix の逆。rotate は XYZ 順のオイラー角
//   ※ 完全には元に戻らない
//     ・ジンバルロック（Y が ±90度付近）では x と z の分け方が一通りに決まらない
//     ・鏡像（負のスケール）は長さからは判別できないので、正のスケールとして返る
Transform DecomposeAffineMatrix(const Matrix4x4& matrix);

// --- レンダリングパイプライン ---

// 1. 透視投影行列の作成
Matrix4x4 MakePerspectiveMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

// 2. 正射影行列の作成
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

// 3. ビューポート変換行列の作成
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

// WVP行列の作成
Matrix4x4 MakeWVPMatrix(const Matrix4x4& world, const Matrix4x4& view, const Matrix4x4& projection);

// XYZ軸回転を合成した回転行列を作成
Matrix4x4 MakeRotateXYZMatrix(const Vector3& rotation);

// ベクトルを行列で変換（方向ベクトル用、w=0として扱う）
Vector3 TransformNormal(const Vector3& vector, const Matrix4x4& matrix);

// 点を行列で変換（位置ベクトル用、w=1として扱う）
//   TransformNormal との違いは「平行移動成分を足すかどうか」だけ
Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix);


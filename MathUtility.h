#pragma once
#include "Matrix4x4.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"


/// <summary>
/// 変換データ
/// </summary>
struct Transform {
    Vector3 scale;
    Vector3 rotate;
    Vector3 translate;
};

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

// --- 3次元アフィン変換行列の作成 ---
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

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


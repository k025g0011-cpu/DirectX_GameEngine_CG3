#include "MathUtility.h"
#include <cmath>

// ============================================
// Vector3 算術演算子
// ============================================

/// <summary>
/// ベクトルの加算 (v1 + v2)
/// </summary>
const Vector3 operator+(const Vector3& v1, const Vector3& v2) {
	return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

/// <summary>
/// ベクトルの減算 (v1 - v2)
/// </summary>
const Vector3 operator-(const Vector3& v1, const Vector3& v2) {
	return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

/// <summary>
/// スカラー倍 (v * s)
/// </summary>
const Vector3 operator*(const Vector3& v, float s) {
	return { v.x * s, v.y * s, v.z * s };
}

// ============================================
// Vector3 代入演算子
// ============================================

/// <summary>
/// 加算代入 (v1 += v2)
/// </summary>
Vector3& operator+=(Vector3& v1, const Vector3& v2) {
	v1.x += v2.x;
	v1.y += v2.y;
	v1.z += v2.z;
	return v1;
}

/// <summary>
/// 減算代入 (v1 -= v2)
/// </summary>
Vector3& operator-=(Vector3& v1, const Vector3& v2) {
	v1.x -= v2.x;
	v1.y -= v2.y;
	v1.z -= v2.z;
	return v1;
}

/// <summary>
/// スカラー倍代入 (v *= s)
/// </summary>
Vector3& operator*=(Vector3& v, float s) {
	v.x *= s;
	v.y *= s;
	v.z *= s;
	return v;
}

/// <summary>
/// スカラー除算代入 (v /= s)
/// </summary>
Vector3& operator/=(Vector3& v, float s) {
	// 0除算を防ぐ
	if (s != 0.0f) {
		v.x /= s;
		v.y /= s;
		v.z /= s;
	}
	return v;
}

// ============================================
// Vector3 関数
// ============================================

/// <summary>
/// 内積を計算
/// </summary>
float Dot(const Vector3& v1, const Vector3& v2) {
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

/// <summary>
/// ベクトルの長さを計算
/// </summary>
float Length(const Vector3& v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

/// <summary>
/// ベクトルを正規化（長さ1に）
/// </summary>
Vector3 Normalize(const Vector3& v) {
	float len = Length(v);
	if (len != 0.0f) {
		return { v.x / len, v.y / len, v.z / len };
	}
	// 長さが0の場合は零ベクトルを返す
	return { 0.0f, 0.0f, 0.0f };
}

/// <summary>
/// 外積を計算
/// </summary>
Vector3 Cross(const Vector3& v1, const Vector3& v2) {
	Vector3 result;
	result.x = v1.y * v2.z - v1.z * v2.y;
	result.y = v1.z * v2.x - v1.x * v2.z;
	result.z = v1.x * v2.y - v1.y * v2.x;
	return result;
}


// --- 1. 行列の加法 (operator+) ---
Matrix4x4 operator+(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][j] + m2.m[i][j];
		}
	}
	return result;
}

// --- 2. 行列の減法 (operator-) ---
Matrix4x4 operator-(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][j] - m2.m[i][j];
		}
	}
	return result;
}

// --- 3. 行列の積 (operator*) ---
Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result = { 0 };
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			for (int k = 0; k < 4; k++) {
				result.m[i][j] += m1.m[i][k] * m2.m[k][j];
			}
		}
	}
	return result;
}

// --- 4. 逆行列 (Inverse) ---
// 逆行列は単項演算（引数1つ）なので、スライドの形を維持しつつ参照渡しにします
Matrix4x4 Inverse(const Matrix4x4& m) {
	float temp[4][8]; // 元の行列と単位行列を並べた作業用配列
	Matrix4x4 result = {};

	// 1. 作業用配列の初期化（左側に元の行列、右側に単位行列を入れる）
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			temp[i][j] = m.m[i][j];
			temp[i][j + 4] = (i == j) ? 1.0f : 0.0f;
		}
	}

	// 2. 掃き出し法
	for (int i = 0; i < 4; i++) {
		// ピボット選択（絶対値が最大のものを選ぶと精度が安定するが、今回は簡易的に0チェックのみ）
		float pivot = temp[i][i];
		if (std::abs(pivot) < 1e-6f) return result; // 逆行列が存在しない場合は零行列を返す

		// i行目を pivot で割って temp[i][i] を 1 にする
		for (int j = 0; j < 8; j++) temp[i][j] /= pivot;

		// 他の行の i列目を 0 にする
		for (int k = 0; k < 4; k++) {
			if (i != k) {
				float factor = temp[k][i];
				for (int j = 0; j < 8; j++) {
					temp[k][j] -= temp[i][j] * factor;
				}
			}
		}
	}

	// 3. 右半分の計算結果を result にコピー
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = temp[i][j + 4];
		}
	}
	return result;
}

// --- 5. 転置行列 (Transpose) ---
Matrix4x4 Transpose(const Matrix4x4& m) {
	Matrix4x4 result;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m.m[j][i];
		}
	}
	return result;
}

// --- 6. 単位行列の作成 (MakeIdentity) ---
// 引数が必要ないため、そのまま関数として定義
Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result = { 0 };
	for (int i = 0; i < 4; i++) {
		result.m[i][i] = 1.0f;
	}
	return result;
}

// 1. 平行移動行列の作成
Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result = { 0 };
	// 単位行列をベースにする
	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;

	// 平行移動成分をセット (スライドの出力結果に合わせ4行目に配置)
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;

	return result;
}

// 2. 拡大縮小行列の作成
Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result = { 0 };
	// 対角成分にスケール値をセット
	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;
	result.m[3][3] = 1.0f;

	return result;
}

// 1. X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian) {
	// 全ての要素を0で初期化し、対角成分を1にする（単位行列にする）
	Matrix4x4 result = MakeIdentity4x4();
	float s = std::sin(radian);
	float c = std::cos(radian);

	result.m[1][1] = c;
	result.m[1][2] = s;
	result.m[2][1] = -s;
	result.m[2][2] = c;

	return result;
}

// 2. Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4(); // 単位行列で初期化
	float s = std::sin(radian);
	float c = std::cos(radian);

	result.m[0][0] = c;
	result.m[0][2] = -s;
	result.m[2][0] = s;
	result.m[2][2] = c;

	return result;
}

// 3. Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4(); // 単位行列で初期化
	float s = std::sin(radian);
	float c = std::cos(radian);

	result.m[0][0] = c;
	result.m[0][1] = s;
	result.m[1][0] = -s;
	result.m[1][1] = c;

	return result;
}

// --- 3次元アフィン変換行列の作成 ---
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	// 1. 各成分の行列を作成 (既存の関数を利用)
	Matrix4x4 matScale = MakeScaleMatrix(scale);

	Matrix4x4 matRotateX = MakeRotateXMatrix(rotate.x);
	Matrix4x4 matRotateY = MakeRotateYMatrix(rotate.y);
	Matrix4x4 matRotateZ = MakeRotateZMatrix(rotate.z);

	Matrix4x4 matTranslate = MakeTranslateMatrix(translate);

	// 2. 回転行列を合成 (スライドの一般的な順序 Rz * Rx * Ry を想定)
	// 既存の operator* を使用
	Matrix4x4 matRotate = matRotateX * matRotateY * matRotateZ;

	// 3. 全てを合成 (Scaling * Rotation * Translation)
	// 既存の operator* を使用
	Matrix4x4 result = matScale * matRotate * matTranslate;

	return result;
}

// 1.透視投影行列の作成
Matrix4x4 MakePerspectiveMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {

	float h = 1.0f / std::tan(fovY / 2.0f);
	float w = h / aspectRatio;
	float a = farClip / (farClip - nearClip);
	float b = (-farClip * nearClip) / (farClip - nearClip);

	Matrix4x4 result{};
	result.m[0][0] = w;
	result.m[1][1] = h;
	result.m[2][2] = a;
	result.m[2][3] = 1.0f;
	result.m[3][2] = b;

	return result;
}

// 2.正射影行列の作成
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 result{};

	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;

	return result;
}

// 3.ビューポート変換行列の作成
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	Matrix4x4 result{};
	result.m[0][0] = width / 2.0f;
	result.m[1][1] = -height / 2.0f; // Y軸は上下逆
	result.m[2][2] = maxDepth - minDepth;
	result.m[3][0] = left + width / 2.0f;
	result.m[3][1] = top + height / 2.0f;
	result.m[3][2] = minDepth;
	result.m[3][3] = 1.0f;
	return result;
}

// WVP行列の作成
Matrix4x4 MakeWVPMatrix(const Matrix4x4& world, const Matrix4x4& view, const Matrix4x4& projection) {

	return world * view * projection;
}


// XYZ軸回転を合成した回転行列を作成
Matrix4x4 MakeRotateXYZMatrix(const Vector3& rotation) {
	// X, Y, Z軸の回転行列を個別に作成
	Matrix4x4 rotateX = MakeRotateXMatrix(rotation.x);
	Matrix4x4 rotateY = MakeRotateYMatrix(rotation.y);
	Matrix4x4 rotateZ = MakeRotateZMatrix(rotation.z);

	// 合成順序: X * Y * Z
	return rotateX * rotateY * rotateZ;
}

// ベクトルを行列で変換（方向ベクトル用、w=0として扱う）
Vector3 TransformNormal(const Vector3& vector, const Matrix4x4& matrix) {
	Vector3 result;

	// w=0として扱うため、平行移動成分(m[3][0-2])は無視
	result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0];
	result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1];
	result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2];

	return result;
}

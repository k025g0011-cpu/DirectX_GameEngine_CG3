#include "MathUtility.h"
#include <algorithm>   // std::clamp（asin に渡す前に範囲を切る）
#include <cmath>
#include <utility>   // std::swap（ピボット選択で行を入れ替える）

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

		// ────────────────────────────────────────────────────────
		// 部分ピボット選択
		//   i列目で絶対値が最大の行を探し、i行目と入れ替える
		//   これが無いと、対角成分が 0 の行列で失敗する
		// 例：X軸90度回転は m[1][1] も m[2][2] も cos(90°) = 0
		//   精度の安定にも効く（小さい数で割らずに済む）
		// ────────────────────────────────────────────────────────
		int pivotRow = i;
		for (int k = i + 1; k < 4; k++) {
			if (std::abs(temp[k][i]) > std::abs(temp[pivotRow][i])) {
				pivotRow = k;
			}
		}
		if (pivotRow != i) {
			for (int j = 0; j < 8; j++) {
				std::swap(temp[i][j], temp[pivotRow][j]);
			}
		}

		float pivot = temp[i][i];

		// ここまでやって 0 なら、本当に逆行列が存在しない（ランクが足りない）
		//   零行列を返すと normalize で NaN になるので、単位行列を返して無害にする
		if (std::abs(pivot) < 1e-6f) {
			return MakeIdentity4x4();
		}

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

	// 2. 回転行列を合成（X → Y → Z の順。行ベクトル規約なので左から順に適用される）
	//	　　DecomposeAffineMatrix はこの順序を前提に逆算する。変えるなら両方直す
	Matrix4x4 matRotate = matRotateX * matRotateY * matRotateZ;

	// 3. 全てを合成 (Scaling * Rotation * Translation)
	// 既存の operator* を使用
	Matrix4x4 result = matScale * matRotate * matTranslate;

	return result;
}

// --- アフィン変換行列を scale / rotate / translate に分解する ---
//   MakeAffineMatrix の逆の操作。モデルファイルのノード行列を
//   GameObject の Transform に移すために使う
Transform DecomposeAffineMatrix(const Matrix4x4& matrix) {

	Transform result{};

	// ════════════════════════════════════════
	// ① 平行移動
	//   行ベクトル規約なので、4行目にそのまま入っている
	// ════════════════════════════════════════
	result.translate = Vector3{ matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };

	// ════════════════════════════════════════
	// ② スケール
	//   合成は S * R なので、各行は「回転した軸ベクトル × その軸のスケール」
	//   回転は長さを変えないので、行の長さがそのままスケールになる
	// ════════════════════════════════════════
	const Vector3 axisX{ matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] };
	const Vector3 axisY{ matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] };
	const Vector3 axisZ{ matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] };

	result.scale = Vector3{ Length(axisX), Length(axisY), Length(axisZ) };

	// ════════════════════════════════════════
	// ③ 回転部分だけを取り出す
	//   スケールで割って長さ1に戻すと、純粋な回転行列が残る
	//   スケール 0 の軸は割れないので、元の軸で代用する（0除算を避ける）
	// ════════════════════════════════════════
	Matrix4x4 rotation = MakeIdentity4x4();

	const Vector3 unitX =
		(result.scale.x > 1e-6f) ? Normalize(axisX) : Vector3{ 1.0f, 0.0f, 0.0f };
	const Vector3 unitY =
		(result.scale.y > 1e-6f) ? Normalize(axisY) : Vector3{ 0.0f, 1.0f, 0.0f };
	const Vector3 unitZ =
		(result.scale.z > 1e-6f) ? Normalize(axisZ) : Vector3{ 0.0f, 0.0f, 1.0f };

	rotation.m[0][0] = unitX.x; rotation.m[0][1] = unitX.y; rotation.m[0][2] = unitX.z;
	rotation.m[1][0] = unitY.x; rotation.m[1][1] = unitY.y; rotation.m[1][2] = unitY.z;
	rotation.m[2][0] = unitZ.x; rotation.m[2][1] = unitZ.y; rotation.m[2][2] = unitZ.z;

	// ════════════════════════════════════════
	// ④ 回転行列からオイラー角（XYZ順）を逆算する
	//
	//   R = Rx * Ry * Rz を展開すると、各成分はこうなる
	//     （s = sin, c = cos, 添字は軸）
	//
	//     m[0][0] = cy*cz          m[0][1] = cy*sz          m[0][2] = -sy
	//     m[1][2] = sx*cy
	//     m[2][1] = cx*sy*sz - sx*cz
	//     m[2][2] = cx*cy
	//
	//   ・m[0][2] は sy だけでできている       → y が直接求まる
	//   ・m[1][2] / m[2][2] は cy が共通       → 約分されて tan(x) になる
	//   ・m[0][1] / m[0][0] も cy が共通       → 約分されて tan(z) になる
	//
	//   atan2 を使うのは、tan だけだと ±180度の区別がつかないため
	// ════════════════════════════════════════
	result.rotate.y = std::asin(std::clamp(-rotation.m[0][2], -1.0f, 1.0f));

	//   ジンバルロック
	//     cos(y) が 0 に近いと、上の「約分」の分母が 0 になって x と z が求まらない
	//     幾何的にも、このとき X 回転と Z 回転は同じ見た目になり、答えが一つに定まらない
	//     → z を 0 に決め打ちして、x に全部押し付ける（よく使われる約束）
	if (std::abs(rotation.m[0][2]) < 0.9999f) {
		result.rotate.x = std::atan2(rotation.m[1][2], rotation.m[2][2]);
		result.rotate.z = std::atan2(rotation.m[0][1], rotation.m[0][0]);
	} else {
		//   z = 0 とすると m[1][1] = cx, m[2][1] = -sx になるので、ここから x が出る
		result.rotate.x = std::atan2(-rotation.m[2][1], rotation.m[1][1]);
		result.rotate.z = 0.0f;
	}

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

// 点を行列で変換（位置ベクトル用、w=1として扱う）
Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix) {
	Vector3 result;

	// w=1として扱うため、平行移動成分(m[3][0-2])も足す
	result.x = point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
	result.y = point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
	result.z = point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];

	// ※ 射影行列のように w が 1 でなくなる行列には使えない（w で割る処理が必要）
	//   　アフィン変換（スケール・回転・平行移動）専用

	return result;
}

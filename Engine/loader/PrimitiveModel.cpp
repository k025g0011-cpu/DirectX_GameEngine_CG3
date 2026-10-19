#include "PrimitiveModel.h"
#include <cmath>
#include "MathUtility.h"   // kPi

// ════════════════════════════════════════════════════════════════════
// 緯度経度で分割した球をプロシージャル生成する
// ════════════════════════════════════════════════════════════════════
ModelData PrimitiveModel::CreateSphere(uint32_t subdivision, const std::string& texturePath) {

	ModelData modelData;

	const uint32_t vertexCount = (subdivision + 1) * (subdivision + 1);  // 格子点の数
	const uint32_t indexCount = subdivision * subdivision * 6;           // マス数×6
	modelData.vertices.resize(vertexCount);   // 先に必要な数だけ確保しておく
	modelData.indices.resize(indexCount);

	const float kLonEvery = 2.0f * kPi / float(subdivision);   // 経度1つ分の角度 φd
	const float kLatEvery = kPi / float(subdivision);          // 緯度1つ分の角度 θd

	// ────────────────────────────────────────
	// 頂点データ（格子点を1個ずつ置いていく）
	// ────────────────────────────────────────
	// 緯度方向に分割　-π/2 ～ π/2
	for (uint32_t latIndex = 0; latIndex <= subdivision; ++latIndex) {       // <= （端の点も作る）
		float lat = -kPi / 2.0f + kLatEvery * float(latIndex);               // 現在の緯度 θ

		// 経度方向に分割　0 ～ 2π
		for (uint32_t lonIndex = 0; lonIndex <= subdivision; ++lonIndex) {   // <= （一周を閉じる）
			float lon = kLonEvery * float(lonIndex);                         // 現在の経度 φ

			// この格子点の番号。横に (subdivision+1) 個ならんでいる
			uint32_t index = latIndex * (subdivision + 1) + lonIndex;

			// 単位球（半径1）上の座標
			modelData.vertices[index].position.x = std::cos(lat) * std::cos(lon);
			modelData.vertices[index].position.y = std::sin(lat);
			modelData.vertices[index].position.z = std::cos(lat) * std::sin(lon);

			// uv。uは経度が一周で0→1、vは緯度が下から上で1→0
			modelData.vertices[index].texcoord.x = float(lonIndex) / float(subdivision);
			modelData.vertices[index].texcoord.y = 1.0f - float(latIndex) / float(subdivision);

			// 単位球なので中心から表面へのベクトル＝位置。それがそのまま法線になる
			modelData.vertices[index].normal.x = modelData.vertices[index].position.x;
			modelData.vertices[index].normal.y = modelData.vertices[index].position.y;
			modelData.vertices[index].normal.z = modelData.vertices[index].position.z;
		}
	}

	// ────────────────────────────────────────
	// インデックスデータ（マスごとに6個）
	// ────────────────────────────────────────
	for (uint32_t latIndex = 0; latIndex < subdivision; ++latIndex) {        // < （マスの数）
		for (uint32_t lonIndex = 0; lonIndex < subdivision; ++lonIndex) {

			// このマスが書き込むインデックス配列の開始位置（1マスにつき6個）
			uint32_t start = (latIndex * subdivision + lonIndex) * 6;

			// このマスの四隅の「格子点の番号」
			uint32_t a = (latIndex + 0) * (subdivision + 1) + (lonIndex + 0);   // 左下
			uint32_t b = (latIndex + 1) * (subdivision + 1) + (lonIndex + 0);   // 左上
			uint32_t c = (latIndex + 0) * (subdivision + 1) + (lonIndex + 1);   // 右下
			uint32_t d = (latIndex + 1) * (subdivision + 1) + (lonIndex + 1);   // 右上

			// 三角形1枚目　a → b → c
			modelData.indices[start + 0] = a;
			modelData.indices[start + 1] = b;
			modelData.indices[start + 2] = c;
			// 三角形2枚目　c → b → d
			modelData.indices[start + 3] = c;
			modelData.indices[start + 4] = b;
			modelData.indices[start + 5] = d;
		}
	}

	// ────────────────────────────────────────
	// 既定のマテリアルを1つ用意する（mtl が存在しないため）
	//   テクスチャは呼び出し側が決める。空なら Model が white1x1 を使う
	// ────────────────────────────────────────
	MaterialData material{};
	material.name = "Sphere";
	material.textureFilePath = texturePath;
	modelData.materials.push_back(material);

	// 全体を1つの Mesh として扱う（インデックス描画）
	Mesh mesh{};
	mesh.materialIndex = 0;
	mesh.startVertex = 0;
	mesh.vertexCount = static_cast<uint32_t>(modelData.vertices.size());
	mesh.startIndex = 0;
	mesh.indexCount = static_cast<uint32_t>(modelData.indices.size());
	modelData.meshes.push_back(mesh);

	return modelData;
}

// ════════════════════════════════════════════════════════════════════
// XY 平面に四角形を1枚作る
//   インデックスは使わない（四角形では節約にならず、使う側が複雑になるだけ）
// ════════════════════════════════════════════════════════════════════
ModelData PrimitiveModel::CreatePlane(float size, const std::string& texturePath) {

	ModelData modelData;

	// 中心を原点に置くので、半分の長さだけ上下左右に広げる
	const float half = size * 0.5f;

	// ────────────────────────────────────────
	// 四隅の座標と、貼り付ける UV
	//
	//    TL(0,0) ──────── TR(1,0)      ← 画面の上
	//        │  ＼          │
	//        │     ＼       │
	//    BL(0,1) ──────── BR(1,1)      ← 画面の下
	//
	//   UV は左上が (0,0)、右下が (1,1)。画像ファイルの並びと同じ
	// ────────────────────────────────────────
	const Vector3 topLeft = { -half,  half, 0.0f };
	const Vector3 topRight = { half,  half, 0.0f };
	const Vector3 bottomLeft = { -half, -half, 0.0f };
	const Vector3 bottomRight = { half, -half, 0.0f };

	// 手前（カメラ側 = -Z 方向）を向く板にする
	const Vector3 normal = { 0.0f, 0.0f, -1.0f };

	// ────────────────────────────────────────
	// 三角形2枚ぶんの頂点を、時計回りになるよう並べる
	//   DirectX の既定では「画面上で時計回り＝表」
	// ────────────────────────────────────────
	modelData.vertices.resize(6);

	// 1枚目　TL → TR → BL
	modelData.vertices[0] = { topLeft,     { 0.0f, 0.0f }, normal };
	modelData.vertices[1] = { topRight,    { 1.0f, 0.0f }, normal };
	modelData.vertices[2] = { bottomLeft,  { 0.0f, 1.0f }, normal };

	// 2枚目　BL → TR → BR
	modelData.vertices[3] = { bottomLeft,  { 0.0f, 1.0f }, normal };
	modelData.vertices[4] = { topRight,    { 1.0f, 0.0f }, normal };
	modelData.vertices[5] = { bottomRight, { 1.0f, 1.0f }, normal };

	// ────────────────────────────────────────
	// 既定のマテリアルを1つ用意する
	// ────────────────────────────────────────
	MaterialData material{};
	material.name = "Plane";
	material.textureFilePath = texturePath;
	modelData.materials.push_back(material);

	// 全体を1つの Mesh として扱う（インデックスなし描画）
	Mesh mesh{};
	mesh.materialIndex = 0;
	mesh.startVertex = 0;
	mesh.vertexCount = static_cast<uint32_t>(modelData.vertices.size());
	mesh.startIndex = 0;
	mesh.indexCount = 0;   // ★0 にすると Model::Draw が DrawInstanced を選ぶ
	modelData.meshes.push_back(mesh);

	return modelData;
}
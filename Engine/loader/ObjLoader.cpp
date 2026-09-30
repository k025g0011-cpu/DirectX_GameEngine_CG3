#include "ObjLoader.h"

#include <cassert>
#include <fstream>
#include <sstream>
#include <vector>

// ════════════════════════════════════════════════════════════════════
// このファイルの中だけで使う関数
//   mtl の読み込みは obj の読み込みの一部。外から単独で使うことはない
// ════════════════════════════════════════════════════════════════════
namespace {

	// ════════════════════════════════════════
	// mtlファイル(テキスト)を読み、マテリアル情報を「全部」取り出す
	// directoryPath : ファイルが置いてあるフォルダ (例 "resources")
	// filename      : mtlのファイル名             (例 "suzanne.mtl")
	// 戻り値        : mtl に書かれていた順のマテリアル一覧
	// ════════════════════════════════════════
	std::vector<MaterialData> LoadMaterialTemplateFile(
		const std::string& directoryPath, const std::string& filename) {

		// ────────────────────────────────────────
		// 1. 必要な変数の宣言
		// ────────────────────────────────────────
		std::vector<MaterialData> materials;   // 構築して返す完成品
		std::string line;                      // ファイルから読んだ1行

		// ────────────────────────────────────────
		// 2. ファイルを開く
		// ────────────────────────────────────────
		std::ifstream file(directoryPath + "/" + filename);
		assert(file.is_open());   // 開けなかったら止める(objと同じ階層にmtlが無い等)

		// ────────────────────────────────────────
		// 3. ファイルを読み、マテリアルを1つずつ構築していく
		// ────────────────────────────────────────
		while (std::getline(file, line)) {
			std::string identifier;
			std::istringstream s(line);
			s >> identifier;

			// ─── 新しいマテリアルの開始 ───
			if (identifier == "newmtl") {
				MaterialData material{};
				s >> material.name;              // obj の usemtl と照合する名前
				materials.push_back(material);
				continue;
			}

			// ─── newmtl より前の行（先頭のコメントなど）は捨てる ───
			if (materials.empty()) {
				continue;
			}

			// ─── 書き込み先は「いちばん最後に作ったマテリアル」 ───
			//     ★ 毎回 back() を呼び直す（push_back で vector が引っ越すため）
			MaterialData& current = materials.back();

			// ─── 拡散色に使うテクスチャ ───
			if (identifier == "map_Kd") {
				std::string textureFilename;
				s >> textureFilename;
				// mtlにはファイル名しか書かれていないので、ディレクトリ名と連結する
				current.textureFilePath = directoryPath + "/" + textureFilename;
			}
			// ─── 拡散色（RGB） ───
			else if (identifier == "Kd") {
				s >> current.color.x >> current.color.y >> current.color.z;
			}
			// ─── 不透明度（1.0 = 不透明） ───
			else if (identifier == "d") {
				s >> current.color.w;
			}
			// ─── 透明度（d の逆。書き出すソフトによってはこちら） ───
			else if (identifier == "Tr") {
				float transparency = 0.0f;
				s >> transparency;
				current.color.w = 1.0f - transparency;
			}
			// 上記以外( Ka / Ks / Ns / Ni / Ke / illum など )は今は無視する
		}

		// ────────────────────────────────────────
		// 4. マテリアル一覧を返す
		// ────────────────────────────────────────
		return materials;
	}

}   // namespace

// ════════════════════════════════════════════════════════════════════
// objファイル(テキスト)を1行ずつ読み、VertexDataの配列(ModelData)を構築する
// ════════════════════════════════════════════════════════════════════
ModelData ObjLoader::Load(const std::string& directoryPath, const std::string& filename) {

	// ────────────────────────────────────────
	// 1. 必要な変数の宣言
	// ────────────────────────────────────────
	ModelData modelData;                 // 構築して返す完成品
	std::vector<Vector3> positions;      // objの v  を溜める作業用配列
	std::vector<Vector3> normals;        // objの vn を溜める作業用配列
	std::vector<Vector2> texcoords;      // objの vt を溜める作業用配列
	std::string line;                    // ファイルから読んだ1行

	// ────────────────────────────────────────
	// 2. ファイルを開く
	// ifstreamはデストラクタで自動的に閉じるのでcloseは不要
	// ────────────────────────────────────────
	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());   // 開けなかったら止める(パス間違い・作業ディレクトリ違い)

	// ────────────────────────────────────────
	// 2.5 Mesh を切り分けるための状態
	//     usemtl が出てくるたびに、そこまでに積んだ面を1つの Mesh として確定する
	// ────────────────────────────────────────
	uint32_t currentMaterialIndex = 0;   // いま使っているマテリアルの番号
	uint32_t currentStartVertex = 0;     // いまの Mesh が始まった頂点番号

	// ここまでに積んだ面を1つの Mesh として確定する
	auto flushMesh = [&]() {
		// 「今の頂点数」−「この Mesh が始まった位置」＝ この Mesh の頂点数
		uint32_t vertexCount =
			static_cast<uint32_t>(modelData.vertices.size()) - currentStartVertex;

		// 1面も積まれていなければ Mesh を作らない
		//   （面より先に usemtl が来る obj で、空の Mesh ができるのを防ぐ）
		if (vertexCount == 0) {
			return;
		}

		Mesh mesh{};
		mesh.materialIndex = currentMaterialIndex;
		mesh.startVertex = currentStartVertex;
		mesh.vertexCount = vertexCount;
		mesh.startIndex = 0;   // obj はインデックスを作らないので使わない
		mesh.indexCount = 0;
		modelData.meshes.push_back(mesh);

		// 次の Mesh はここから始まる
		currentStartVertex = static_cast<uint32_t>(modelData.vertices.size());
		};

	// ────────────────────────────────────────
	// 3. ファイルを読み、ModelDataを構築していく
	// ────────────────────────────────────────
	while (std::getline(file, line)) {          // ファイルの終端まで1行ずつ読む
		std::string identifier;                  // 行の先頭の識別子(v / vt / vn / f など)
		std::istringstream s(line);              // 1行を空白区切りで読むためのstream
		s >> identifier;                         // 先頭の識別子を読む

		// ─── 頂点位置 ───
		if (identifier == "v") {
			Vector3 position;
			s >> position.x >> position.y >> position.z;
			positions.push_back(position);

			// ─── テクスチャ座標 ───
		} else if (identifier == "vt") {
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			// ════════════════════════════════════════
			// 左下原点Texture座標系(obj) → 左上原点Texture座標系(DirectX)
			// uは両方とも右向きなので変換不要。vの向きだけが逆なので反転する
			// ※ 3D座標系(手系)の変換とはまったく別の話。positionのyは触らない
			// ════════════════════════════════════════
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);

			// ─── 法線 ───
		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);

			// ─── 面(三角形) ───
		} else if (identifier == "f") {

			// 右手系→左手系の変換で回り順を逆にするため、一度ここに溜めてから登録する
			VertexData triangle[3];

			// 面は三角形限定。四角形以上のポリゴンは未対応
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition;    // "2/1/1" のような1頂点ぶんの定義
				s >> vertexDefinition;

				// ════════════════════════════════════════
				// 「位置/UV/法線」の3つのIndexを '/' 区切りで分解する
				//   obj の Index は1始まりなので、0 を「指定なし」の印として使える
				//   例： "47/12/3" → {47, 12, 3}
				//        "47//3"   → {47,  0, 3}   UV を持たないモデル（スザンヌ等）
				//        "47"      → {47,  0, 0}   位置だけのモデル
				// ════════════════════════════════════════
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3] = { 0, 0, 0 };
				for (int32_t element = 0; element < 3; ++element) {
					std::string index;
					std::getline(v, index, '/'); // 第3引数で区切り文字を'/'に指定
					// 空欄（"//" や、そもそも欄が無い）なら 0 のままにする
					//   std::stoi("") は例外を投げるので、必ず中身を確認してから呼ぶ
					if (!index.empty()) {
						elementIndices[element] = std::stoi(index);  // 文字列→整数
					}
				}

				// ════════════════════════════════════════
				// Indexから実際の値を取り出して1頂点を組み立てる
				// objのIndexは1始まりなので、配列の添字にするときは -1 する
				// ════════════════════════════════════════
				// 位置は必須
				Vector3 position = positions[elementIndices[0] - 1];

				// UV が無いモデルは (0,0) にしておく
				Vector2 texcoord = { 0.0f, 0.0f };
				if (elementIndices[1] != 0) {
					texcoord = texcoords[elementIndices[1] - 1];
				}

				// 法線が無いモデルは (0,0,0)
				Vector3 normal = { 0.0f, 0.0f, 0.0f };
				if (elementIndices[2] != 0) {
					normal = normals[elementIndices[2] - 1];
				}

				// ════════════════════════════════════════
				// 右手座標系(obj) → 左手座標系(DirectX) へ変換 ①
				// 手系を変えるには軸を1本だけ反転すればよい
				// 授業の指定どおり x を反転する。この選び方では
				// 「Blenderで見た向きの真後ろから見た状態」になる
				// 位置だけでなく法線も反転しないとライティングが壊れる
				// ════════════════════════════════════════
				position.x *= -1.0f;
				normal.x *= -1.0f;

				// この場ではpush_backせず、一時配列に溜める
				triangle[faceVertex] = { position, texcoord, normal };
			}

			// ════════════════════════════════════════
			// 右手座標系(obj) → 左手座標系(DirectX) へ変換 ②
			// x反転で鏡像になり表裏が逆転するので、頂点の回り順も逆にして打ち消す
			// 右手系の表は反時計回り、左手系の表は時計回り
			// ════════════════════════════════════════
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);

		} else if (identifier == "mtllib") {
			// materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;
			// mtl に書かれていたマテリアルを「全部」受け取る
			modelData.materials = LoadMaterialTemplateFile(directoryPath, materialFilename);

		} else if (identifier == "usemtl") {
			std::string materialName;
			s >> materialName;

			// ここまでに積んだ面を1つの Mesh として確定する
			flushMesh();

			// 名前から、使うマテリアルの番号を探す
			currentMaterialIndex = 0;   // 見つからなければ0番を使う
			for (uint32_t i = 0; i < modelData.materials.size(); ++i) {
				if (modelData.materials[i].name == materialName) {
					currentMaterialIndex = i;
					break;
				}
			}
		}
		// 上記以外の識別子( # / o / s )は今は無視する
	}

	// ────────────────────────────────────────
	// 4. 後始末
	// ────────────────────────────────────────

	// 最後の usemtl 以降に積んだ面を Mesh として確定する
	//   usemtl が1つも無い obj でも、ここで全頂点ぶんの Mesh が1個できる
	flushMesh();

	// mtl が無い／読めなかった場合のデフォルトマテリアル（白・テクスチャなし）
	//   Mesh の materialIndex は 0 のままなので、これで辻褄が合う
	if (modelData.materials.empty()) {
		modelData.materials.push_back(MaterialData{});
	}

	// ────────────────────────────────────────
	// 5. ModelDataを返す
	// ────────────────────────────────────────
	return modelData;
}
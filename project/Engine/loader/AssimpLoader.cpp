#include "AssimpLoader.h"

#include <cassert>
#include <filesystem>

#include <windows.h>   // OutputDebugStringA（読み込み失敗の理由を出すため）

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace {

	// ────────────────────────────────────────────────────────────────
	// 読み込み時のオプション
	//   右手座標系（obj / glTF）→ 左手座標系（DirectX）への変換と、
	//   UV の原点の違いを吸収する
	//
	//   aiProcess_ConvertToLeftHanded は、次の3つをまとめた別名
	//     ① MakeLeftHanded    … Z を反転して手系を変える
	//     ② FlipWindingOrder  … 軸を1本反転すると表裏が逆転するので、
	//                            頂点の回り順も逆にして打ち消す
	//                            右手系の表は反時計回り、左手系の表は時計回り
	//     ③ FlipUVs           … UV の原点が左下（obj / glTF）→ 左上（DirectX）
	//                            u は両方とも右向きなので v だけ反転する
	//                            ※ 3D 座標系の手系とは別の話。position の y は触らない
	//
	//   ・反転する軸に Z を選ぶ理由
	//     右手系のカメラは -Z を、左手系のカメラは +Z を見る。
	//     Z を反転すると、この「カメラの前後の違い」も同時に打ち消される
	//       → Blender で見たままの正面・左右でエンジンに出る
	//     X を反転しても手系は変わるが前後が残り、裏側から見た鏡像になる
	//
	//   ・assimp にやらせる利点
	//     頂点だけでなく、ノードの行列・ボーンのオフセット行列・
	//     アニメーションのキーにも同じ変換がかかる
	//     手動で反転すると、それらを全部自分で変換する必要がある
	// ────────────────────────────────────────────────────────────────
	constexpr uint32_t kImportFlags =
		aiProcess_ConvertToLeftHanded |    // Z 反転 ＋ 回り順反転 ＋ UV の v 反転
		aiProcess_Triangulate |            // 四角形以上を三角形に分割
		aiProcess_GenSmoothNormals |       // 法線が無いモデルだけ、自動生成する
		aiProcess_JoinIdenticalVertices;   // 同じ頂点をまとめる（重複を除く）


	// ────────────────────────────────────────────────────────────────
	// 外部ファイルのテクスチャパスを解決する
	//   assimp が返すのはモデルからの相対パスなので、フォルダをくっつける
	//   埋め込み（"*0"）は呼び出し側で分岐済みなので、ここには来ない
	// ────────────────────────────────────────────────────────────────
	std::string ResolveTexturePath(const std::string& directoryPath, const aiString& raw) {

		const std::string name = raw.C_Str();

		if (directoryPath.empty()) {
			return name;
		}
		return directoryPath + "/" + name;
	}

	// ────────────────────────────────────────────────────────────────
	// assimp の行列を、エンジンの行列に直す
	//
	//   やることは「規約を合わせる」1つだけ
	//     assimp は「列ベクトル規約」（M × v）、エンジンは「行ベクトル規約」（v × M）
	//     → 転置する。assimp の平行移動 a4/b4/c4 が m[3][0..2] に来る
	//
	//   座標系（手系）の変換は aiProcess_ConvertToLeftHanded が
	//   ノードの行列にも適用済みなので、ここでは何もしなくてよい
	// ────────────────────────────────────────────────────────────────
	Matrix4x4 ToEngineMatrix(const aiMatrix4x4& src) {

		Matrix4x4 result{};

		result.m[0][0] = src.a1; result.m[0][1] = src.b1; result.m[0][2] = src.c1; result.m[0][3] = src.d1;
		result.m[1][0] = src.a2; result.m[1][1] = src.b2; result.m[1][2] = src.c2; result.m[1][3] = src.d2;
		result.m[2][0] = src.a3; result.m[2][1] = src.b3; result.m[2][2] = src.c3; result.m[2][3] = src.d3;
		result.m[3][0] = src.a4; result.m[3][1] = src.b4; result.m[3][2] = src.c4; result.m[3][3] = src.d4;

		return result;
	}


	// ────────────────────────────────────────────────────────────────
	// 埋め込みテクスチャを ModelData に積み、その添字を返す
	//   同じ画像を2回積まないよう、名前で探してから足す
	// ────────────────────────────────────────────────────────────────
	int32_t AddEmbeddedTexture(ModelData& modelData, const std::string& modelPath,
		const std::string& rawPath, const aiTexture& src) {

		//   mHeight が 0 でないものは「生のピクセル」。今回は対応しない
		//     Blender は png で埋め込むので、実際には来ない
		if (src.mHeight != 0) {
			OutputDebugStringA("AssimpLoader: 非圧縮の埋め込みテクスチャは未対応\n");
			return -1;
		}

		//   キャッシュのキー。モデルのパス＋"*0" で一意になる
		const std::string name = modelPath + "#" + rawPath;

		for (size_t i = 0; i < modelData.embeddedTextures.size(); ++i) {
			if (modelData.embeddedTextures[i].name == name) {
				return static_cast<int32_t>(i);   // 既に積んである
			}
		}

		//   mHeight == 0 のとき、mWidth は「バイト数」
		const uint8_t* begin = reinterpret_cast<const uint8_t*>(src.pcData);

		EmbeddedTexture texture{};
		texture.name = name;
		texture.bytes.assign(begin, begin + src.mWidth);

		modelData.embeddedTextures.push_back(std::move(texture));
		return static_cast<int32_t>(modelData.embeddedTextures.size() - 1);
	}

	// ────────────────────────────────────────────────────────────────
	// ノードを再帰でたどり、ModelData::nodes を作る
	//   変換は localMatrix に入れるだけ。頂点には焼き込まない
	//     → 階層は GameObject ツリーが持ち、親子の合成は GetWorldMatrix が行う
	//   戻り値：作ったノードの添字
	// ────────────────────────────────────────────────────────────────
	uint32_t BuildNode(const aiNode& src, ModelData& modelData) {

		//   先に自分の枠だけ確保して、添字を覚えておく
		//   子を作ると nodes が伸びて、要素への参照が無効になるため
		const uint32_t selfIndex = static_cast<uint32_t>(modelData.nodes.size());
		modelData.nodes.emplace_back();

		{
			ModelNode& node = modelData.nodes[selfIndex];
			node.name = src.mName.C_Str();
			node.localMatrix = ToEngineMatrix(src.mTransformation);
			//   mMeshes は scene->mMeshes の添字。Mesh を同じ順で作るので、そのまま使える
			node.meshIndices.assign(src.mMeshes, src.mMeshes + src.mNumMeshes);
		}

		//   子を作る
		//   この中で nodes が伸びるので、上で取った参照は使わない。毎回添字で取り直す
		for (uint32_t i = 0; i < src.mNumChildren; ++i) {
			const uint32_t childIndex = BuildNode(*src.mChildren[i], modelData);
			modelData.nodes[selfIndex].children.push_back(childIndex);
		}

		return selfIndex;
	}


} // namespace

// ════════════════════════════════════════════════════════════════════
// 読み込み本体
// ════════════════════════════════════════════════════════════════════
ModelData AssimpLoader::Load(const std::string& filePath) {

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filePath.c_str(), kImportFlags);

	//   失敗の理由は importer.GetErrorString() にしか入っていない
	//     assert だけだと「何が悪いか」が分からないので、先に出力ウィンドウへ出す
	if (scene == nullptr || !scene->HasMeshes()) {
		const std::string message =
			"AssimpLoader: failed to load \"" + filePath + "\"\n"
			"  reason: " + importer.GetErrorString() + "\n";
		OutputDebugStringA(message.c_str());
		assert(false);
	}

	//   モデルと同じフォルダ。テクスチャの相対パスを解決するのに使う
	const std::string directoryPath =
		std::filesystem::path(filePath).parent_path().string();

	ModelData modelData{};

	// ════════════════════════════════════════
	// ① マテリアル
	//    assimp は materialIndex で参照するので、順番を崩さず全部作る
	// ════════════════════════════════════════
	modelData.materials.resize(scene->mNumMaterials);

	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {

		const aiMaterial* src = scene->mMaterials[materialIndex];
		MaterialData& dst = modelData.materials[materialIndex];

		//   名前（Inspector の表示用）
		aiString name;
		if (src->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
			dst.name = name.C_Str();
		}

		//   拡散反射色（.mtl の Kd / glTF の baseColorFactor）
		aiColor4D diffuse{};
		if (src->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
			dst.color = Vector4{ diffuse.r, diffuse.g, diffuse.b, diffuse.a };
		}

		//   不透明度（.mtl の d / Tr）
		//     obj では Kd のアルファではなく、別のキーに入る
		float opacity = 1.0f;
		if (src->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
			dst.color.w *= opacity;   // 掛け算なので、両方あっても壊れない
		}

		//   鏡面反射色（.mtl の Ks）
		aiColor3D specular{};
		if (src->Get(AI_MATKEY_COLOR_SPECULAR, specular) == AI_SUCCESS) {
			dst.specularColor = Vector3{ specular.r, specular.g, specular.b };
		}

		//   光沢度（.mtl の Ns）
		//     Ns 0 は「光らない」の意味だが、シェーダの pow(x, 0) は 1（＝最大）になる
		//       意味が真逆なので、指数は 1 に留めて、鏡面反射色のほうを 0 にして消す
		float shininess = 0.0f;
		if (src->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
			if (shininess > 0.0f) {
				dst.shininess = shininess;
			} else {
				dst.shininess = 1.0f;
				dst.specularColor = Vector3{ 0.0f, 0.0f, 0.0f };
			}
		}

		//   テクスチャ（.mtl の map_Kd / glTF の baseColorTexture）
		//     模様に使うのは DIFFUSE
		if (src->GetTextureCount(aiTextureType_DIFFUSE) != 0) {

			aiString texturePath;
			src->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath);

			//   "*0" のような名前なら、モデルに埋め込まれた画像
			//     GetEmbeddedTexture が "*0" の解決までやってくれる
			const aiTexture* embedded = scene->GetEmbeddedTexture(texturePath.C_Str());

			if (embedded != nullptr) {
				//   .glb：画像のバイト列を ModelData に積む
				dst.embeddedTextureIndex =
					AddEmbeddedTexture(modelData, filePath, texturePath.C_Str(), *embedded);
			} else {
				//   .obj / .gltf：外部ファイルなのでパスを解決する
				dst.textureFilePath = ResolveTexturePath(directoryPath, texturePath);
			}
		}
	}

	// ════════════════════════════════════════
	// ② ノード階層
	//    Blender の Z-up → Y-up 変換は、ノードの回転として書き出される
	//    その回転はここでは消費せず、ノードの localMatrix として残す
	//      → GameScene が GameObject の Transform に移す
	// ════════════════════════════════════════
	if (scene->mRootNode != nullptr) {
		BuildNode(*scene->mRootNode, modelData);
	}

	// ════════════════════════════════════════
	// ③ メッシュ
	//    aiMesh ごとに Mesh を作り、頂点は1本の配列に並べていく
	// ════════════════════════════════════════
	modelData.meshes.reserve(scene->mNumMeshes);

	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

		const aiMesh* mesh = scene->mMeshes[meshIndex];
		assert(mesh->HasNormals());   // GenSmoothNormals があるので必ず持っている

		//   UV を持たないモデルがある（suzanne.obj など）
		//   その場合は (0,0) で代用する（Model 側が white1x1 を貼る）
		//   ループの外で1回だけ調べる（頂点ごとに聞く必要はない）
		const bool hasTexcoord = mesh->HasTextureCoords(0);

		Mesh dstMesh{};
		dstMesh.materialIndex = mesh->mMaterialIndex;
		dstMesh.startVertex = static_cast<uint32_t>(modelData.vertices.size());
		dstMesh.startIndex = static_cast<uint32_t>(modelData.indices.size());

		//   ③ 頂点
		//     assimp は重複を除いた状態で持っているので、そのまま写すだけ
		modelData.vertices.reserve(modelData.vertices.size() + mesh->mNumVertices);

		for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {

			const aiVector3D& position = mesh->mVertices[vertexIndex];
			const aiVector3D& normal = mesh->mNormals[vertexIndex];

			VertexData vertex{};

			//   座標系の変換は assimp が済ませている（詳細は kImportFlags の説明）
			//   ノードの変換は焼き込まない。メッシュのローカル空間のまま置く
			//   階層は GameObject ツリーが持ち、変換は毎フレーム GPU 側で効く
			vertex.position = Vector3{ position.x, position.y, position.z };
			vertex.normal = Vector3{ normal.x,   normal.y,   normal.z };

			//   UV を持たないモデルは (0,0)
			//     Model 側が white1x1 を貼るので、見た目は単色になる
			vertex.texcoord = Vector2{ 0.0f, 0.0f };
			if (hasTexcoord) {
				const aiVector3D& uv = mesh->mTextureCoords[0][vertexIndex];
				vertex.texcoord = Vector2{ uv.x, uv.y };
			}

			modelData.vertices.push_back(vertex);
		}
		dstMesh.vertexCount = mesh->mNumVertices;

		//   ④ 面（三角形）
		//     番号は「この Mesh の中での番号」のまま積む
		//     描画時に BaseVertexLocation = startVertex を足してもらう
		modelData.indices.reserve(modelData.indices.size() + mesh->mNumFaces * 3);

		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {

			const aiFace& face = mesh->mFaces[faceIndex];
			assert(face.mNumIndices == 3);   // Triangulate 済みなので必ず3

			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				modelData.indices.push_back(face.mIndices[element]);
			}
		}
		dstMesh.indexCount =
			static_cast<uint32_t>(modelData.indices.size()) - dstMesh.startIndex;

		modelData.meshes.push_back(dstMesh);
	}

	return modelData;
}
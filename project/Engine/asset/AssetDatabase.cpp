#include "AssetDatabase.h"

#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

// 長いので別名を付ける
namespace fs = std::filesystem;

// ════════════════════════════════════════════════════════════════════
// このファイルの中だけで使う関数
// ════════════════════════════════════════════════════════════════════
namespace {

	// 文字列を小文字に揃える（.PNG と .png を同じ扱いにするため）
	std::string ToLower(const std::string& text) {
		std::string result = text;
		std::transform(result.begin(), result.end(), result.begin(),
			// unsigned char にキャストしないと、負の値で未定義動作になる
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

}   // namespace

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void AssetDatabase::Initialize(const std::string& rootDirectory, std::ostream& logStream) {

	rootDirectory_ = rootDirectory;
	logStream_ = &logStream;

	Refresh();
}

// ════════════════════════════════════════════════════════════════════
// ルートフォルダを走査して一覧を作り直す
// ════════════════════════════════════════════════════════════════════
void AssetDatabase::Refresh() {

	assets_.clear();

	// フォルダが無ければ空のまま（起動はできる）
	if (!fs::exists(rootDirectory_)) {
		Log(*logStream_, "AssetDatabase: directory not found: " + rootDirectory_ + "\n");
		return;
	}

	// ────────────────────────────────────────────────────────────────
	// サブフォルダまで含めて全ファイルを見る
	// ────────────────────────────────────────────────────────────────
	for (const fs::directory_entry& entry : fs::recursive_directory_iterator(rootDirectory_)) {

		// フォルダやショートカットは飛ばす
		if (!entry.is_regular_file()) {
			continue;
		}

		const fs::path& filePath = entry.path();

		// 知らない拡張子は一覧に載せない
		const AssetType type = DetectType(filePath.extension().generic_string());
		if (type == AssetType::Unknown) {
			continue;
		}

		AssetEntry asset{};
		// generic_string() は区切りを / に統一してくれる
		//   Windows の native な \ のままだと、他のクラスに渡す文字列と食い違う
		asset.path = filePath.generic_string();
		asset.name = filePath.stem().generic_string();
		asset.extension = ToLower(filePath.extension().generic_string());
		asset.type = type;

		assets_.push_back(asset);
	}

	// ────────────────────────────────────────────────────────────────
	// 種類 → 名前 の順に並べ替える（一覧が見やすくなる）
	// ────────────────────────────────────────────────────────────────
	std::sort(assets_.begin(), assets_.end(),
		[](const AssetEntry& lhs, const AssetEntry& rhs) {
			if (lhs.type != rhs.type) {
				return lhs.type < rhs.type;
			}
			return lhs.name < rhs.name;
		});

	Log(*logStream_, "AssetDatabase: " + std::to_string(assets_.size()) + " assets found\n");
}

// ════════════════════════════════════════════════════════════════════
// 外部ファイルを取り込む
//   resources/ にコピーして、一覧を作り直す
// ════════════════════════════════════════════════════════════════════
bool AssetDatabase::Import(const std::string& sourceFilePath) {

	const fs::path source = sourceFilePath;

	// ────────────────────────────────────────────────────────────────
	// ① ファイルとして存在するか（フォルダを落とされた場合もここで弾く）
	// ────────────────────────────────────────────────────────────────
	if (!fs::exists(source) || !fs::is_regular_file(source)) {
		Log(*logStream_, "Import failed (not a file): " + sourceFilePath + "\n");
		return false;
	}

	// ────────────────────────────────────────────────────────────────
	// ② 扱える種類か（.exe などを落とされても無視する）
	// ────────────────────────────────────────────────────────────────
	const AssetType type = DetectType(source.extension().generic_string());
	if (type == AssetType::Unknown) {
		Log(*logStream_, "Import skipped (unsupported): " + sourceFilePath + "\n");
		return false;
	}

	// ────────────────────────────────────────────────────────────────
	// ③ コピー先を決める（resources/ の直下に、同じファイル名で置く）
	// ────────────────────────────────────────────────────────────────
	const fs::path destination = fs::path(rootDirectory_) / source.filename();

	std::error_code errorCode;

	// すでに resources/ の中のファイルをドラッグした場合は、コピーせず一覧だけ更新
	if (fs::exists(destination) && fs::equivalent(source, destination, errorCode)) {
		Log(*logStream_, "Import skipped (already here): " + destination.generic_string() + "\n");
		Refresh();
		return true;
	}

	// ────────────────────────────────────────────────────────────────
	// ④ コピーする
	//   同名があれば上書き（モデルを作り直して入れ直す運用を想定）
	// ────────────────────────────────────────────────────────────────
	fs::create_directories(rootDirectory_, errorCode);

	if (!fs::copy_file(source, destination, fs::copy_options::overwrite_existing, errorCode)) {
		Log(*logStream_, "Import failed (" + errorCode.message() + "): " + sourceFilePath + "\n");
		return false;
	}

	Log(*logStream_, "Import: " + destination.generic_string() + "\n");

	// ────────────────────────────────────────────────────────────────
	// ⑤ 一覧を作り直す
	// ────────────────────────────────────────────────────────────────
	Refresh();
	return true;
}

// ════════════════════════════════════════════════════════════════════
// パスから番号を探す
// ════════════════════════════════════════════════════════════════════
uint32_t AssetDatabase::FindIndex(const std::string& path) const {

	for (size_t i = 0; i < assets_.size(); ++i) {
		if (assets_[i].path == path) {
			return static_cast<uint32_t>(i);
		}
	}

	return kInvalidIndex;
}

// ════════════════════════════════════════════════════════════════════
// 拡張子から種類を判定する
//   ★新しい形式に対応するときは、ここに1行足すだけ
// ════════════════════════════════════════════════════════════════════
AssetType AssetDatabase::DetectType(const std::string& extension) {

	const std::string lower = ToLower(extension);

	if (lower == ".obj") {
		return AssetType::Model;
	}
	if (lower == ".png" || lower == ".jpg" || lower == ".jpeg" ||
		lower == ".dds" || lower == ".tga") {
		return AssetType::Texture;
	}

	if (lower == ".json") {
		return AssetType::Scene;
	}

	if (lower == ".mtl") {          
		return AssetType::Material;
	}

	if (lower == ".wav" || lower == ".mp3" || lower == ".ogg") {
		return AssetType::Audio;
	}

	return AssetType::Unknown;
}

// ════════════════════════════════════════════════════════════════════
// 種類の表示名
// ════════════════════════════════════════════════════════════════════
const char* AssetDatabase::GetTypeName(AssetType type) {

	switch (type) {
	case AssetType::Model:   return "モデル";
	case AssetType::Texture: return "テクスチャ";
	case AssetType::Scene:   return "シーン";
	case AssetType::Audio:   return "サウンド";
	case AssetType::Material: return "マテリアル";
	default:                 return "不明";
	}
}
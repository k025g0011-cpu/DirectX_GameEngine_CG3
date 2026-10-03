#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

// ════════════════════════════════════════════════════════════════════
// アセットの種類
//   拡張子から判定する
// ════════════════════════════════════════════════════════════════════
enum class AssetType : uint32_t {
	Unknown,
	Model,     // .obj
	Texture,   // .png .jpg .dds .tga
	Scene,     // .json
	Audio,     // .wav .mp3 .ogg
	Material,  // .mtl
};

// ════════════════════════════════════════════════════════════════════
// ImGui のドラッグ＆ドロップで使う小包の名前
//   ドラッグする側と受け取る側が同じ文字列を使う必要があるので、
//   ここで一度だけ定義する（ImGui の制限で 32 文字未満）
// ════════════════════════════════════════════════════════════════════
namespace AssetPayload {
	constexpr const char* kModel = "ASSET_MODEL";
	constexpr const char* kTexture = "ASSET_TEXTURE";
}

// ════════════════════════════════════════════════════════════════════
// 1つのアセットの情報
//   中身は持たない。「どこに何があるか」だけ
// ════════════════════════════════════════════════════════════════════
struct AssetEntry {
	// ルートからの相対パス。区切りは / に統一する
	//   例: "resources/suzanne.obj"
	//   ModelManager::Load() にそのまま渡せる形にしておく
	std::string path;

	// 拡張子を除いた名前。例: "suzanne"
	std::string name;

	// 拡張子（小文字）。例: ".obj"
	std::string extension;

	AssetType type = AssetType::Unknown;
};

// ════════════════════════════════════════════════════════════════════
// アセットの一覧を管理する
//   ・起動時に resources/ を走査して一覧を作る
//   ・外部からドロップされたファイルを resources/ に取り込む
//   ・エディタのアセットパネルはこれを表示する
// ════════════════════════════════════════════════════════════════════
class AssetDatabase {
public:
	/// <summary>見つからなかったときに返る番号</summary>
	static constexpr uint32_t kInvalidIndex = 0xFFFFFFFF;

	/// <summary>初期化。ルートフォルダを走査する</summary>
	/// <param name="rootDirectory">アセットを置くフォルダ（例 "resources"）</param>
	/// <param name="logStream">ログの出力先</param>
	void Initialize(const std::string& rootDirectory, std::ostream& logStream);

	/// <summary>ルートフォルダを走査し直して一覧を作り直す</summary>
	void Refresh();

	/// <summary>外部のファイルをルートフォルダにコピーして登録する</summary>
	/// <param name="sourceFilePath">コピー元の絶対パス（ドロップされたもの）</param>
	/// <returns>取り込めたか</returns>
	bool Import(const std::string& sourceFilePath);

	// ────────────────────────────────────────────────────────────────
	// 一覧の取得
	// ────────────────────────────────────────────────────────────────
	uint32_t GetAssetCount() const { return static_cast<uint32_t>(assets_.size()); }
	const AssetEntry& GetAsset(uint32_t index) const { return assets_[index]; }

	/// <summary>パスから番号を探す。無ければ kInvalidIndex</summary>
	uint32_t FindIndex(const std::string& path) const;

	// ────────────────────────────────────────────────────────────────
	// 種類に関する道具（インスタンスの状態が要らないので static）
	// ────────────────────────────────────────────────────────────────

	/// <summary>拡張子から種類を判定する</summary>
	static AssetType DetectType(const std::string& extension);

	/// <summary>種類の表示名</summary>
	static const char* GetTypeName(AssetType type);

private:
	std::string   rootDirectory_;
	std::ostream* logStream_ = nullptr;

	std::vector<AssetEntry> assets_;
};
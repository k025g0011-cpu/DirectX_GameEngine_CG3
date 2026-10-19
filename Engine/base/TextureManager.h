#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

// 前方宣言
class DirectXCommon;

// ════════════════════════════════════════════════════════════════════
// テクスチャの読み込みと管理
//   パスを渡すと「SRV ヒープの何番に置いたか」が返る
//   同じパスを2回渡しても、読み込みは1回だけ
// ════════════════════════════════════════════════════════════════════
class TextureManager {
public:
	// 初期化
	void Initialize(DirectXCommon* dxCommon);

	/// <summary>テクスチャを読み込む。既に読み込み済みなら再利用する</summary>
	/// <param name="filePath">画像ファイルのパス（例 "resources/uvChecker.png"）</param>
	/// <returns>SRV ヒープ上の番号。描画時にこれを使う</returns>
	uint32_t Load(const std::string& filePath);

	/// <summary>
	/// 指定したテクスチャ（SRV)の GPU ディスクリプタハンドルを取得する
	/// </summary>
	/// <param name="srvIndex">SRVの管理インデックス(テクスチャ番号）</param>
	/// <returns> GPU から直接読み取れる、ディスクリプタヒープ上の引数で受け取った番号の SRV アドレス（住所）</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t srvIndex) const;

	/// <summary>読み込んだテクスチャの枚数（ImGui 表示用）</summary>
	uint32_t GetTextureCount() const { return static_cast<uint32_t>(textures_.size()); }

private:
	// 1枚のテクスチャが持つ情報
	struct TextureData {
		// 読み込み元のパス（同じものを2回読まないための照合用）
		std::string filePath;
		// VRAM 上のテクスチャ本体
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		// SRV ヒープの何番に置いたか
		uint32_t srvIndex = 0;
	};

	/// <summary>　1x1のマゼンタ画像を使って SRV を張る。Initialize から1回だけ呼ぶ　</summary>
	/// <returns>　作った SRV　の番号</returns>
	uint32_t CreateDummyTexture();
	


	// 借りているもの（所有していない）
	DirectXCommon* dxCommon_ = nullptr;

	// 読み込み済みのテクスチャ一覧
	std::vector<TextureData> textures_;

	Microsoft::WRL::ComPtr<ID3D12Resource> dummyResource_;
	uint32_t dummySrvIndex_ = 0;
};
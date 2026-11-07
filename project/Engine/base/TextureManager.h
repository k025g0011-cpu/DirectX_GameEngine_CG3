#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>
#include <optional>

//   解像度を返すのに Vector2 だけ使う。MathUtility.h ごと取り込むと
//   行列まで付いてきて、このヘッダを使う全ファイルに広がる
#include "Vector2.h"

// 前方宣言
class DirectXCommon;

//   DirectXTex.h をここで include すると、TextureManager.h を使う全ファイルに
//   重いヘッダが広がる。参照でしか使わないので前方宣言で足りる
namespace DirectX { class ScratchImage; }

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

	/// <summary>メモリ上の画像を読み込む（.glb の埋め込みテクスチャ用）</summary>
	/// <param name="name">キャッシュのキー。"model.glb#*0" のように一意にする</param>
	/// <param name="data">png / jpg のバイト列の先頭</param>
	/// <param name="size">バイト数</param>
	/// <returns>SRV ヒープ上の番号。描画時にこれを使う</returns>
	uint32_t LoadFromMemory(const std::string& name, const void* data, size_t size);

	/// <summary>
	/// 指定したテクスチャ（SRV)の GPU ディスクリプタハンドルを取得する
	/// </summary>
	/// <param name="srvIndex">SRVの管理インデックス(テクスチャ番号）</param>
	/// <returns> GPU から直接読み取れる、ディスクリプタヒープ上の引数で受け取った番号の SRV アドレス（住所）</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t srvIndex) const;

	/// <summary>読み込んだテクスチャの枚数（ImGui 表示用）</summary>
	uint32_t GetTextureCount() const { return static_cast<uint32_t>(textures_.size()); }

	/// <summary>元画像の解像度（ピクセル）</summary>
	///   ピクセル指定の切り出し範囲を UV（0〜1）に直すのに使う
	/// <param name="srvIndex">Load が返した番号</param>
	/// <returns>見つからなければ 1×1（0 除算を避けるため）</returns>
	Vector2 GetTextureSize(uint32_t srvIndex) const;

private:
	// 1枚のテクスチャが持つ情報
	struct TextureData {
		// 識別用のキー（同じものを2回読まないための照合用）
		//   ファイルなら相対パス、埋め込みなら "model.glb#*0" のような名前
		std::string key;
		// VRAM 上のテクスチャ本体
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		// SRV ヒープの何番に置いたか
		uint32_t srvIndex = 0;

		// 元画像の解像度（ピクセル）。切り出し範囲の計算に使う
		uint32_t width = 1;
		uint32_t height = 1;
	};

	/// <summary>　1x1のマゼンタ画像を使って SRV を張る。Initialize から1回だけ呼ぶ　</summary>
	/// <returns>　作った SRV　の番号</returns>
	uint32_t CreateDummyTexture();
	
	/// <summary>読み込み済みならその番号を返す。無ければ nullopt</summary>
	std::optional<uint32_t> FindCached(const std::string& key) const;

	/// <summary>CPU 上の画像を GPU に載せ、一覧に登録して番号を返す</summary>
	///   Load と LoadFromMemory の「後半」は完全に同じなので、ここにまとめる
	uint32_t RegisterTexture(const std::string& key, const DirectX::ScratchImage& mipImages);

	// 借りているもの（所有していない）
	DirectXCommon* dxCommon_ = nullptr;

	// 読み込み済みのテクスチャ一覧
	std::vector<TextureData> textures_;

	Microsoft::WRL::ComPtr<ID3D12Resource> dummyResource_;
	uint32_t dummySrvIndex_ = 0;
};
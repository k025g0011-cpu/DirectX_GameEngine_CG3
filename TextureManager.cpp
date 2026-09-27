#include "TextureManager.h"

#include "DirectXCommon.h"
#include "Logger.h"

#include <cassert>

// DirectXTex（main.cpp から移動）
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"

using Microsoft::WRL::ComPtr;

// ════════════════════════════════════════════════════════════════════
// このファイルの中だけで使う関数
//   無名名前空間に入れると、他の .cpp からは見えなくなる
// ════════════════════════════════════════════════════════════════════
namespace {

	// ════════════════════════════════════════
	// LoadTexture関数
	// 画像ファイルを読み、ミップマップ付きのデータを CPU 側(RAM)に作る
	// ════════════════════════════════════════
	DirectX::ScratchImage LoadTexture(const std::string& filePath) {
		// テクスチャファイルを読んでプログラムで扱えるようにする
		DirectX::ScratchImage scratchImage{};
		std::wstring filePathW = ConvertString(filePath);
		HRESULT hr = DirectX::LoadFromWICFile(
			filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, scratchImage);
		assert(SUCCEEDED(hr));

		// ミップマップの作成
		DirectX::ScratchImage mipImages{};
		hr = DirectX::GenerateMipMaps(
			scratchImage.GetImages(),
			scratchImage.GetImageCount(),
			scratchImage.GetMetadata(),
			DirectX::TEX_FILTER_SRGB,
			0,
			mipImages);
		assert(SUCCEEDED(hr));

		// ミップマップ付きのデータを返す
		return mipImages;
	}

	// ════════════════════════════════════════
	// CreateTextureResource関数
	// TexMetadata の情報をもとに、GPU 側にテクスチャ用の Resource(空の箱)を作る
	// ※ 中身はまだ空。データの転送は UploadTextureData で行う
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> CreateTextureResource(
		const ComPtr<ID3D12Device>& device, const DirectX::TexMetadata& metadata) {

		// 1. metadata を基に Resource の設定
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Width = UINT(metadata.width);                    // Texture の幅
		resourceDesc.Height = UINT(metadata.height);                   // Texture の高さ
		resourceDesc.MipLevels = UINT16(metadata.mipLevels);              // mipmap の数
		resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);              // 奥行き or 配列 Texture の配列数
		resourceDesc.Format = metadata.format;                         // Texture の Format
		resourceDesc.SampleDesc.Count = 1;                                       // サンプリングカウント。1固定
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Texture の次元数

		// 2. 利用する Heap の設定
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;   // VRAM 上に作る

		// 3. Resource を生成する
		ComPtr<ID3D12Resource> resource = nullptr;
		HRESULT hr = device->CreateCommittedResource(
			&heapProperties,                  // Heap の設定
			D3D12_HEAP_FLAG_NONE,             // Heap の特殊な設定。特になし
			&resourceDesc,                    // Resource の設定
			D3D12_RESOURCE_STATE_COPY_DEST,   // データコピーを受け入れる設定
			nullptr,                          // Clear 最適値。使わないので nullptr
			IID_PPV_ARGS(&resource));         // 作成する Resource ポインタの格納先
		assert(SUCCEEDED(hr));

		return resource;
	}

	// ════════════════════════════════════════
	// UploadTextureData関数
	// 中間リソース(UPLOAD)を経由して、VRAM 上のテクスチャへデータを転送する
	//   戻り値の中間リソースは、転送が完了するまで生かしておく必要がある
	// ════════════════════════════════════════
	[[nodiscard]]
	ComPtr<ID3D12Resource> UploadTextureData(
		const ComPtr<ID3D12Resource>& texture,
		const DirectX::ScratchImage& mipImages,
		DirectXCommon* dxCommon) {

		// 読み込んだ mipImages を、DX12 が読める形式(D3D12_SUBRESOURCE_DATA)に書き換える
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		DirectX::PrepareUpload(
			dxCommon->GetDevice().Get(),
			mipImages.GetImages(),
			mipImages.GetImageCount(),
			mipImages.GetMetadata(),
			subresources);

		// 格納に必要なサイズを測り、その大きさで中間リソース(UPLOAD)を作る
		uint64_t intermediateSize =
			GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));
		ComPtr<ID3D12Resource> intermediateResource = dxCommon->CreateBufferResource(intermediateSize);

		// 「指定したコピー先へテクスチャデータをコピーしろ」という命令を積む
		UpdateSubresources(
			dxCommon->GetCommandList().Get(),   // ① コピー命令を積む先
			texture.Get(),                      // ② コピー先（VRAM）
			intermediateResource.Get(),         // ③ 中継地点（UPLOAD）
			0,                                  // ④ 中間リソースの何バイト目から使うか
			0,                                  // ⑤ テクスチャの何番目のサブリソースから
			UINT(subresources.size()),          // ⑥ 何個分
			subresources.data());               // ⑦ PrepareUpload で作った案内板の先頭

		// 転送後は読めるよう、COPY_DEST → GENERIC_READ へ状態を遷移させる
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = texture.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;     // コピー受け入れのリソースから
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;  // 読むリソースへ
		dxCommon->GetCommandList()->ResourceBarrier(1, &barrier);

		return intermediateResource;
	}

}   // namespace


// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void TextureManager::Initialize(DirectXCommon* dxCommon) {
	assert(dxCommon != nullptr);
	dxCommon_ = dxCommon;
}

// ════════════════════════════════════════════════════════════════════
// テクスチャを読み込んで、SRV ヒープ上の番号を返す
//   ※ 中で ExecuteAndWait するので、描画コマンドを積んでいる最中は呼ばないこと
// ════════════════════════════════════════════════════════════════════
uint32_t TextureManager::Load(const std::string& filePath) {

	// ────────────────────────────────────────────────────────────────
	// ① 既に読み込み済みなら、その番号をそのまま返す
	// ────────────────────────────────────────────────────────────────
	for (const TextureData& texture : textures_) {
		if (texture.filePath == filePath) {
			return texture.srvIndex;
		}
	}

	// ────────────────────────────────────────────────────────────────
	// ② 画像ファイルを読み込む（ミップマップ付きで CPU 側に用意される）
	// ────────────────────────────────────────────────────────────────
	DirectX::ScratchImage mipImages = LoadTexture(filePath);
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	// ────────────────────────────────────────────────────────────────
	// ③ VRAM 上にテクスチャ本体（中身は空の箱）を作る
	// ────────────────────────────────────────────────────────────────
	ComPtr<ID3D12Resource> resource = CreateTextureResource(dxCommon_->GetDevice(), metadata);

	// ────────────────────────────────────────────────────────────────
	// ④ 中間リソース経由で VRAM へ転送するコマンドを積む
	// ────────────────────────────────────────────────────────────────
	ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(resource, mipImages, dxCommon_);

	// ────────────────────────────────────────────────────────────────
	// ⑤ 転送を実行して完了を待つ
	//     待ち終われば中間リソースは用済み。この関数を抜けるとき自動で解放される
	// ────────────────────────────────────────────────────────────────
	dxCommon_->ExecuteAndWait();

	// ────────────────────────────────────────────────────────────────
	// ⑥ SRV を置く場所を1つ確保する
	// ────────────────────────────────────────────────────────────────
	uint32_t srvIndex = dxCommon_->AllocateSrv();

	// ────────────────────────────────────────────────────────────────
	// ⑦ SRV の設定を作って、確保した場所に書き込む
	// ────────────────────────────────────────────────────────────────
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	dxCommon_->GetDevice()->CreateShaderResourceView(
		resource.Get(), &srvDesc, dxCommon_->GetSrvCpuDescriptorHandle(srvIndex));

	// ────────────────────────────────────────────────────────────────
	// ⑧ 一覧に登録する（resource を保持しないと VRAM から消えてしまう）
	// ────────────────────────────────────────────────────────────────
	TextureData textureData{};
	textureData.filePath = filePath;
	textureData.resource = resource;
	textureData.srvIndex = srvIndex;
	textures_.push_back(textureData);

	Log(dxCommon_->GetLogStream(), "Load texture: " + filePath + "\n");

	return srvIndex;
}

// ════════════════════════════════════════════════════════════════════
// SRV 番号から GPU ハンドルを得る
// ════════════════════════════════════════════════════════════════════
D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvGpuHandle(uint32_t srvIndex) const {
	return dxCommon_->GetSrvGpuDescriptorHandle(srvIndex);
}
#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <string>
#include <dxgi1_6.h>
#include <ostream>
#include <wrl.h>

// 前方宣言（ポインタとしてしか使わないので #include 不要）
class WinApp;

// ════════════════════════════════════════════════════════════════════
// DirectX12 の基盤をまとめたクラス
//   デバイス・コマンド・スワップチェーン・各種ヒープ・毎フレーム処理
// ════════════════════════════════════════════════════════════════════
class DirectXCommon {
public:
	// バックバッファの数
	static const uint32_t kBackBufferCount = 2;

	// SRV ディスクリプタヒープのサイズ
	static const uint32_t kMaxSrvCount = 128;   

	// 初期化
	void Initialize(WinApp* winApp, std::ostream& logStream);

	// 後片付け
	void Finalize();

	// 描画前処理（バリア・クリア・ビューポート設定まで）
	void PreDraw();

	// 描画後処理（バリア・コマンド実行・Present・フェンス待ち）
	void PostDraw();


	// 積んだコマンドを実行し、GPU の完了を待つ
	//   テクスチャ転送など、描画ループの外で使う単発処理用
	void ExecuteAndWait();       

	// 定数バッファ・頂点バッファなどの Buffer リソースを作る
	// UPLOAD ヒープなので、作った後 Map して CPU から書き込める
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);


	// hlsl をコンパイルして、GPU が実行できるバイナリ（Blob）を返す
	//   filePath : コンパイル対象の .hlsl
	//   profile  : シェーダープロファイル（L"vs_6_0" / L"ps_6_0" など）
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath, const wchar_t* profile);

	// ────────────────────────────────────────────────────────────────
	// 取得系
	//   ComPtr の const 参照を返す。生ポインタが欲しければ呼ぶ側で .Get()
	// ────────────────────────────────────────────────────────────────
	const Microsoft::WRL::ComPtr<ID3D12Device>& GetDevice() const { return device_; }
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& GetCommandList() const { return commandList_; }
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& GetSrvDescriptorHeap() const { return srvDescriptorHeap_; }

	const Microsoft::WRL::ComPtr<IDxcUtils>& GetDxcUtils() const { return dxcUtils_; }
	const Microsoft::WRL::ComPtr<IDxcCompiler3>& GetDxcCompiler() const { return dxcCompiler_; }
	const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& GetIncludeHandler() const { return includeHandler_; }

	std::ostream& GetLogStream() const { return *logStream_; }


	// ────────────────────────────────────────────────────────────────
	// 任意のディスクリプタヒープから index 番目のハンドルを取得する（汎用）
	//   ヒープとサイズを引数でもらうので、インスタンスの状態は不要 → static
	// ────────────────────────────────────────────────────────────────
	static D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(
		const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
		uint32_t descriptorSize, uint32_t index);

	static D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(
		const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
		uint32_t descriptorSize, uint32_t index);


	// SRV ヒープの index 番目のハンドルを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE GetSrvCpuDescriptorHandle(uint32_t index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuDescriptorHandle(uint32_t index) const;


	// SRV ヒープの空き番号を1つ確保して返す
	//   テクスチャや ImGui は、この番号を使って SRV を作る
	uint32_t AllocateSrv();

private:
	// ────────────────────────────────────────────────────────────────
	// 初期化の内訳
	//   順序が厳密なので、外からは呼べないよう private にする
	// ────────────────────────────────────────────────────────────────
	void InitializeDevice();             // F-2
	void InitializeCommand();            // F-3
	void InitializeSwapChain();          // F-4
	void InitializeRenderTargetView();   // F-4
	void InitializeDepthStencilView();   // F-5
	void InitializeSrvHeap();            // F-5
	void InitializeDxcCompiler();        // F-5
	void InitializeViewport();           // F-5

	// GPU の処理完了を待つ
	void WaitForGPU();

	// ディスクリプタヒープを生成する
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

	// ────────────────────────────────────────────────────────────────
	// 借りているもの（所有していない）
	// ────────────────────────────────────────────────────────────────
	WinApp* winApp_ = nullptr;
	std::ostream* logStream_ = nullptr;

	// ────────────────────────────────────────────────────────────────
	// デバイス関連
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
	Microsoft::WRL::ComPtr<ID3D12Device>  device_;

	// ────────────────────────────────────────────────────────────────
	// コマンド関連
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12CommandQueue>        commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;

	// ────────────────────────────────────────────────────────────────
	// 同期（GPU の処理完了を待つ仕組み）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	HANDLE   fenceEvent_ = nullptr;

	// ────────────────────────────────────────────────────────────────
	// スワップチェーンと描画先
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12Resource>  swapChainResources_[kBackBufferCount];

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kBackBufferCount]{};

	// ────────────────────────────────────────────────────────────────
	// 深度バッファ
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource>       depthStencilResource_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

	// ────────────────────────────────────────────────────────────────
	// SRV（テクスチャと ImGui が使う）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;

	// ディスクリプタ1個あたりのバイト数（GPU ごとに違う）
	uint32_t descriptorSizeRTV_ = 0;
	uint32_t descriptorSizeDSV_ = 0;
	uint32_t descriptorSizeSRV_ = 0;

	// 次に配る SRV の番号（配るたびに増える）
	uint32_t srvUseIndex_ = 0;

	// ────────────────────────────────────────────────────────────────
	// シェーダーコンパイラ
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<IDxcUtils>          dxcUtils_;
	Microsoft::WRL::ComPtr<IDxcCompiler3>      dxcCompiler_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;

	// ────────────────────────────────────────────────────────────────
	// 描画範囲
	// ────────────────────────────────────────────────────────────────
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT     scissorRect_{};

	// ────────────────────────────────────────────────────────────────
	// PreDraw と PostDraw で共有する一時データ
	// ────────────────────────────────────────────────────────────────
	D3D12_RESOURCE_BARRIER barrier_{};
	UINT backBufferIndex_ = 0;
};
#include "DirectXCommon.h"
#include "Logger.h"

#include "WinApp.h"

#include <cassert>
#include <format>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

// ════════════════════════════════════════════════════════════════════
// デストラクタ
//   Finalize() の呼び忘れに対する保険
//   ComPtr のメンバは何もしなくても自動で解放される
// ════════════════════════════════════════════════════════════════════
DirectXCommon::~DirectXCommon() {
	CloseFenceEvent();
}

// ════════════════════════════════════════════════════════════════════
// 後片付け
//   解放の順序を自分で制御したいとき用。呼ばなくてもデストラクタが拾う
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::Finalize() {
	CloseFenceEvent();
}

// ════════════════════════════════════════════════════════════════════
// フェンス用イベントを閉じる
//   閉じたら nullptr にするので、何度呼んでも安全
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::CloseFenceEvent() {

	if (fenceEvent_ == nullptr) {
		return;
	}

	CloseHandle(fenceEvent_);
	fenceEvent_ = nullptr;   // これを忘れると二重 Close になる
}

// ════════════════════════════════════════════════════════════════════
// 初期化
//   順序が厳密。上から順に依存している
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::Initialize(WinApp* winApp, std::ostream& logStream) {
	assert(winApp != nullptr);

	winApp_ = winApp;
	logStream_ = &logStream;

	InitializeDevice();           // F-2
	InitializeCommand();          // F-3
	InitializeSwapChain();        // F-4
	InitializeRenderTargetView(); // F-5
	InitializeDepthStencilView(); // F-6
	InitializeSrvHeap();          // F-7
	InitializeDxcCompiler();      // F-8
	InitializeViewport();         // F-9
}

// ════════════════════════════════════════════════════════════════════
// デバイスの生成
//   「このプログラムが使う GPU」を1つ決めて、操作用の窓口を作る
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::InitializeDevice() {
	HRESULT hr{};

	// ────────────────────────────────────────────────────────────────
	// ① デバッグレイヤーの有効化
	//     デバイス生成より前でないと効かない
	// ────────────────────────────────────────────────────────────────
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効にする
		debugController->EnableDebugLayer();
		// GPU 側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	// ────────────────────────────────────────────────────────────────
	// ② DXGIFactory の生成
	//     GPU の一覧を取得したり、スワップチェーンを作ったりする「窓口」
	// ────────────────────────────────────────────────────────────────
	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────────────────────────────
	// ③ 使用するアダプタ（GPU）を決める
	//     性能の高い順に見ていき、最初に見つかった「本物の GPU」を採用
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;

	for (uint32_t i = 0;
		dxgiFactory_->EnumAdapterByGpuPreference(
			i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND;
		++i) {

		// アダプタの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// ソフトウェアアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			// 採用したアダプタの情報をログに出力（wstring なので注意）
			Log(*logStream_, ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));
			break;
		}

		// ソフトウェアアダプタだったら見なかったことにする
		useAdapter = nullptr;
	}

	// 適切なアダプタが見つからなかったら起動できない
	assert(useAdapter != nullptr);

	// ────────────────────────────────────────────────────────────────
	// ④ D3D12Device の生成
	//     新しい機能レベルから順に試し、生成できたものを採用する
	// ────────────────────────────────────────────────────────────────
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
	};
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };

	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		// 採用したアダプタでデバイスを生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));

		// 指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr)) {
			// 生成できたのでログ出力を行ってループを抜ける
			Log(*logStream_, std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスの生成がうまくいかなかったので起動できない
	assert(device_ != nullptr);
	Log(*logStream_, "Complete create D3D12Device!!!\n");

	// ────────────────────────────────────────────────────────────────
	// ⑤ エラー・警告時に止める設定 & 誤検知の抑制
	// ────────────────────────────────────────────────────────────────
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// やばいエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// 警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// 抑制するメッセージの ID
		D3D12_MESSAGE_ID denyIds[] = {
			// Windows11 での DXGI デバッグレイヤーと DX12 デバッグレイヤーの相互作用バグによるエラーメッセージ
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE };

		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };

		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
	}
#endif

	// ────────────────────────────────────────────────────────────────
	// ⑥ ディスクリプタ1個あたりのバイト数を取得しておく
	//     device ごとに固有で、実行中に変わらないのでここで1回だけ取る
	// ────────────────────────────────────────────────────────────────
	descriptorSizeRTV_ = device_->GetDescriptorHandleIncrementSize(
	    D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV_ = device_->GetDescriptorHandleIncrementSize(
	    D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(
	    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}


// ════════════════════════════════════════════════════════════════════
// コマンド関連の生成
//   Queue（提出口）/ Allocator（用紙）/ List（ペン）/ Fence（番号札）
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::InitializeCommand() {
	HRESULT hr{};

	// ────────────────────────────────────────────────────────────────
	// ① コマンドキュー
	//     GPU にコマンドリストを提出する窓口
	// ────────────────────────────────────────────────────────────────
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────────────────────────────
	// ② コマンドアロケータ
	//     コマンドを書き込むメモリ領域の管理者
	// ────────────────────────────────────────────────────────────────
	hr = device_->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────────────────────────────
	// ③ コマンドリスト
	//     アロケータのメモリにコマンドを書き込むためのインターフェース
	// ────────────────────────────────────────────────────────────────
	hr = device_->CreateCommandList(
		0,                                // ノードマスク（GPU が1個なら 0）
		D3D12_COMMAND_LIST_TYPE_DIRECT,   // アロケータと同じ種類でなければならない
		commandAllocator_.Get(),          // 書き込み先のアロケータ
		nullptr,                          // 初期 PSO（後で設定するので nullptr）
		IID_PPV_ARGS(&commandList_));
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────────────────────────────
	// ④ フェンス
	//     GPU の処理完了を CPU が知るための「番号札」
	// ────────────────────────────────────────────────────────────────
	fenceValue_ = 0;
	hr = device_->CreateFence(
		fenceValue_,                 // 初期値
		D3D12_FENCE_FLAG_NONE,
		IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────────────────────────────
	// ⑤ フェンス用のイベント
	//     「値が変わったら教えて」を受け取る Windows のイベントオブジェクト
	// ────────────────────────────────────────────────────────────────
	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent_ != nullptr);
}

// ════════════════════════════════════════════════════════════════════
// スワップチェーンの生成
//   画面に表示するためのバッファを kBackBufferCount 枚用意する
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::InitializeSwapChain() {
	HRESULT hr{};

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = WinApp::kClientWidth;   // 画面の幅
	swapChainDesc.Height = WinApp::kClientHeight;  // 画面の高さ
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // 色の形式
	swapChainDesc.SampleDesc.Count = 1;                 // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;  // 描画先として使う
	swapChainDesc.BufferCount = kBackBufferCount;       // バッファ数
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // 表示後は中身を破棄

	// スワップチェーンを生成する
	//   IDXGISwapChain1 で受け取ってから IDXGISwapChain4 に変換する
	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1 = nullptr;
	hr = dxgiFactory_->CreateSwapChainForHwnd(
		commandQueue_.Get(),     // コマンドキュー（Present がここに積まれる）
		winApp_->GetHwnd(),      // 表示先のウィンドウ
		&swapChainDesc,          // 設定
		nullptr,                 // フルスクリーン設定（使わない）
		nullptr,                 // 出力先の制限（使わない）
		&swapChain1);
	assert(SUCCEEDED(hr));

	// IDXGISwapChain4 を得る
	hr = swapChain1->QueryInterface(IID_PPV_ARGS(&swapChain_));
	assert(SUCCEEDED(hr));
}

// ════════════════════════════════════════════════════════════════════
// レンダーターゲットビューの生成
//   「このバッファを描画先として使う」という説明書をヒープに並べる
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::InitializeRenderTargetView() {
	HRESULT hr{};

	// ────────────────────────────────────────────────────────────────
	// ① RTV 用のディスクリプタヒープ（棚）を作る
	//     RTV は Shader 内で触らないので ShaderVisible は false
	//     オフスクリーン描画先の分も入るよう kMaxRtvCount で作る
	// ────────────────────────────────────────────────────────────────
	rtvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kMaxRtvCount, false);


	// ────────────────────────────────────────────────────────────────
	// ② スワップチェーンから各バッファを取り出す
	// ────────────────────────────────────────────────────────────────
	for (uint32_t i = 0; i < kBackBufferCount; ++i) {
		hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
		assert(SUCCEEDED(hr));

		swapChainResources_[i]->SetName(L"SwapChainBuffer");
	}

	// ────────────────────────────────────────────────────────────────
	// ③ RTV の設定
	// ────────────────────────────────────────────────────────────────
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // sRGB として書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;    // 2Dテクスチャとして扱う

	// ────────────────────────────────────────────────────────────────
	// ④ 配布係から番号をもらって RTV を作る
	//     0, 1 が返る。以降の番号はオフスクリーン描画先が使う
	// ────────────────────────────────────────────────────────────────
	for (uint32_t i = 0; i < kBackBufferCount; ++i) {
		uint32_t rtvIndex = AllocateRtv();
		rtvHandles_[i] = GetRtvCpuDescriptorHandle(rtvIndex);
		device_->CreateRenderTargetView(
			swapChainResources_[i].Get(), &rtvDesc, rtvHandles_[i]);
	}

}


// ════════════════════════════════════════════════════════════════════
// 深度ステンシルビューの生成
//   ピクセルごとの「カメラからの距離」を記録するバッファ
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::InitializeDepthStencilView() {
	HRESULT hr{};

	// ────────────────────────────────────────────────────────────────
	// ① 深度バッファ本体（リソース）を作る
	// ────────────────────────────────────────────────────────────────
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = WinApp::kClientWidth;   // 画面と同じ幅
	resourceDesc.Height = WinApp::kClientHeight;  // 画面と同じ高さ
	resourceDesc.MipLevels = 1;                      // ミップマップは不要
	resourceDesc.DepthOrArraySize = 1;                      // 奥行き1
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;  // 深度24bit + ステンシル8bit
	resourceDesc.SampleDesc.Count = 1;                      // マルチサンプルしない
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;  // 2Dテクスチャ
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;  // 深度として使う

	// VRAM 上に置く
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// クリア時の最適化された値
	//   ここで指定した値と ClearDepthStencilView の値が一致すると高速になる
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;                        // 最大値（一番遠い）
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	hr = device_->CreateCommittedResource(
		&heapProperties,                          // ヒープの設定
		D3D12_HEAP_FLAG_NONE,                     // ヒープの特殊設定
		&resourceDesc,                            // リソースの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE,         // 深度書き込み状態で開始
		&depthClearValue,                         // クリア最適値
		IID_PPV_ARGS(&depthStencilResource_));
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────────────────────────────
	// ② DSV 用のディスクリプタヒープを作る
	//     オフスクリーン用の深度バッファの分も入るよう kMaxDsvCount で作る
	// ────────────────────────────────────────────────────────────────
	dsvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kMaxDsvCount, false);


	// ────────────────────────────────────────────────────────────────
	// ③ 配布係から番号をもらって DSV を作る
	// ────────────────────────────────────────────────────────────────
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	depthStencilDsvIndex_ = AllocateDsv();   //  0 が返る

	device_->CreateDepthStencilView(
		depthStencilResource_.Get(),
		&dsvDesc,
		GetDsvCpuDescriptorHandle(depthStencilDsvIndex_));
}


// ════════════════════════════════════════════════════════════════════
// SRV 用ディスクリプタヒープの生成
//   テクスチャと ImGui が使う。シェーダーから見える必要がある
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::InitializeSrvHeap() {

	srvDescriptorHeap_ = CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSrvCount, true);

}

// ════════════════════════════════════════════════════════════════════
// DXC（シェーダーコンパイラ）の初期化
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::InitializeDxcCompiler() {
	HRESULT hr{};

	// dxcCompiler を初期化
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr));

	// #include に対応するための設定（今は使っていないが必要）
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));
}

// ════════════════════════════════════════════════════════════════════
// ビューポートとシザー矩形の設定
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::InitializeViewport() {
	// ビューポート
	//   クライアント領域のサイズと同じにして画面全体に表示
	viewport_.Width = static_cast<FLOAT>(WinApp::kClientWidth);
	viewport_.Height = static_cast<FLOAT>(WinApp::kClientHeight);
	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	// シザー矩形
	//   基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect_.left = 0;
	scissorRect_.right = WinApp::kClientWidth;
	scissorRect_.top = 0;
	scissorRect_.bottom = WinApp::kClientHeight;
}

// ════════════════════════════════════════════════════════════════════
// 描画先をスワップチェーンに設定する
//   PreDraw から呼ばれるほか、オフスクリーン描画のあとに戻すためにも呼ぶ
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::SetSwapChainRenderTarget() {

	// 深度バッファの DSV ハンドル
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		GetCpuDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV_, 0);

	// 描画先（RTV と DSV）を設定する
	commandList_->OMSetRenderTargets(
		1,                              // レンダーターゲットの数
		&rtvHandles_[backBufferIndex_], // レンダーターゲットのハンドル
		false,                          // 連続していないので false
		&dsvHandle);                    // 深度バッファのハンドル

	// ビューポートとシザー矩形を設定する
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);
}



// ════════════════════════════════════════════════════════════════════
// 描画前処理
//   バックバッファを描画可能な状態にして、クリアし、設定を整える
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::PreDraw() {

	// ────────────────────────────────────────────────────────────────
	// ① これから書き込むバックバッファのインデックスを取得
	// ────────────────────────────────────────────────────────────────
	backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// ────────────────────────────────────────────────────────────────
	// ② バリア：表示状態 → 描画対象状態
	// ────────────────────────────────────────────────────────────────
	barrier_.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier_.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier_.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
	barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	commandList_->ResourceBarrier(1, &barrier_);

	// ────────────────────────────────────────────────────────────────
	// ③ 描画先・ビューポート・シザー矩形を設定する
	// ────────────────────────────────────────────────────────────────
	SetSwapChainRenderTarget();

	// ────────────────────────────────────────────────────────────────
	// ④ 画面をクリアする
	// ────────────────────────────────────────────────────────────────
	float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.6f };
	commandList_->ClearRenderTargetView(
		rtvHandles_[backBufferIndex_], clearColor, 0, nullptr);

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		GetDsvCpuDescriptorHandle(depthStencilDsvIndex_);
	commandList_->ClearDepthStencilView(
		dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// ────────────────────────────────────────────────────────────────
	// ⑤ 使用するディスクリプタヒープを設定する
	// ────────────────────────────────────────────────────────────────
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
	commandList_->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
}



// ════════════════════════════════════════════════════════════════════
// 描画後処理
//   描画を確定し、画面に表示し、GPU の完了を待って次フレームに備える
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::PostDraw() {
	HRESULT hr{};

	// ────────────────────────────────────────────────────────────────
	// ① バリア：描画対象状態 → 表示状態
	//     PreDraw で作った barrier_ の Before / After を入れ替えるだけ
	// ────────────────────────────────────────────────────────────────
	barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList_->ResourceBarrier(1, &barrier_);

	// ────────────────────────────────────────────────────────────────
	// ② コマンドリストを閉じる（これ以降は書き込めない）
	// ────────────────────────────────────────────────────────────────
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────────────────────────────
	// ③ GPU にコマンドリストの実行を行わせる
	// ────────────────────────────────────────────────────────────────
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(_countof(commandLists), commandLists);

	// ────────────────────────────────────────────────────────────────
	// ④ GPU とモニタに画面の交換を行うよう通知する
	// ────────────────────────────────────────────────────────────────
	swapChain_->Present(1, 0);

	// ────────────────────────────────────────────────────────────────
	// ⑤ GPU の処理完了を待つ
	// ────────────────────────────────────────────────────────────────
	WaitForGPU();

	// ────────────────────────────────────────────────────────────────
	// ⑥ 次のフレーム用にコマンドリストを準備する
	//     GPU が読み終わったのでアロケータを白紙に戻せる
	// ────────────────────────────────────────────────────────────────
	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));

	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

// ════════════════════════════════════════════════════════════════════
// 積んだコマンドを実行し、GPU の完了を待つ
//   描画ループの外で使う（テクスチャ転送など）
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::ExecuteAndWait() {
	HRESULT hr{};

	// コマンドリストの内容を確定させる
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// GPU にコマンドリストの実行を行わせる
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(_countof(commandLists), commandLists);

	// 完了を待つ
	WaitForGPU();

	// 次に備えてリセット
	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

// ════════════════════════════════════════════════════════════════════
// GPU の処理完了を待つ
// ════════════════════════════════════════════════════════════════════
void DirectXCommon::WaitForGPU() {
	// フェンスの値を更新
	++fenceValue_;
	// GPU がここまでたどり着いたときに、フェンスの値を代入するよう Signal を送る
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	// まだ達していないなら、達するまで待つ
	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}
}


// ════════════════════════════════════════════════════════════════════
// Buffer リソースの生成
//   VertexBuffer や ConstantBuffer など、CPU から書き込むバッファを作る
// ════════════════════════════════════════════════════════════════════
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes) {

	// UPLOAD ヒープ（CPU から書けて GPU からも読める領域）
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// リソースの設定
	D3D12_RESOURCE_DESC bufferResourceDesc{};
	// バッファリソース。テクスチャの場合はまた別の設定をする
	bufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferResourceDesc.Width = sizeInBytes;
	// バッファの場合はこれらは1にする決まり
	bufferResourceDesc.Height = 1;
	bufferResourceDesc.DepthOrArraySize = 1;
	bufferResourceDesc.MipLevels = 1;
	bufferResourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	bufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// 実際にリソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferResourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&bufferResource));
	assert(SUCCEEDED(hr));

	return bufferResource;
}

// ════════════════════════════════════════════════════════════════════
// ディスクリプタヒープの生成
//   RTV / DSV / SRV で共通。ShaderVisible だけが違う
// ════════════════════════════════════════════════════════════════════
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(
	D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;

	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible
		? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		: D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	HRESULT hr = device_->CreateDescriptorHeap(
		&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));

	return descriptorHeap;
}

// ════════════════════════════════════════════════════════════════════
// 任意のヒープの index 番目のハンドルを取得（汎用）
// ════════════════════════════════════════════════════════════════════
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCpuDescriptorHandle(
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
	uint32_t descriptorSize, uint32_t index) {

	D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<SIZE_T>(descriptorSize) * index;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGpuDescriptorHandle(
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
	uint32_t descriptorSize, uint32_t index) {

	D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += static_cast<UINT64>(descriptorSize) * index;
	return handle;
}

// ════════════════════════════════════════════════════════════════════
// SRV / RTV / DSV ヒープ専用の省略版
//   ヒープとサイズは自分が持っているので、呼ぶ側は index だけでよい
// ════════════════════════════════════════════════════════════════════
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetSrvCpuDescriptorHandle(uint32_t index) const {
	return GetCpuDescriptorHandle(srvDescriptorHeap_, descriptorSizeSRV_, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetSrvGpuDescriptorHandle(uint32_t index) const {
	return GetGpuDescriptorHandle(srvDescriptorHeap_, descriptorSizeSRV_, index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetRtvCpuDescriptorHandle(uint32_t index) const {
	return GetCpuDescriptorHandle(rtvDescriptorHeap_, descriptorSizeRTV_, index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetDsvCpuDescriptorHandle(uint32_t index) const {
	return GetCpuDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV_, index);
}

// ════════════════════════════════════════════════════════════════════
// SRV ヒープの空き番号を1つ確保して返す
//   「次は何番？」を人間が覚えなくてよくするための係
// ════════════════════════════════════════════════════════════════════
uint32_t DirectXCommon::AllocateSrv() {
	// 上限チェック。超えたら他のディスクリプタを壊すので止める
	assert(srvUseIndex_ < kMaxSrvCount);

	// 今の番号を返して、次に備えて1つ進める
	uint32_t index = srvUseIndex_;
	++srvUseIndex_;
	return index;
}

// ════════════════════════════════════════════════════════════════════
// RTV ヒープの空き番号を1つ確保して返す
// ════════════════════════════════════════════════════════════════════
uint32_t DirectXCommon::AllocateRtv() {
	assert(rtvUseIndex_ < kMaxRtvCount);

	uint32_t index = rtvUseIndex_;
	++rtvUseIndex_;
	return index;
}

// ════════════════════════════════════════════════════════════════════
// DSV ヒープの空き番号を1つ確保して返す
// ════════════════════════════════════════════════════════════════════
uint32_t DirectXCommon::AllocateDsv() {
	assert(dsvUseIndex_ < kMaxDsvCount);

	uint32_t index = dsvUseIndex_;
	++dsvUseIndex_;
	return index;
}

// ════════════════════════════════════════════════════════════════════
// シェーダーのコンパイル
//   hlsl ファイルを読み、DXC でコンパイルし、GPU が実行できるバイナリを返す
// ════════════════════════════════════════════════════════════════════
Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(
	const std::wstring& filePath, const wchar_t* profile) {

	// ────────────────────────────────────────────────────────────────
	// 1. hlsl ファイルを読む
	// ────────────────────────────────────────────────────────────────
	// これからシェーダーをコンパイルする旨をログに出す
	Log(*logStream_, std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile));

	// hlsl ファイルを読む
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	// 読めなかったら止める
	assert(SUCCEEDED(hr));

	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;   // UTF8 の文字コードであることを通知

	// ────────────────────────────────────────────────────────────────
	// 2. コンパイルする
	// ────────────────────────────────────────────────────────────────
	LPCWSTR arguments[] = {
		filePath.c_str(),           // コンパイル対象の hlsl ファイル名
		L"-E", L"main",             // エントリーポイントの指定
		L"-T", profile,             // ShaderProfile の設定
		L"-Zi", L"-Qembed_debug",   // デバッグ用の情報を埋め込む
		L"-Od",                     // 最適化を外しておく
		L"-Zpr",                    // メモリレイアウトは行優先
	};

	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler_->Compile(
		&shaderSourceBuffer,         // 読み込んだファイル
		arguments,                   // コンパイルオプション
		_countof(arguments),         // コンパイルオプションの数
		includeHandler_.Get(),       // include が含まれた諸々
		IID_PPV_ARGS(&shaderResult));
	// コンパイルエラーではなく dxc が起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────────────────────────────
	// 3. 警告・エラーが出ていないか確認する
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(*logStream_, shaderError->GetStringPointer());
		// 警告・エラーダメゼッタイ
		assert(false);
	}

	// ────────────────────────────────────────────────────────────────
	// 4. コンパイル結果を受け取って返す
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	// 成功したログを出す
	Log(*logStream_, std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile));

	// shaderSource / shaderResult / shaderError は ComPtr が自動解放する
	return shaderBlob;
}
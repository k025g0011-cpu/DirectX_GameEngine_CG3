#include "RenderTexture.h"

#include "DirectXCommon.h"

#include "Logger.h"
#include <format>
#include <cassert>

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void RenderTexture::Initialize(DirectXCommon* dxCommon, const Desc& desc) {
	assert(dxCommon != nullptr);
	assert(desc.width > 0 && desc.height > 0);

	dxCommon_ = dxCommon;
	desc_ = desc;

	// カラー（RTV + SRV）
	CreateColorResource();

	// 深度（必要なときだけ）
	if (desc_.useDepth) {
		CreateDepthResource();
	}

	// ────────────────────────────────────────────────────────────────
	// 描画範囲を1回だけ作る
	//   解像度は実行中変わらないので、毎フレーム組み立てる必要はない
	// ────────────────────────────────────────────────────────────────
	viewport_.Width = static_cast<FLOAT>(desc_.width);
	viewport_.Height = static_cast<FLOAT>(desc_.height);
	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	scissorRect_.left = 0;
	scissorRect_.right = static_cast<LONG>(desc_.width);
	scissorRect_.top = 0;
	scissorRect_.bottom = static_cast<LONG>(desc_.height);

	// ────────────────────────────────────────────────────────────────
// 確保した番号をログに出す
//   既存のディスクリプタを奪っていないかを目で確認できるようにする
// ────────────────────────────────────────────────────────────────
	Log(dxCommon_->GetLogStream(), std::format(
		"RenderTexture created : {}x{}  rtv={}  dsv={}  srv={}\n",
		desc_.width, desc_.height, rtvIndex_, dsvIndex_, srvIndex_));
}

// ════════════════════════════════════════════════════════════════════
// カラーのリソースを作り、RTV と SRV の両方を張る
//   このクラスの中心。1つのリソースを2通りの見方で登録する
// ════════════════════════════════════════════════════════════════════
void RenderTexture::CreateColorResource() {

	// ────────────────────────────────────────────────────────────────
	// ① リソースの設定
	// ────────────────────────────────────────────────────────────────
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = desc_.width;
	resourceDesc.Height = desc_.height;
	resourceDesc.DepthOrArraySize = 1;                 // 配列ではない
	resourceDesc.MipLevels = 1;                 // 毎フレーム描き直すのでミップ不要
	resourceDesc.Format = desc_.format;
	resourceDesc.SampleDesc.Count = 1;                 // マルチサンプルしない

	// 「RTV を張ってよい」フラグ。
	//   DENY_SHADER_RESOURCE を立てていないので SRV も張れる
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	// ────────────────────────────────────────────────────────────────
	// ② VRAM 上に置く
	// ────────────────────────────────────────────────────────────────
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// ────────────────────────────────────────────────────────────────
	// ③ クリア最適値
	//   ※ ここで指定した色と ClearRenderTargetView に渡す色が
	//      一致していないと、デバッグレイヤーが警告を出す。
	//      このプロジェクトは警告でブレークする設定なので、
	//      クリア色は生成後に変更できない（SetClearColor を作らない理由）
	// ────────────────────────────────────────────────────────────────
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = desc_.format;
	clearValue.Color[0] = desc_.clearColor.x;
	clearValue.Color[1] = desc_.clearColor.y;
	clearValue.Color[2] = desc_.clearColor.z;
	clearValue.Color[3] = desc_.clearColor.w;

	// ────────────────────────────────────────────────────────────────
	// ④ リソースを作る
	// ────────────────────────────────────────────────────────────────
	HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
		&heapProperties,                       // ヒープの設定
		D3D12_HEAP_FLAG_NONE,                  // ヒープの特殊設定
		&resourceDesc,                         // リソースの設定
		D3D12_RESOURCE_STATE_RENDER_TARGET,    // 描画先状態で開始する
		&clearValue,                           // クリア最適値
		IID_PPV_ARGS(&colorResource_));
	assert(SUCCEEDED(hr));

	colorResource_->SetName(L"RenderTexture_Color");


	// 開始状態をメンバにも記録しておく（バリアの StateBefore に使う）
	currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;

	// ────────────────────────────────────────────────────────────────
	// ⑤ RTV（書く用のビュー）を張る
	// ────────────────────────────────────────────────────────────────
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = desc_.format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	rtvIndex_ = dxCommon_->AllocateRtv();
	dxCommon_->GetDevice()->CreateRenderTargetView(
		colorResource_.Get(),
		&rtvDesc,
		dxCommon_->GetRtvCpuDescriptorHandle(rtvIndex_));

	// ────────────────────────────────────────────────────────────────
	// ⑥ SRV（読む用のビュー）を張る  ＜＜同じ colorResource_ に対して
	// ────────────────────────────────────────────────────────────────
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc_.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	srvIndex_ = dxCommon_->AllocateSrv();
	dxCommon_->GetDevice()->CreateShaderResourceView(
		colorResource_.Get(),
		&srvDesc,
		dxCommon_->GetSrvCpuDescriptorHandle(srvIndex_));
}

// ════════════════════════════════════════════════════════════════════
// 深度のリソースを作り、DSV を張る
//   DirectXCommon::InitializeDepthStencilView とほぼ同じ内容。
//   違いは「解像度が desc_ 由来」と「番号を配布係からもらう」の2点
// ════════════════════════════════════════════════════════════════════
void RenderTexture::CreateDepthResource() {

	// ① リソースの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = desc_.width;
	resourceDesc.Height = desc_.height;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// ② VRAM 上に置く
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// ③ クリア最適値（深度は 1.0 = 一番遠い）
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clearValue.DepthStencil.Depth = 1.0f;

	// ④ リソースを作る
	HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,      // 深度書き込み状態で開始
		&clearValue,
		IID_PPV_ARGS(&depthResource_));
	assert(SUCCEEDED(hr));

	depthResource_->SetName(L"RenderTexture_Depth");

	// ⑤ DSV を張る
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	dsvIndex_ = dxCommon_->AllocateDsv();
	dxCommon_->GetDevice()->CreateDepthStencilView(
		depthResource_.Get(),
		&dsvDesc,
		dxCommon_->GetDsvCpuDescriptorHandle(dsvIndex_));
}

// ════════════════════════════════════════════════════════════════════
// 描画先をこのテクスチャに切り替えてクリアする
//   DirectXCommon::PreDraw のオフスクリーン版
// ════════════════════════════════════════════════════════════════════
void RenderTexture::PreDraw() {
	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList().Get();

	// ────────────────────────────────────────────────────────────────
	// ① 読み取り状態 → 描画先状態
	//    2フレーム目以降はここで PIXEL_SHADER_RESOURCE から戻る
	// ────────────────────────────────────────────────────────────────
	TransitionTo(D3D12_RESOURCE_STATE_RENDER_TARGET);

	// ────────────────────────────────────────────────────────────────
	// ② 描画先（RTV と DSV）を設定し、深度をクリアする
	// ────────────────────────────────────────────────────────────────
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
		dxCommon_->GetRtvCpuDescriptorHandle(rtvIndex_);

	if (desc_.useDepth) {
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
			dxCommon_->GetDsvCpuDescriptorHandle(dsvIndex_);

		commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
		commandList->ClearDepthStencilView(
			dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	} else {
		// 深度を持たない場合は nullptr。深度テストなしで描画される
		commandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);
	}

	// ────────────────────────────────────────────────────────────────
	// ③ カラーをクリアする
	//    ※ 色は clearValue と同じでなければならない
	// ────────────────────────────────────────────────────────────────
	float clearColor[] = {
		desc_.clearColor.x, desc_.clearColor.y,
		desc_.clearColor.z, desc_.clearColor.w };
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// ────────────────────────────────────────────────────────────────
	// ④ 描画範囲を設定する
	//    スワップチェーンと解像度が違う場合があるので必ず設定し直す
	// ────────────────────────────────────────────────────────────────
	commandList->RSSetViewports(1, &viewport_);
	commandList->RSSetScissorRects(1, &scissorRect_);
}

// ════════════════════════════════════════════════════════════════════
// 描画を終えて、シェーダーから読める状態に戻す
// ════════════════════════════════════════════════════════════════════
void RenderTexture::PostDraw() {
	TransitionTo(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// ════════════════════════════════════════════════════════════════════
// 状態を遷移させる
//   同じ状態なら何もしない → PreDraw / PostDraw を何度呼んでも壊れない
// ════════════════════════════════════════════════════════════════════
void RenderTexture::TransitionTo(D3D12_RESOURCE_STATES afterState) {
	// すでにその状態なら、バリアを張る必要がない
	if (currentState_ == afterState) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = colorResource_.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = currentState_;
	barrier.Transition.StateAfter = afterState;

	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);

	// 今の状態を更新する（これを忘れると次のバリアで StateBefore が嘘になる）
	currentState_ = afterState;
}

// ════════════════════════════════════════════════════════════════════
// テクスチャとして読むための GPU ハンドル
// ════════════════════════════════════════════════════════════════════
D3D12_GPU_DESCRIPTOR_HANDLE RenderTexture::GetSrvGpuHandle() const {
	return dxCommon_->GetSrvGpuDescriptorHandle(srvIndex_);
}
#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "MathUtility.h"   // Vector4

// 前方宣言（ポインタとしてしか使わないので #include 不要）
class DirectXCommon;

// ════════════════════════════════════════════════════════════════════
// オフスクリーン描画先
//   1つのリソースに RTV（書く用）と SRV（読む用）の両方を張る
//   ・PreDraw / PostDraw で描画先として使う
//   ・GetSrvGpuHandle() でテクスチャとして読める
// ════════════════════════════════════════════════════════════════════
class RenderTexture {
public:
	// ────────────────────────────────────────────────────────────────
	// 生成時の設定
	//   項目が増えても Initialize の signature を変えずに済む
	// ────────────────────────────────────────────────────────────────
	struct Desc {
		// 解像度（必須。0 のままだと assert で止まる）
		uint32_t width = 0;
		uint32_t height = 0;

		// カラーのフォーマット
		//   スワップチェーンの RTV と同じ sRGB にしておくと見た目が一致する
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		// クリア色
		//   ※ 生成後に変えられない（理由は .cpp のコメント参照）
		Vector4 clearColor = { 0.1f, 0.25f, 0.5f, 1.0f };

		// 深度バッファを持つか
		//   3D を描くなら true。ポストエフェクトの中間バッファなら false
		bool useDepth = true;
	};

	/// <summary>生成する</summary>
	/// <param name="dxCommon">デバイスとディスクリプタヒープの持ち主</param>
	/// <param name="desc">解像度・フォーマットなどの設定</param>
	void Initialize(DirectXCommon* dxCommon, const Desc& desc);

	/// <summary>描画先をこのテクスチャに切り替えてクリアする</summary>
	void PreDraw();

	/// <summary>描画を終えて、シェーダーから読める状態に戻す</summary>
	void PostDraw();

	// ────────────────────────────────────────────────────────────────
	// 取得系
	// ────────────────────────────────────────────────────────────────
	uint32_t GetWidth()  const { return desc_.width; }
	uint32_t GetHeight() const { return desc_.height; }

	/// <summary>縦横比。カメラの aspectRatio に渡す用</summary>
	float GetAspectRatio() const {
		return static_cast<float>(desc_.width) / static_cast<float>(desc_.height);
	}

	/// <summary>SRV ヒープ上の番号</summary>
	uint32_t GetSrvIndex() const { return srvIndex_; }

	/// <summary>テクスチャとして読むための GPU ハンドル（ImGui::Image にも渡せる）</summary>
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const;

private:
	// カラーのリソース + RTV + SRV を作る
	void CreateColorResource();

	// 深度のリソース + DSV を作る
	void CreateDepthResource();

	// 状態を遷移させる（同じ状態なら何もしない）
	void TransitionTo(D3D12_RESOURCE_STATES afterState);

	// 借りているもの（所有していない）
	DirectXCommon* dxCommon_ = nullptr;

	// 生成時の設定をそのまま保持する
	//   width などを別メンバに写すと二重管理になるので持たない
	Desc desc_{};

	// ────────────────────────────────────────────────────────────────
	// カラー（RTV と SRV の両方を張る）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> colorResource_;
	uint32_t rtvIndex_ = 0;
	uint32_t srvIndex_ = 0;

	// ────────────────────────────────────────────────────────────────
	// 深度（useDepth が false なら作られない）
	// ────────────────────────────────────────────────────────────────
	Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_;
	uint32_t dsvIndex_ = 0;

	// ────────────────────────────────────────────────────────────────
	// 描画範囲（解像度から決まる。実行中変わらないので1回だけ作る）
	// ────────────────────────────────────────────────────────────────
	D3D12_VIEWPORT viewport_{};
	D3D12_RECT     scissorRect_{};

	// 今どの状態にいるか（バリアの StateBefore に使う）
	D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
};
#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

#include "MathUtility.h"

// 前方宣言（ポインタとしてしか使わないので #include 不要）
class DirectXCommon;

// ════════════════════════════════════════
// 基本プリミティブ描画
//   毎フレーム Reset() → DrawLine3d() で積む → Draw() でまとめて描画する
//   線を何本引いてもドローコールは1回
// ════════════════════════════════════════
class PrimitiveDrawer {
public:
	// 1フレームに積める線の最大数
	static constexpr uint32_t kMaxLineCount = 4096;
	// 線1本あたりの頂点数
	static constexpr uint32_t kVertexCountLine = 2;

	// 頂点1個の中身（Line.VS.hlsl の VertexShaderInput と対応）
	struct VertexPosColor {
		Vector3 position;   // 12バイト
		Vector4 color;      // 16バイト
	};                      // 合計28バイト

	/// <summary>初期化。シェーダー・RootSignature・PSO・バッファを作る</summary>
	void Initialize(DirectXCommon* dxCommon);

	/// <summary>フレームの先頭で呼ぶ。積んだ線をクリアする</summary>
	void Reset();

	/// <summary>線を1本積む。この時点ではまだ描画されない</summary>
	/// <param name="p1">始点（ワールド座標）</param>
	/// <param name="p2">終点（ワールド座標）</param>
	/// <param name="color">色(RGBA)</param>
	void DrawLine3d(const Vector3& p1, const Vector3& p2, const Vector4& color);

	/// <summary>積んだ線をまとめて1回で描画する</summary>
	/// <param name="commandList">描画コマンドを積むコマンドリスト（所有しない）</param>
	/// <param name="viewProjection">ビュー行列 × 射影行列</param>
	void Draw(const Matrix4x4& viewProjection);

	/// <summary>今フレームに積んだ本数（ImGui表示用）</summary>
	uint32_t GetLineCount() const { return lineCount_; }

private:
	// 定数バッファの中身（Line.VS.hlsl の TransformationMatrix と対応）
	struct ConstBufferData {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	DirectXCommon* dxCommon_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	// 頂点バッファ（Mapしっぱなしで毎フレーム上書きする）
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	VertexPosColor* vertexData_ = nullptr;

	// 定数バッファ（線はワールド座標なので World は常に単位行列）
	Microsoft::WRL::ComPtr<ID3D12Resource> constBufferResource_;
	ConstBufferData* constBufferData_ = nullptr;

	// 今フレームに積んだ線の本数
	uint32_t lineCount_ = 0;
};
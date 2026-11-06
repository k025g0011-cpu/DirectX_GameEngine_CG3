#include "ParticlePipeline.h"

#include <cassert>

#include "DirectXCommon.h"
#include "Logger.h"

using Microsoft::WRL::ComPtr;

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void ParticlePipeline::Initialize(DirectXCommon* dxCommon) {
	assert(dxCommon != nullptr);
	dxCommon_ = dxCommon;

	CreateRootSignature();    // 先に引数の形を決める
	CreatePipelineStates();   // それを参照して PSO を作る
}

// ════════════════════════════════════════════════════════════════════
// RootSignature の生成
// ════════════════════════════════════════════════════════════════════
void ParticlePipeline::CreateRootSignature() {

	const ComPtr<ID3D12Device>& device = dxCommon_->GetDevice();
	HRESULT hr{};

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	// 「InputAssembler（頂点入力）を使ってよい」という許可
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_ROOT_PARAMETER rootParameters[3] = {};

	// [0] Material（PS b0）
	rootParameters[kRootParameterMaterial].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootParameterMaterial].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParameterMaterial].Descriptor.ShaderRegister = 0;

	//	------------------------------------------------------------通常パイプラインとの違い区画
	// [1] TransformationMatrix の配列（VS t0）
	//   StructuredBuffer はビューとしては SRV。テーブル経由で渡す
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0;   // t0 から始まる
	descriptorRangeForInstancing[0].NumDescriptors = 1;       // SRV は1つ（配列の中身は何個でもよい）
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[kRootParameterInstancing].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	// VERTEX にする。[2] の Texture も t0 だが、そちらは PIXEL なので衝突しない
	rootParameters[kRootParameterInstancing].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[kRootParameterInstancing].DescriptorTable.pDescriptorRanges =
		descriptorRangeForInstancing;
	rootParameters[kRootParameterInstancing].DescriptorTable.NumDescriptorRanges =
		_countof(descriptorRangeForInstancing);
	// -------------------------------------------------------------

	// [2] Texture（PS t0）… ディスクリプタテーブル経由
	D3D12_DESCRIPTOR_RANGE descriptorRangeForTexture[1] = {};
	descriptorRangeForTexture[0].BaseShaderRegister = 0;   // t0 から始まる
	descriptorRangeForTexture[0].NumDescriptors = 1;       // t0 の1枚だけ
	descriptorRangeForTexture[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForTexture[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[kRootParameterTexture].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootParameterTexture].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParameterTexture].DescriptorTable.pDescriptorRanges =
		descriptorRangeForTexture;
	rootParameters[kRootParameterTexture].DescriptorTable.NumDescriptorRanges =
		_countof(descriptorRangeForTexture);


	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	// ────────────────────────────────────────────────────────────────
	// サンプラー（テクスチャの読み方）
	// ────────────────────────────────────────────────────────────────
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};   // register(s0)
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニア
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// ────────────────────────────────────────────────────────────────
	// シリアライズして生成する
	// ────────────────────────────────────────────────────────────────
	ComPtr<ID3DBlob> signatureBlob = nullptr;   // 成功時のバイナリ
	ComPtr<ID3DBlob> errorBlob = nullptr;       // 失敗時のエラーメッセージ

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob);
	if (FAILED(hr)) {
		Log(dxCommon_->GetLogStream(), reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	hr = device->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));

	rootSignature_->SetName(L"ParticleRootSignature");
}

// ════════════════════════════════════════════════════════════════════
// PSO の生成
//   ブレンドモードの数だけ作る。ブレンド以外の設定はすべて共通
// ════════════════════════════════════════════════════════════════════
void ParticlePipeline::CreatePipelineStates() {

	const ComPtr<ID3D12Device>& device = dxCommon_->GetDevice();
	HRESULT hr{};

	// ────────────────────────────────────────────────────────────────
	// InputLayout（VertexData と Particle.VS.hlsl の両方と一致させる）
	// ────────────────────────────────────────────────────────────────
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};

	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;   // Vector3
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;      // Vector2
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;   // Vector3
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);


	// ────────────────────────────────────────────────────────────────
	// RasterizerState
	//   粒は裏返ることもあるので、表裏を区別せず両面描く
	// ────────────────────────────────────────────────────────────────
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;   // 両面描く
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// ────────────────────────────────────────────────────────────────
	// DepthStencilState
	//   テストはする（壁の裏の粒は隠れる）
	//   書き込みはしない（粒どうしが隠し合わないように）
	// ────────────────────────────────────────────────────────────────
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;                             // 深度テストは有効
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;   // 書き込まない
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// ────────────────────────────────────────────────────────────────
	// シェーダーのコンパイル（ここが Object3d との唯一の実質的な違い）
	// ────────────────────────────────────────────────────────────────
	ComPtr<IDxcBlob> vertexShaderBlob =
		dxCommon_->CompileShader(L"Particle.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	ComPtr<IDxcBlob> pixelShaderBlob =
		dxCommon_->CompileShader(L"Particle.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	// ────────────────────────────────────────────────────────────────
	// PSO の共通設定（BlendState 以外をすべて埋める）
	// ────────────────────────────────────────────────────────────────
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{};
	pipelineStateDesc.pRootSignature = rootSignature_.Get();
	pipelineStateDesc.InputLayout = inputLayoutDesc;
	pipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
							 vertexShaderBlob->GetBufferSize() };
	pipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
							 pixelShaderBlob->GetBufferSize() };
	pipelineStateDesc.RasterizerState = rasterizerDesc;
	pipelineStateDesc.DepthStencilState = depthStencilDesc;
	pipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	pipelineStateDesc.NumRenderTargets = 1;
	pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	pipelineStateDesc.SampleDesc.Count = 1;
	pipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// ────────────────────────────────────────────────────────────────
	// ブレンドモードの数だけ PSO を作る
	//   CreateBlendDesc は BlendMode.h が提供する（P-0 で切り出したもの）
	// ────────────────────────────────────────────────────────────────
	for (uint32_t i = 0; i < static_cast<uint32_t>(BlendMode::kMaxCount); ++i) {

		pipelineStateDesc.BlendState = CreateBlendDesc(static_cast<BlendMode>(i));

		hr = device->CreateGraphicsPipelineState(
			&pipelineStateDesc, IID_PPV_ARGS(&pipelineStates_[i]));
		assert(SUCCEEDED(hr));
	}
}

// ════════════════════════════════════════════════════════════════════
// パイプラインをコマンドリストに設定する
// ════════════════════════════════════════════════════════════════════
void ParticlePipeline::SetPipeline(BlendMode blendMode) {

	assert(blendMode < BlendMode::kMaxCount);

	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList().Get();

	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineStates_[static_cast<uint32_t>(blendMode)].Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
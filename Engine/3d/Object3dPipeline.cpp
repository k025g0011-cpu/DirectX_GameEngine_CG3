#include "Object3dPipeline.h"

#include <cassert>

#include "DirectXCommon.h"
#include "Logger.h"

using Microsoft::WRL::ComPtr;

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void Object3dPipeline::Initialize(DirectXCommon* dxCommon) {
	assert(dxCommon != nullptr);
	dxCommon_ = dxCommon;

	CreateRootSignature();    // 先に引数の形を決める
	CreatePipelineStates();   // それを参照して PSO を作る
}

// ════════════════════════════════════════════════════════════════════
// RootSignature の生成
//   シェーダとリソースをどう関連付けるかを示した受け渡し表
// ════════════════════════════════════════════════════════════════════
void Object3dPipeline::CreateRootSignature() {

	const ComPtr<ID3D12Device>& device = dxCommon_->GetDevice();
	HRESULT hr{};

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	// 「InputAssembler（頂点入力）を使ってよい」という許可
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// ────────────────────────────────────────────────────────────────
	// ルートパラメータ（シェーダーへの引数の一覧）
	//   ここの並び順が kRootParameterXxx の値そのもの
	// ────────────────────────────────────────────────────────────────
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	// [0] Material（PS b0）
	rootParameters[kRootParameterMaterial].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
	rootParameters[kRootParameterMaterial].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
	rootParameters[kRootParameterMaterial].Descriptor.ShaderRegister = 0;					  // レジスタ番号0とバインド

	// [1] TransformationMatrix（VS b0）
	rootParameters[kRootParameterTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;		// CBVを使う
	rootParameters[kRootParameterTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;	// VertexShaderで使う
	rootParameters[kRootParameterTransform].Descriptor.ShaderRegister = 0;						// レジスタ番号0とバインド

	// [2] Texture（PS t0）… テクスチャはディスクリプタテーブル経由
	//   1回の描画で複数のテクスチャを読みたくなったら、この配列の要素が増える
	//   （PBR なら ベースカラー + 法線 + ラフネス で3〜4枚）
	/*
	1回の描画（1つのピクセル）で複数のテクスチャを読みたいときに、registerの数が増えていく（t1,t2,t3・・・)>>> 伴ってdescriptorRangeの要素も増える
	* 同時に複数枚使う代表例
	   ＜用途＞	                    ＜同時に読むテクスチャ＞
	  PBR（リアルな質感）	        ベースカラー + 法線マップ + ラフネス/メタリック（+ AO）→ 3〜4枚
	  IBL（環境光）	            ベースカラー + 環境マップ（キューブマップ）+ BRDF LUT など
	  影	                        ベースカラー + シャドウマップ
	  エフェクト	                ベースカラー + ディゾルブ用のノイズ画像 / マスク画像
	  地形	                    草・土・岩の画像 + どこに何を塗るかのブレンド用画像
	  ポストエフェクト	            描画結果の画面 + 深度（被写界深度など）
	*/
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;   // t0 から始まる
	descriptorRange[0].NumDescriptors = 1;       // t0 の1枚だけ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[kRootParameterTexture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootParameterTexture].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[kRootParameterTexture].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[kRootParameterTexture].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	// [3] DirectionalLight（PS b1）
	rootParameters[kRootParameterLight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;	  // CBVを使う
	rootParameters[kRootParameterLight].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // VertexShaderで使う
	rootParameters[kRootParameterLight].Descriptor.ShaderRegister = 1;					  // レジスタ番号1とバインド

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);


#pragma region MyRegion
	/*
	 ＜記号＞	    ＜由来＞	                    ＜何を置くか＞	                        ＜HLSL の例＞
	   b	    Buffer（Constant Buffer）	CPU から渡す定数（行列・色・ライト）	    ConstantBuffer<Material> gMaterial : register(b0);
	   t	    Texture	                    シェーダが読むリソース（SRV）	        Texture2D<float32_t4> gTexture : register(t0);
	   s	    Sampler	                    テクスチャの読み方（補間・繰り返し）	    SamplerState gSampler : register(s0);
	   u	    Unordered Access（UAV）	    シェーダから読み書きするリソース	        RWTexture2D<float4> gOutput : register(u0);

	*/
#pragma endregion

	// ────────────────────────────────────────────────────────────────
	// サンプラー（テクスチャの読み方）
	//   RootSignature に焼き込むので、実行時に何も渡さなくてよい
	// ────────────────────────────────────────────────────────────────
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};   // register(s0)
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;   // バイリニア
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0~1の範囲外をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                 // ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// ────────────────────────────────────────────────────────────────
	// シリアライズ（ポインタの先まで全部たどって1本のバイト列に詰め直す）
	// ────────────────────────────────────────────────────────────────
	ComPtr<ID3DBlob> signatureBlob = nullptr;   // 成功時のバイナリ
	ComPtr<ID3DBlob> errorBlob = nullptr;       // 失敗時のエラーメッセージ

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature,      // 設計図
		D3D_ROOT_SIGNATURE_VERSION_1,   // 書式のバージョン
		&signatureBlob,
		&errorBlob);
	if (FAILED(hr)) {
		Log(dxCommon_->GetLogStream(), reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	// バイナリを元に生成
	hr = device->CreateRootSignature(
		0,                                   // nodeMask。GPU が1台なら 0
		signatureBlob->GetBufferPointer(),   // バイナリ本体の場所
		signatureBlob->GetBufferSize(),      // バイナリのバイト数
		IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

// ════════════════════════════════════════════════════════════════════
// PSO の生成
//   ブレンドモードの数だけ作る。ブレンド以外の設定はすべて共通
// ════════════════════════════════════════════════════════════════════
void Object3dPipeline::CreatePipelineStates() {

	const ComPtr<ID3D12Device>& device = dxCommon_->GetDevice();
	HRESULT hr{};

	// ────────────────────────────────────────────────────────────────
	// InputLayout（VertexShader へ渡す頂点データの形）
	//   VertexData（position / texcoord / normal）と対応している
	//   ※ VertexData を変えたら、ここと HLSL の3点セットで直すこと
	// ────────────────────────────────────────────────────────────────
#pragma region MyRegion
//   HLSL の POSITION0 を、名前 "POSITION" と番号 0 に分けて指定
//   番号が分かれているのは、同じ種類のデータを複数持つことがあるからです。
/*
	<HLSL 側>	                <C++ 側>	        <値>
	POSITION	                SemanticName	"POSITION"
	0（POSITION0 の数字部分）	    SemanticIndex	 0

	<hlsl>
	float32_t2 uv0 : TEXCOORD0;   // SemanticName = "TEXCOORD", SemanticIndex = 0
	float32_t2 uv1 : TEXCOORD1;   // SemanticName = "TEXCOORD", SemanticIndex = 1
*/
#pragma endregion
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};

	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;      // Vector3   <<<<float が 3つ  =  12バイト  (シェーダーではfloat32_t4）
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;         // Vector2　　<<<<float が 2つ  =  8バイト  (シェーダーではfloat32_t2）
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;      // Vector3	 <<<<float が 3つ  =  12バイト  (シェーダーではfloat32_t3）
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

#pragma region  MyRegion
	/*
	
	inputLayoutDesc は、
		「頂点1個の形の説明は、この配列のここから○行ぶんだよ」
		ていうやつ
	*/
#pragma endregion
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// ────────────────────────────────────────────────────────────────
	// RasterizerState（三角形をピクセルに分解する処理の設定）
	//   画面上で時計回り＝表、反時計回り＝裏（DirectX の既定）
	// ────────────────────────────────────────────────────────────────
#pragma region  MyRegion
/*
<< Rasterizerとは
・VS が出力した三角形の頂点3つを受け取り、「画面上のどのピクセルがその三角形の内側か

<< GPU は頂点を並べた順番で表裏を判定します。DirectX の既定は次のとおりです。>>

・画面上で時計回りに並んだ三角形    → 表（描かれる）
・画面上で反時計回りに並んだ三角形  → 裏（BACK なら捨てられる）

※三角形を描いたのに何も出ないときは、頂点の順番を疑ってください。
	デバッグ中は一時的に CULL_MODE_NONE にして、表示されるか確かめるのが定番の切り分けです。

*/
#pragma endregion
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;   // 裏面を描かない
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;  // 中を塗りつぶす

	// ────────────────────────────────────────────────────────────────
	// DepthStencilState（奥行きの比較方法）
	// ────────────────────────────────────────────────────────────────
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;                            // 深度テストを有効に
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;   // 深度を書き込む
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;  // 近い(同じ)なら描画

	// ────────────────────────────────────────────────────────────────
	// シェーダーのコンパイル
	//   ループの外で1回だけ。6個の PSO が同じバイナリを共有する
	// ────────────────────────────────────────────────────────────────
	ComPtr<IDxcBlob> vertexShaderBlob =
		dxCommon_->CompileShader(L"Object3d.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	ComPtr<IDxcBlob> pixelShaderBlob =
		dxCommon_->CompileShader(L"Object3d.PS.hlsl", L"ps_6_0");
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

	pipelineStateDesc.NumRenderTargets = 1; // PixelShaderOutput の SV_TARGET0 に対応
	pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // RTV を作ったときの rtvDesc.Format と一致させること

	// 利用するトポロジ(形状)のタイプ。三角形
	pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// SwapChain で SampleDesc.Count = 1 にしたのと揃える
	pipelineStateDesc.SampleDesc.Count = 1;
	pipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// ────────────────────────────────────────────────────────────────
	// ブレンドモードの数だけ PSO を作る
	//   BlendState だけを差し替えて、同じ設定から6個生成する
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
//   描画コマンドを積む前に、1フレームに必要な回数だけ呼ぶ
// ════════════════════════════════════════════════════════════════════
void Object3dPipeline::SetPipeline(BlendMode blendMode) {

	assert(blendMode < BlendMode::kMaxCount);

	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList().Get();

	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineStates_[static_cast<uint32_t>(blendMode)].Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
#include "PrimitiveDrawer.h"
#include "DirectXCommon.h"     
#include "Logger.h"

#include <cassert>

// ════════════════════════════════════════════════════════════════════
// 初期化
//   main.cpp にあった「線用パイプライン一式」をここへ引っ越したもの
// ════════════════════════════════════════════════════════════════════
void PrimitiveDrawer::Initialize(DirectXCommon* dxCommon) {

    dxCommon_ = dxCommon;

    const Microsoft::WRL::ComPtr<ID3D12Device>& device = dxCommon_->GetDevice();

    HRESULT hr{};

    // ────────────────────────────────────────────────────────────────
    // ① RootSignature
    //     b0 に TransformationMatrix を1個だけ送る（VS のみが読む）
    // ────────────────────────────────────────────────────────────────
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_ROOT_PARAMETER rootParameters[1]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister = 0;   // b0

    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
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

    // ────────────────────────────────────────────────────────────────
    // ② InputLayout
    //     VertexPosColor { Vector3 position; Vector4 color; } と対応
    // ────────────────────────────────────────────────────────────────
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[2]{};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;      // Vector3
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElementDescs[1].SemanticName = "COLOR";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;   // Vector4
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // ────────────────────────────────────────────────────────────────
    // ③ BlendState（main.cpp から借りていたものを自前で持つ）
    // ────────────────────────────────────────────────────────────────
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // ────────────────────────────────────────────────────────────────
    // ④ RasterizerState
    //     線はカリングされないので CULL_NONE でも意味は同じ（明示のため）
    // ────────────────────────────────────────────────────────────────
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    // ────────────────────────────────────────────────────────────────
    // ⑤ DepthStencilState（main.cpp から借りていたものを自前で持つ）
    //     深度テストは有効・書き込みも有効 → モデルと正しく前後する
    // ────────────────────────────────────────────────────────────────
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // ────────────────────────────────────────────────────────────────
    // ⑥ シェーダーをコンパイル
    // ────────────────────────────────────────────────────────────────
    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob =
        dxCommon_->CompileShader(L"Line.VS.hlsl", L"vs_6_0");
    assert(vertexShaderBlob != nullptr);

    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob =
        dxCommon_->CompileShader(L"Line.PS.hlsl", L"ps_6_0");
    assert(pixelShaderBlob != nullptr);

    // ────────────────────────────────────────────────────────────────
    // ⑦ PSO（3つの契約をここで検査される）
    // ────────────────────────────────────────────────────────────────
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{};
    pipelineStateDesc.pRootSignature = rootSignature_.Get();
    pipelineStateDesc.InputLayout = inputLayoutDesc;
    pipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
                             vertexShaderBlob->GetBufferSize() };
    pipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
                             pixelShaderBlob->GetBufferSize() };
    pipelineStateDesc.BlendState = blendDesc;
    pipelineStateDesc.RasterizerState = rasterizerDesc;
    pipelineStateDesc.DepthStencilState = depthStencilDesc;
    pipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipelineStateDesc.NumRenderTargets = 1;
    pipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    // ここが「三角形用」との唯一の違い
    pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

    pipelineStateDesc.SampleDesc.Count = 1;
    pipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    hr = device->CreateGraphicsPipelineState(
        &pipelineStateDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));

    // ────────────────────────────────────────────────────────────────
    // ⑧ 頂点バッファ（最大本数ぶんを最初に確保しておく）
    //     4096本 × 2頂点 × 28バイト ≒ 224KB
    // ────────────────────────────────────────────────────────────────
    const size_t vertexBufferSize =
        sizeof(VertexPosColor) * kMaxLineCount * kVertexCountLine;

    vertexResource_ = dxCommon_->CreateBufferResource(vertexBufferSize);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    vertexBufferView_.StrideInBytes = sizeof(VertexPosColor);

    // Map したまま持ち続ける（毎フレーム書き換えるので Unmap しない）
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

    // ────────────────────────────────────────────────────────────────
    // ⑨ 定数バッファ
    // ────────────────────────────────────────────────────────────────
    constBufferResource_ = dxCommon_->CreateBufferResource(sizeof(ConstBufferData));
    constBufferResource_->Map(0, nullptr, reinterpret_cast<void**>(&constBufferData_));
    constBufferData_->WVP = MakeIdentity4x4();
    constBufferData_->World = MakeIdentity4x4();
}

// ════════════════════════════════════════════════════════════════════
// 書き込み位置のリセット
//   毎フレームの最初に呼ぶ。バッファの中身は消さない（上から書き潰す）
// ════════════════════════════════════════════════════════════════════
void PrimitiveDrawer::Reset() {
    lineCount_ = 0;
}

// ════════════════════════════════════════════════════════════════════
// 線1本を「予約」する
//   ここでは描画命令を出さない。頂点を書き込むだけ
// ════════════════════════════════════════════════════════════════════
void PrimitiveDrawer::DrawLine3d(const Vector3& p1, const Vector3& p2, const Vector4& color) {
    // 上限チェック。超えたら黙って無視する（落とさない）
    assert(lineCount_ < kMaxLineCount);
    if (lineCount_ >= kMaxLineCount) {
        return;
    }

    // この線が使う頂点の書き込み開始位置
    //   0本目 → 頂点[0][1]   1本目 → 頂点[2][3]   2本目 → 頂点[4][5]
    const uint32_t index = lineCount_ * kVertexCountLine;

    vertexData_[index + 0].position = p1;
    vertexData_[index + 0].color = color;
    vertexData_[index + 1].position = p2;
    vertexData_[index + 1].color = color;

    ++lineCount_;
}

// ════════════════════════════════════════════════════════════════════
// 予約された線を「まとめて1回」描画する
// ════════════════════════════════════════════════════════════════════
void PrimitiveDrawer::Draw(const Matrix4x4& viewProjection) {
    // 1本も予約されていなければ何もしない
    if (lineCount_ == 0) {
        return;
    }

    // 今フレームのコマンドリストを取得する
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList().Get();

    // ① 定数バッファを更新
    //    線の座標はすでにワールド空間なので World は単位行列のまま。
    //    つまり WVP = ViewProjection でよい
    constBufferData_->WVP = viewProjection;
    constBufferData_->World = MakeIdentity4x4();

    // ② パイプラインを線用に切り替え
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, constBufferResource_->GetGPUVirtualAddress());

    // ③ 今フレームで予約された頂点数だけ描画
    commandList->DrawInstanced(lineCount_ * kVertexCountLine, 1, 0, 0);
}
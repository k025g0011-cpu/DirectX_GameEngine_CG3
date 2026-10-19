#include "Particle.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
};

// ConstantBuffer ではなく StructuredBuffer を使う
//   C++ の配列の hlsl 版。register は SRV なので b ではなく t
StructuredBuffer<TransformationMatrix> gTransformationMatrices : register(t0);

struct VertexShaderInput // CPU -> VS
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

// 第2引数の instanceId は「今が何個目のインスタンスか」をシステムが入れてくれる
//   DrawInstanced(頂点数, N, 0, 0) なら 0 〜 N-1 が割り当てられる
VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrices[instanceId].WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(
        mul(input.normal, (float32_t3x3) gTransformationMatrices[instanceId].World));
    return output;
}
#include "object3d.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};

ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

struct VertexShaderInput // CPU -> VS
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatrix.WVP);
    output.texcoord = input.texcoord;
    //   法線は World ではなく「World の逆行列の転置」で変換する
    //     非均等スケール（scale の x/y/z が違う）のとき、World だと向きが狂うため
    output.normal = normalize(mul(input.normal, (float32_t3x3) gTransformationMatrix.WorldInverseTranspose));
        // ワールド座標。World 行列だけを掛ける（射影は掛けない）
    //   .xyz で切り出すのは、w は使わないため
    output.worldPosition = mul(input.position, gTransformationMatrix.World).xyz;
    return output;
}

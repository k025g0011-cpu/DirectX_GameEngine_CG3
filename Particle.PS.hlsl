#include "Particle.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
};


ConstantBuffer<Material> gMaterial : register(b0);
Texture2D<float32_t4> gTexture : register(t0); // SRVのregisterはt
SamplerState gSampler : register(s0); // Samplerのregisterはs

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // ライティングはしない。マテリアル色とテクスチャの掛け算だけ
    output.color = gMaterial.color * textureColor;

    // αが完全に0のピクセルは書き込まない
    //   深度バッファも汚さないので、粒が後ろの粒を隠さなくなる
    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}
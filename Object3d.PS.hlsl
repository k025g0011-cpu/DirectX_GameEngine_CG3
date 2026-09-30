#include "object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
Texture2D<float32_t4> gTexture : register(t0); // SRVのregisterはt
SamplerState gSampler : register(s0); // Samplerのregisterはs

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform); // XとY座標だけ取り出す
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy); // Textureをどのように（Sampler）、**どの部分を（texcoord）**読むか
    if (gMaterial.enableLighting != 0)
    { // Lightingする場合
        float NdotL = dot(normalize(input.normal), -gDirectionalLight.direction);
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f); // Half Lambert
        
        // 色（rgb）にだけライティングを掛ける
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb
                         * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        // α値はライティングの影響を受けない。マテリアルとテクスチャだけで決まる
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    { // Lightingしない場合
        output.color = gMaterial.color * textureColor;
    }
    return output;
}

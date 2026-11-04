#include "object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    int32_t specularModel; // 0 = Phong, 1 = Blinn-Phong
    float32_t4x4 uvTransform;
    float shininess;            // 光沢度。大きいほど狭く鋭く光る
    float32_t3 specularColor;   // 鏡面反射色
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};

struct PointLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float radius;
    float decay;
};

struct Camera
{
    float32_t3 worldPosition;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);
Texture2D<float32_t4> gTexture : register(t0); // SRVのregisterはt
SamplerState gSampler : register(s0); // Samplerのregisterはs

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

//   ライティング計算に必要な「面そのもの」の情報
//     ライトが何個あっても変わらない値をまとめておく
struct SurfaceInfo
{
    float32_t3 normal; // 正規化済みの法線
    float32_t3 toEye; // ピクセル → カメラ の向き
    float32_t3 baseColor; // マテリアル色 × テクスチャ色
};

//   光源1つぶんの寄与を計算する
//     surface        : 面の情報
//     lightDirection : 光が「進む」向き（正規化済み）
//     lightColor     : 光の色
//     intensity      : 明るさ（点光源なら距離減衰を掛けたもの）
//   gMaterial はグローバルなので引数に渡さなくても読める
float32_t3 ComputeLighting(
    SurfaceInfo surface,
    float32_t3 lightDirection,
    float32_t3 lightColor,
    float intensity)
{
    //   拡散反射（ハーフランバート）
    float NdotL = dot(surface.normal, -lightDirection);
    float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
    float32_t3 diffuse = surface.baseColor * lightColor * cos * intensity;

    //   鏡面反射。方式はマテリアルが決める（一様な分岐なので if でよい）
    float specularPower;
    if (gMaterial.specularModel == 0)
    {
        //   Phong
        float32_t3 reflectLight = reflect(lightDirection, surface.normal);
        specularPower = pow(saturate(dot(reflectLight, surface.toEye)), gMaterial.shininess);
    }
    else
    {
        //   Blinn-Phong
        float32_t3 halfVector = normalize(-lightDirection + surface.toEye);
        specularPower = pow(saturate(dot(surface.normal, halfVector)), gMaterial.shininess);
    }

    //   光が当たっていない面では、つやも出さない
    float lightMask = step(0.0f, NdotL);
    float32_t3 specular = lightColor * intensity * specularPower * lightMask * gMaterial.specularColor;

    return diffuse + specular;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    float4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform); // XとY座標だけ取り出す
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy); // Textureをどのように（Sampler）、**どの部分を（texcoord）**読むか
    
    if (gMaterial.enableLighting != 0)
    {
        //   面の情報を1回だけ作る。ライトが何個あっても使い回せる
        SurfaceInfo surface;
        surface.normal    = normalize(input.normal);
        surface.toEye     = normalize(gCamera.worldPosition - input.worldPosition);
        surface.baseColor = gMaterial.color.rgb * textureColor.rgb;

        //   ① 平行光源
        float32_t3 lighting = ComputeLighting(
            surface,
            normalize(gDirectionalLight.direction),
            gDirectionalLight.color.rgb,
            gDirectionalLight.intensity);

        //   ② 点光源
        //     光の向きは「光源 → ピクセル」。ピクセルごとに違う
        float32_t3 pointLightDirection = normalize(input.worldPosition - gPointLight.position);
        //     距離による減衰。radius で 0 になり、decay がカーブの鋭さ
        float distance = length(gPointLight.position - input.worldPosition);
        float factor = pow(saturate(-distance / gPointLight.radius + 1.0f), gPointLight.decay);

        lighting += ComputeLighting(
            surface,
            pointLightDirection,
            gPointLight.color.rgb,
            gPointLight.intensity * factor);   // ★減衰を intensity に掛け込む

        output.color.rgb = lighting;
        output.color.a   = gMaterial.color.a * textureColor.a;
    }
    else
    { // Lightingしない場合
        output.color = gMaterial.color * textureColor;
    }
    
    
    return output;
}

#include "object3d.hlsli"

//   マテリアルの情報をまとめておく構造体
struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    int32_t specularModel; // 0 = Phong, 1 = Blinn-Phong
    float32_t4x4 uvTransform;
    float shininess;            // 光沢度。大きいほど狭く鋭く光る
    float32_t3 specularColor;   // 鏡面反射色
};

//   平行光源は「色 + 向き」で表す
struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};

//   点光源は「位置 + 距離減衰」で表す
struct PointLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float radius;
    float decay;
    float32_t2 padding;
};

//   スポットライトは「点光源 + 円錐の向きと角度」で表す
struct SpotLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float32_t3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    float padding; 
};

//   ライトの数をまとめておく構造体
struct LightCount
{
    uint directionalCount;
    uint pointCount;
    uint spotCount;
};


struct Camera
{
    float32_t3 worldPosition;
};

ConstantBuffer<Material> gMaterial : register(b0); // CBVのregisterは <b>
ConstantBuffer<LightCount> gLightCount : register(b1); 
ConstantBuffer<Camera> gCamera : register(b2);
Texture2D<float32_t4> gTexture : register(t0); // SRVのregisterは <t>
StructuredBuffer<DirectionalLight> gDirectionalLights : register(t1);
StructuredBuffer<PointLight> gPointLights : register(t2);
StructuredBuffer<SpotLight> gSpotLights : register(t3);
SamplerState gSampler : register(s0); // Samplerのregisterは <s>

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

//   長さ 0 のベクトルを normalize すると NaN になる
//     NaN は足し算で全体に伝染し、画面が真っ黒になる
//     壊れた入力でも、壊れた出力にしない
float32_t3 SafeNormalize(float32_t3 v, float32_t3 fallback)
{
    float lengthSquared = dot(v, v);
    return (lengthSquared > 1e-12f) ? (v * rsqrt(lengthSquared)) : fallback;
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

        //   寄与を足し込んでいくので、0 から始める
        float32_t3 lighting = float32_t3(0.0f, 0.0f, 0.0f);

        //   ① 平行光源（入っている数だけ繰り返す）
        for (uint d = 0; d < gLightCount.directionalCount; ++d)
        {
            DirectionalLight light = gDirectionalLights[d];
            lighting += ComputeLighting(
                surface,
                SafeNormalize(light.direction, float32_t3(0.0f, -1.0f, 0.0f)),
                light.color.rgb,
                light.intensity);
        }

        //   ② 点光源
        for (uint p = 0; p < gLightCount.pointCount; ++p)
        {
            PointLight light = gPointLights[p];

            //     光の向きは「光源 → ピクセル」。ピクセルごとに違う
            float32_t3 lightDirection = normalize(input.worldPosition - light.position);
            //     距離による減衰。radius で 0 になり、decay がカーブの鋭さ
            float dist = length(light.position - input.worldPosition);
            float factor = pow(saturate(-dist / light.radius + 1.0f), light.decay);

            lighting += ComputeLighting(
                surface, lightDirection, light.color.rgb, light.intensity * factor);
        }

        //   ③ スポットライト
        for (uint s = 0; s < gLightCount.spotCount; ++s)
        {
            SpotLight light = gSpotLights[s];

            float32_t3 lightDirection = normalize(input.worldPosition - light.position);

            //     （1）距離による減衰
            float dist = length(light.position - input.worldPosition);
            float attenuationFactor = pow(saturate(-dist / light.distance + 1.0f), light.decay);

            //     （2）角度による減衰
            float cosAngle = dot(lightDirection,
                SafeNormalize(light.direction, float32_t3(0.0f, -1.0f, 0.0f)));
            float falloffRange = max(light.cosFalloffStart - light.cosAngle, 0.0001f);
            float falloffFactor = saturate((cosAngle - light.cosAngle) / falloffRange);

            lighting += ComputeLighting(
                surface, lightDirection, light.color.rgb,
                light.intensity * attenuationFactor * falloffFactor);
        }

        output.color.rgb = lighting;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    { // Lightingしない場合
        output.color = gMaterial.color * textureColor;
    }
    
    
    return output;
}

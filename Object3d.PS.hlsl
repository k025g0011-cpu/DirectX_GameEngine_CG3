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

struct Camera
{
    float32_t3 worldPosition;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
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
        
        // ───── 下準備：必要なベクトルをすべて正規化して用意する ─────
        float32_t3 normal = normalize(input.normal);
        
        // 光の進む向き（ライトから面へ）
        float32_t3 lightDirection = normalize(gDirectionalLight.direction);
        
        // 面からカメラへ向かう向き
        float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

        // ───── 拡散反射（Half Lambert）─────
        //   ざらざらした面の明るさ。どの角度から見ても同じ
        float NdotL = dot(normal, -lightDirection);
        float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
        
        float32_t3 diffuse = gMaterial.color.rgb * textureColor.rgb
                           * gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;

        // ───── 鏡面反射（Phong）─────
        //   光が鏡のように跳ね返った向きと、視線の向きが揃うほど強く光る
        float32_t3 reflectLight = reflect(lightDirection, normal);
        float RdotE = dot(reflectLight, toEye);
        
        //   鏡面反射の強さ。計算方式はマテリアルが決める
        //     この分岐の条件は定数バッファから来るので、
        //     1回の描画の中では全ピクセルが同じ道を通る（＝一様な分岐。重くない）
        float specularPower;
        if (gMaterial.specularModel == 0)
        {
            //   Phong：反射ベクトルと視線を比べる
            //     「跳ね返った光が、どれだけ目のほうを向いているか」
            float32_t3 reflectLight = reflect(lightDirection, normal);
            float RdotE = dot(reflectLight, toEye);
            specularPower = pow(saturate(RdotE), gMaterial.shininess);
        }
        else
        {
            //   Blinn-Phong：ハーフベクトルと法線を比べる
            //     「この面が、光を目へ跳ね返す理想の向きに、どれだけ近いか」
            //     -lightDirection は「光源へ向かう向き」。toEye は「目へ向かう向き」
            float32_t3 halfVector = normalize(-lightDirection + toEye);
            float NdotH = dot(normal, halfVector);
            specularPower = pow(saturate(NdotH), gMaterial.shininess);
        }
        
        
        //   光が当たっていない面（NdotL <= 0）では、つやも出さない
        //     これが無いと、裏側にハイライトだけが浮かぶ
        float lightMask = step(0.0f, NdotL);
        
        //   ハイライトの色はマテリアルが決める
        //     非金属 → 白（1,1,1）。光源の色がそのまま出る
        //     金属   → その金属の色（金なら 1.0, 0.76, 0.33 など）
        float32_t3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity
                            * specularPower * lightMask * gMaterial.specularColor;

        // ───── 合成 ─────
        //   拡散と鏡面は「足す」。掛けるのではない
        output.color.rgb = diffuse + specular;
        
        // α値はライティングの影響を受けない。マテリアルとテクスチャだけで決まる
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    { // Lightingしない場合
        output.color = gMaterial.color * textureColor;
    }
    return output;
}

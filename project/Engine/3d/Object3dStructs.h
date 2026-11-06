#pragma once
#include <cstdint>
#include "MathUtility.h"  

// 鏡面反射の計算方式
//   GPU に int として渡るので、基底型を明示して 4 バイトに固定する
enum class SpecularModel : int32_t {
    Phong = 0,   // reflect で反射ベクトルを作る。古典的
    BlinnPhong = 1,   // ハーフベクトルを使う。PBR につながる現代的な方式
};

// ライトの最大数
//   StructuredBuffer の確保サイズ。実際に使う数は LightCount で伝える
inline constexpr uint32_t kMaxDirectionalLights = 4;
inline constexpr uint32_t kMaxPointLights = 16;
inline constexpr uint32_t kMaxSpotLights = 16;

// ================================================================
// Object3d シェーダーとやり取りする構造体
//   モデルもスプライトも、この形でデータを渡す
// ================================================================

// VS の入力（InputLayout と一致していること）32B
// 1頂点の持つデータ
struct VertexData {	  // 全体＜32＞バイト
    Vector3 position; // 座標       (float<3> * 3) = 12バイト  
    Vector2 texcoord; // UV座標     (float<2> * 2) = 8バイト
    Vector3 normal;   // 頂点の向き  (float<3> * 3) = 12バイト
};


// PS の b0（112B）
// オブジェクトごとに持つマテリアル情報
struct Material {			        // 全体＜112＞バイト
    Vector4 color;			        // マテリアル色　(float<4> * 4) = 16バイト
    int32_t enableLighting;         // 有効or無効					　　 ４バイト
    SpecularModel specularModel;    // 鏡面反射の計算方式			　　 ４バイト
    float padding[2];		        // hlsl側の自動空白に合わせる　　　 12バイト
    Matrix4x4 uvTransform;          // UV座標　　　　　　　　　　　　　　64バイト
    float shininess;                // 光沢度。大きいほど狭く鋭く光る　　 ４バイト
    Vector3 specularColor;          // 鏡面反射色                    12 バイト
};

// VS の b0（128B）
// 座標変換用の行列（VSに渡す定数）
struct TransformationMatrix { // 全体＜192＞バイト
    Matrix4x4 WVP;    // クリップ空間へ変換する行列                   64 バイト (オフセット  0)
    Matrix4x4 World;  // ワールド空間へ変換する行列（法線の変換に使う）   64 バイト (オフセット 64)
    Matrix4x4 WorldInverseTranspose;  // 法線専用の変換行列           64 バイト (オフセット128)
};

// PS の b2（16B）
// カメラの情報。鏡面反射で「どこから見ているか」を知るために使う
struct CameraForGPU {   // 全体＜16＞バイト
    Vector3 worldPosition;  // カメラのワールド座標　(float<3> * 3) = 12バイト
    float padding;          // 16バイト境界に揃える　　　　　　　　 ４バイト
};



// 平行光源（DirectionalLight） (
struct DirectionalLight {// 全体＜32＞バイト
    Vector4 color;		 // ライト色　　　(float<4> * 4) = 16バイト
    Vector3 direction;   // ライトの向き　(float<3> * 3) = 12バイト
    float intensity;     // 輝度							４バイト
};

// 点光源
struct PointLight {
    Vector4 color;       // 16 (オフセット  0)  光の色
    Vector3 position;    // 12 (オフセット 16)  ワールド座標
    float intensity;     //  4 (オフセット 28)  明るさ
    float radius;        //  4 (オフセット 32)  影響半径。これより遠いと真っ暗
    float decay;         //  4 (オフセット 36)  減衰の鋭さ
    float padding[2];    //  8 (オフセット 40)
};                       // 48 バイト

// スポットライト
struct SpotLight {
    Vector4 color;           // 16 (オフセット  0)  光の色
    Vector3 position;        // 12 (オフセット 16)  ワールド座標
    float intensity;         //  4 (オフセット 28)  明るさ
    Vector3 direction;       // 12 (オフセット 32)  円錐の軸（光が進む向き）
    float distance;          //  4 (オフセット 44)  届く距離
    float decay;             //  4 (オフセット 48)  距離の減衰の鋭さ
    float cosAngle;          //  4 (オフセット 52)  円錐の縁の cos（ここで 0 になる）
    float cosFalloffStart;   //  4 (オフセット 56)  減衰が始まる角度の cos
    float padding;           //  4 (オフセット 60)
};                           // 64 バイト

// PS の b1（16B）
// 各ライトが何個入っているかを伝える
//   StructuredBuffer は「配列の長さ」を自分では持たないので、別途渡す
struct LightCount {        // 全体＜16＞バイト
    uint32_t directionalCount;   // 4
    uint32_t pointCount;         // 4
    uint32_t spotCount;          // 4
    uint32_t padding;            // 4
};
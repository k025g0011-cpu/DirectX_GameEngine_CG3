#pragma once
#include <cstdint>
#include "MathUtility.h"   // ← 実際のファイル名に合わせて修正

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


// PS の b0（96B）
// オブジェクトごとに持つマテリアル情報
struct Material {			// 全体＜96＞バイト
    Vector4 color;			// マテリアル色　(float<4> * 4) = 16バイト
    int32_t enableLighting; // 有効or無効					　　 ４バイト
    float padding[3];		// hlsl側の自動空白に合わせる　　　 12バイト
    Matrix4x4 uvTransform;  // UV座標　　　　　　　　　　　　　　64バイト
};

// VS の b0（128B）
// 座標変換用の行列（VSに渡す定数）
struct TransformationMatrix { // 全体＜128＞バイト
    Matrix4x4 WVP;    // クリップ空間へ変換する行列
    Matrix4x4 World;  // ワールド空間へ変換する行列（法線の変換に使う）
};



// 平行光源（DirectionalLight） (
struct DirectionalLight {// 全体＜32＞バイト
    Vector4 color;		 // ライト色　　　(float<4> * 4) = 16バイト
    Vector3 direction;   // ライトの向き　(float<3> * 3) = 12バイト
    float intensity;     // 輝度							４バイト
};

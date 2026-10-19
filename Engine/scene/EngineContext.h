#pragma once

// 前方宣言（ポインタしか持たないので #include 不要）
class WinApp;
class DirectXCommon;
class TextureManager;
class ModelManager;
class AssetDatabase;
class Object3dPipeline;
class ParticlePipeline;
class PrimitiveDrawer;
class Input;
class GameTime;

// ════════════════════════════════════════════════════════════════════
// エンジンが提供する道具一式
//   シーンに「使っていいもの」をまとめて渡すための箱
//   すべて借り物。シーンは1つも所有しない
//
//   道具が増えたらここにメンバを足すだけでよく、
//   各シーンの Initialize の宣言を書き換える必要がない
// ════════════════════════════════════════════════════════════════════
struct EngineContext {
	WinApp* winApp = nullptr;
	DirectXCommon* dxCommon = nullptr;
	TextureManager* textureManager = nullptr;
	ModelManager* modelManager = nullptr;
	AssetDatabase* assetDatabase = nullptr;
	Object3dPipeline* object3dPipeline = nullptr;
	ParticlePipeline* particlePipeline = nullptr;
	PrimitiveDrawer* primitiveDrawer = nullptr;
	Input* input = nullptr;
	GameTime* gameTime = nullptr;
};
#pragma once

#include "Framework.h"
#include "GameScene.h"

// ════════════════════════════════════════════════════════════════════
// このゲーム
//   Framework が用意した穴を埋めるだけ
//   起動・ループ・終了の順番は Framework が持っているので、ここには無い
// ════════════════════════════════════════════════════════════════════
class MyGame : public Framework {
protected:
	void Initialize() override;
	void Update() override;
	void Draw() override;
	RenderTexture* GetPresentTexture() override;

private:
	//   Framework より後に壊れるものはここに書く
	//     派生のメンバは基底より先に破棄されるので、
	//     エンジンの道具がまだ生きているうちに解放される
	GameScene gameScene_;
};
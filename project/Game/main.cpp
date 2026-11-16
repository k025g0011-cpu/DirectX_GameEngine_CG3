#include <Windows.h>
#include <memory>

#include "MyGame.h"



// ════════════════════════════════════════════════════════════════════
// Windows アプリのエントリーポイント
//   エンジンの道具を用意し、シーンを作り、ループを回すだけ
//   ゲームの内容は GameScene にある
// ════════════════════════════════════════════════════════════════════
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	std::unique_ptr<Framework> game = std::make_unique<MyGame>();

	game->Run();

	return 0;
}
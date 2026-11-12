#include "ImGuiManager.h"

#ifdef USE_IMGUI

#include <cassert>

#include "DirectXCommon.h"
#include "WinApp.h"

#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

#endif

// ════════════════════════════════════════════════════════════════════
// 初期化
// ════════════════════════════════════════════════════════════════════
void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon) {

	dxCommon_ = dxCommon;

#ifdef USE_IMGUI
	assert(winApp != nullptr);
	assert(dxCommon != nullptr);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigWindowsMoveFromTitleBarOnly = true;   // ウィンドウはタイトルバーでのみ移動

	// ────────────────────────────────────────────────────────────────
	// パネルのドッキング（結合・分割・サイズ変更）を許可する
	//   ※ ViewportsEnable（パネルをウィンドウの外へ切り離す機能）は入れない
	//     DX12 ではパネルごとのスワップチェーン管理と
	//     UpdatePlatformWindows / RenderPlatformWindowsDefault の呼び出しが必要になる
	// ────────────────────────────────────────────────────────────────
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui::StyleColorsDark();

	// ────────────────────────────────────────────────────────────────
	// 日本語フォントを読み込む
	//   既定のフォントは ASCII のみ。日本語を出すには別途読む必要がある
	//   用意されるのは ASCII ＋ ひらがな・カタカナ ＋ 常用漢字（約3000字）
	// ────────────────────────────────────────────────────────────────
	ImFont* japaneseFont = io.Fonts->AddFontFromFileTTF(
		"C:/Windows/Fonts/meiryo.ttc",          // メイリオ（Windows 標準）
		18.0f,                                   // ピクセルサイズ
		nullptr,                                 // フォント設定（既定でよい）
		io.Fonts->GetGlyphRangesJapanese());     // 用意する文字の範囲
	assert(japaneseFont != nullptr);

	ImGui_ImplWin32_Init(winApp->GetHwnd());

	// ────────────────────────────────────────────────────────────────
	// DX12 バックエンドの初期化
	//   新しい ImGui は実行中にテクスチャを増減するため、
	//   SRV を起動時に1枠渡すのではなく、必要なときにアプリへ要求してくる
	//     → 確保／解放の関数をこちらから渡す
	// ────────────────────────────────────────────────────────────────
	ImGui_ImplDX12_InitInfo initInfo{};
	initInfo.Device = dxCommon->GetDevice().Get();
	//   テクスチャの転送に使う。新しいバックエンドは自分で GPU へ送るので必須
	initInfo.CommandQueue = dxCommon->GetCommandQueue().Get();
	initInfo.NumFramesInFlight = DirectXCommon::kBackBufferCount;
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // RTV のフォーマットと一致させる
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;               // ImGui は深度を使わない
	initInfo.SrvDescriptorHeap = dxCommon->GetSrvDescriptorHeap().Get();

	//   コールバックに「誰に頼むか」を伝えるための置き場
	//     コールバックは生の関数ポインタなので、this のような文脈を持てない
	initInfo.UserData = dxCommon;

	//   SRV を1枠ください、と言われたときの処理
	initInfo.SrvDescriptorAllocFn =
		[](ImGui_ImplDX12_InitInfo* info,
			D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
			D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle) {

				DirectXCommon* dx = static_cast<DirectXCommon*>(info->UserData);

				const uint32_t index = dx->AllocateSrv();
				*outCpuHandle = dx->GetSrvCpuDescriptorHandle(index);
				*outGpuHandle = dx->GetSrvGpuDescriptorHandle(index);
		};

	//   SRV を返します、と言われたときの処理
	initInfo.SrvDescriptorFreeFn =
		[](ImGui_ImplDX12_InitInfo*,
			D3D12_CPU_DESCRIPTOR_HANDLE,
			D3D12_GPU_DESCRIPTOR_HANDLE) {

				// いまの DirectXCommon は SRV を前から順に配るだけで、返却の仕組みがない
				//   何もしなくても破綻はしない（使い終わった枠が再利用されないだけ）
				//   ※ 実行中にテクスチャを大量に作り直すようになったら、空き管理が必要
		};

	ImGui_ImplDX12_Init(&initInfo);
#else
	//   USE_IMGUI が無いビルドでは winApp を使わない
	(void)winApp;
#endif
}

// ════════════════════════════════════════════════════════════════════
// 終了処理
// ════════════════════════════════════════════════════════════════════
void ImGuiManager::Finalize() {
#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif
}

// ════════════════════════════════════════════════════════════════════
// このフレームの ImGui を始める
// ════════════════════════════════════════════════════════════════════
void ImGuiManager::Begin() {
#ifdef USE_IMGUI
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
#endif
}

// ════════════════════════════════════════════════════════════════════
// 内部コマンドの生成
//   描画より前に呼ぶ。これ以降 ImGui::Begin などを呼んではいけない
// ════════════════════════════════════════════════════════════════════
void ImGuiManager::End() {
#ifdef USE_IMGUI
	ImGui::Render();
#endif
}

// ════════════════════════════════════════════════════════════════════
// 描画コマンドを積む
//   ImGui もバックバッファに書き込むので、PostDraw のバリアより前
// ════════════════════════════════════════════════════════════════════
void ImGuiManager::Draw() {
#ifdef USE_IMGUI
	ImGui_ImplDX12_RenderDrawData(
		ImGui::GetDrawData(), dxCommon_->GetCommandList().Get());
#endif
}

// ════════════════════════════════════════════════════════════════════
// 文字入力中か
//   ImGui が無いビルドでは、常に false（入力は誰にも横取りされない）
// ════════════════════════════════════════════════════════════════════
bool ImGuiManager::IsTextInputActive() const {
#ifdef USE_IMGUI
	return ImGui::GetIO().WantTextInput;
#else
	return false;
#endif
}
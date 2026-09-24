#include <Windows.h>
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream> 
#include <chrono>
#include <cmath> 
#include <cstring> 
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxcapi.h>
#include <vector>
#include <numbers>
#include "MathUtility.h"
#include "Input.h"
// imgui用のincludeと外部関数
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
#include "externals/DirectXTex/DirectXTex.h" //　テクスチャを扱う
#include "externals/DirectXTex/d3dx12.h"

// Debug用のあれやこれやを使えるようにする
#include <dbghelp.h>
// StringCchPrintfWを使うため
#include <strsafe.h>

// リソースリークチェック用
#include <dxgidebug.h>
// ComPtr（COMオブジェクトの参照カウントを自動で管理してくれるクラス）用
#include <wrl.h>

// キーボード入力用（DirectInput）
// ※ DIRECTINPUT_VERSION は必ず dinput.h より前に定義すること
#define DIRECTINPUT_VERSION 0x0800   // DirectInput 8.00 を使う
#include <dinput.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dinput8.lib")   

// ════════════════════════════════════════
// Microsoft::WRL::ComPtr は毎回書くと長いので、ComPtr と短縮して使えるようにする
// ※ using namespace ではなく、必要な名前だけを引き込むこと（名前衝突を防ぐため）
// ════════════════════════════════════════
using Microsoft::WRL::ComPtr;

// ════════════════════════════════════════
// D3DResourceLeakChecker
// WinMainの先頭でインスタンスを作ることで「最初に構築されたものは最後に破棄される」
// というC++の性質を利用し、すべてのComPtrが解放された後にリークチェックを行う
// ════════════════════════════════════════
struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker()
	{
		// 解放し忘れたオブジェクトが残っていないか、DXGIに問い合わせる
		ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};


// 1頂点の持つデータ
struct VertexData {	  // 全体＜36＞バイト
	Vector4 position; // 座標       (float<4> * 4) = 16バイト
	Vector2 texcoord; // UV座標     (float<2> * 2) = 8バイト
	Vector3 normal;   // 頂点の向き  (float<3> * 3) = 12バイト
};

// ════════════════════════════════════════
// MaterialData構造体
// mtlファイルから読み込んだマテリアル情報
// ※ 今はテクスチャのパスだけ。将来 Kd(色) や法線マップのパスが増える
// ════════════════════════════════════════
struct MaterialData {
	std::string textureFilePath;   // 使用するテクスチャの相対パス（"resources/uvChecker.png" の形）
};

// ════════════════════════════════════════
// ModelData構造体
// objファイルやプロシージャル生成で作ったモデルの頂点データ
// indices が空 → インデックスを使わない描画 (DrawInstanced)
// indices が有 → インデックスを使う描画   (DrawIndexedInstanced)
// ════════════════════════════════════════
struct ModelData {
	std::vector<VertexData> vertices;   // 頂点データ
	std::vector<uint32_t>   indices;    // インデックス（空なら未使用）
	MaterialData material;
};

// 座標変換用の行列（VSに渡す定数）
struct TransformationMatrix { // 全体＜128＞バイト
	Matrix4x4 WVP;    // クリップ空間へ変換する行列
	Matrix4x4 World;  // ワールド空間へ変換する行列（法線の変換に使う）
};


// オブジェクトごとに持つマテリアル情報
struct Material {			// 全体＜96＞バイト
	Vector4 color;			// マテリアル色　(float<4> * 4) = 16バイト
	int32_t enableLighting; // 有効or無効					　　 ４バイト
	float padding[3];		// hlsl側の自動空白に合わせる　　　 12バイト
	Matrix4x4 uvTransform;  // UV座標　　　　　　　　　　　　　　64バイト
};

// 平行光源（DirectionalLight） (
struct DirectionalLight {// 全体＜32＞バイト
	Vector4 color;		 // ライト色　　　(float<4> * 4) = 16バイト
	Vector3 direction;   // ライトの向き　(float<3> * 3) = 12バイト
	float intensity;     // 輝度							４バイト
};



// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
	WPARAM wparam, LPARAM lparam) {

	// ────────────────────────────────────────────────────────
	// ImGui 用のメッセージハンドリング
	// ────────────────────────────────────────────────────────
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
#endif

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		// ウィンドウが破棄された
	case WM_DESTROY:
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}


// string -> wstring
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

// wstring -> string
std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

// ostreamを引数に追加して、ファイルへ書き出すように変更
void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA((message + "\n").c_str());
}

// wstring用のLog関数もostreamを受け取るように変更
void Log(std::ostream& os, const std::wstring& message) {
	// wstringからstringに変換して、
	// stringの方のLog関数を呼ぶ
	Log(os, ConvertString(message));
}

//════════════════════════════════════════════════════
// キー入力判定ヘルパー
//   key    : 今フレームの状態
//   preKey : 前フレームの状態
//   keyNumber : DIK_〜 (uint8_tなので0〜255に制限される)
//════════════════════════════════════════════════════

// 押している状態か
bool PushKey(const BYTE* key, uint8_t keyNumber) {
	return key[keyNumber] != 0;
}

// 離している状態か
bool ReleaseKey(const BYTE* key, uint8_t keyNumber) {
	return key[keyNumber] == 0;
}

// 押した瞬間か（前:OFF → 今:ON）
bool TriggerKey(const BYTE* key, const BYTE* preKey, uint8_t keyNumber) {
	return key[keyNumber] != 0 && preKey[keyNumber] == 0;
}

// 離した瞬間か（前:ON → 今:OFF）
bool ReleaseTriggerKey(const BYTE* key, const BYTE* preKey, uint8_t keyNumber) {
	return key[keyNumber] == 0 && preKey[keyNumber] != 0;
}

// ════════════════════════════════════════
// CrashHandler
// 誰もSEHの例外を捕捉しなかった場合に実行され、ダンプファイルを出力する
// ════════════════════════════════════════
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
	// 時刻を取得して、時刻を名前に入れたファイルを作成。Dumpsディレクトリ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	// processId(このexeのId)とクラッシュ(例外)の発生したthreadIdを取得
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	// 設定情報を入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;
	// Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle,
		MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
	// 他に関連づけられているSEH例外ハンドラがあれば実行。通常はプロセスを終了する
	return EXCEPTION_EXECUTE_HANDLER;
}

// ════════════════════════════════════════
// CompileShader関数
// hlslファイルを読み、DXCでコンパイルし、GPUが実行できるバイナリ(Blob)を返す　（GPUが読めるテキスト形式　＝　バイナリ）
// ════════════════════════════════════════
ComPtr<IDxcBlob> CompileShader(
	// CompilerするShaderファイルへのパス
	const std::wstring& filePath,
	// Compilerに使用するProfile
	const wchar_t* profile,
	// 初期化で生成したものを3つ
	const ComPtr<IDxcUtils>& dxcUtils,
	const ComPtr<IDxcCompiler3>& dxcCompiler,
	const ComPtr<IDxcIncludeHandler>& includeHandler,
	// ログ出力先(スライドにはない。自作Logに合わせて追加)
	std::ostream& os) {

	// ────────────────────────────────────────
	// 1. hlslファイルを読む
	// ────────────────────────────────────────
	// これからシェーダーをコンパイルする旨をログに出す
	Log(os, std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile));
	// hlslファイルを読む
	ComPtr<IDxcBlobEncoding> shaderSource = nullptr; // テキストの入れ物を指すアドレス
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource); // テキストを箱に入れる＆テキストの箱を指すアドレスを格納する
	// 読めなかったら止める
	assert(SUCCEEDED(hr));
	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer; // コンパイラに渡す伝票(テキストの場所・サイズ・文字コード)。中身は次の3行で埋める
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8; // UTF8の文字コードであることを通知

	// ────────────────────────────────────────
	// 2. Compileする
	// ────────────────────────────────────────
	LPCWSTR arguments[] = {
		filePath.c_str(),           // コンパイル対象のhlslファイル名
		L"-E", L"main",             // エントリーポイントの指定。基本的にmain以外にはしない
		L"-T", profile,             // ShaderProfileの設定
		L"-Zi", L"-Qembed_debug",   // デバッグ用の情報を埋め込む
		L"-Od",                     // 最適化を外しておく
		L"-Zpr",                    // メモリレイアウトは行優先
	};
	// 実際にShaderをコンパイルする
	ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,        // 読み込んだファイル
		arguments,                  // コンパイルオプション
		_countof(arguments),        // コンパイルオプションの数
		includeHandler.Get(),       // includeが含まれた諸々（ComPtrから生ポインタを借りる）
		IID_PPV_ARGS(&shaderResult) // コンパイル結果の箱(エラー文とバイナリの詰め合わせ)のアドレスをshaderResultに格納
	);
	// コンパイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	// ────────────────────────────────────────
	// 3. 警告・エラーがでていないか確認する
	// ────────────────────────────────────────
	ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr); // コンパイル結果からエラー情報部分を取得
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(os, shaderError->GetStringPointer());
		// 警告・エラーダメゼッタイ
		assert(false);
	}

	// ────────────────────────────────────────
	// 4. Compile結果を受け取って返す
	// ────────────────────────────────────────
	ComPtr<IDxcBlob> shaderBlob = nullptr; // 翻訳後のバイナリを管理する入れ物(Blob)のアドレスを格納
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr); // 結果の箱から実行用バイナリの入れ物を取り出す
	assert(SUCCEEDED(hr));
	// 成功したログを出す
	Log(os, std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile));
	// もう使わないリソース（shaderSource / shaderResult / shaderError）は
	// ComPtrがこの関数を抜けるときに自動で解放するので、Releaseを書く必要はない
	// 実行用のバイナリを返却
	return shaderBlob;
}

// ════════════════════════════════════════
// CreateBufferResource関数
// VertexBufferやConstantBufferなど、BufferResourceを作る共通処理
// ════════════════════════════════════════
ComPtr<ID3D12Resource> CreateBufferResource(const ComPtr<ID3D12Device>& device, size_t sizeInBytes) {
	// 頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // UploadHeapを使う
	// リソースの設定
	D3D12_RESOURCE_DESC bufferResourceDesc{};
	// バッファリソース。テクスチャの場合はまた別の設定をする
	bufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferResourceDesc.Width = sizeInBytes; // リソースのサイズ
	// バッファの場合はこれらは1にする決まり
	bufferResourceDesc.Height = 1;
	bufferResourceDesc.DepthOrArraySize = 1;
	bufferResourceDesc.MipLevels = 1;
	bufferResourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	bufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	// 実際にリソースを作る
	ComPtr<ID3D12Resource> bufferResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&bufferResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&bufferResource));
	assert(SUCCEEDED(hr));
	return bufferResource;
}

// ════════════════════════════════════════
// CreateDescriptorHeap関数
// View（リソースの説明書）を格納するディスクリプターヒープを作る関数
// ════════════════════════════════════════
ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
	const ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
	ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}

// ════════════════════════════════════════
// GetCPUDescriptorHandle / GetGPUDescriptorHandle関数
// DescriptorHeapの先頭から index 個分進めたハンドルを返す
// descriptorSize : そのヒープのタイプのディスクリプタ1個分のバイト数
// ════════════════════════════════════════
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

// ════════════════════════════════════════
// LoadTexture関数
// ════════════════════════════════════════
DirectX::ScratchImage LoadTexture(const std::string& filePath)
{
	// テクスチャファイルを読んでプログラムで扱えるようにする
	DirectX::ScratchImage scratchImage{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, scratchImage);
	/*
	＜＜LoadFromWICFile(...,  scratchImage) の後＞＞

	ScratchImage image
			├── m_nimages  = 1
			├── m_size     = 1048576（512×512×4）
			├── m_metadata = width=512, height=512, mipLevels=1, format=..._SRGB
			├── m_image ─────▶ Image構造体の配列（1個だけ）
			│                  └─ [0] {512×512, rowPitch=2048, pixels ─┐}
			└── m_memory ────▶ ピクセル本体（約1MB） ◀─────────────────┘
							   [RGBA][RGBA][RGBA] ...

	＜＜LoadFromWICFileの中で起きている順番＞＞
			① PNGファイルのヘッダーを読む
				  → 「512×512、RGBA、8bit」とわかる
			② m_metadata に書き込む
				  （FORCE_SRGBなので format = R8G8B8A8_UNORM_SRGB）
			③ 必要なメモリを確保する
				  ・Image構造体の配列（1個分）      → m_image
				  ・ピクセル本体（512×512×4バイト） → m_memory
			④ Image[0] の案内板を埋める
				  width=512, height=512, rowPitch=2048, pixels = m_memory の先頭
			⑤ PNGの圧縮を展開して、ピクセル本体に書き込む
				  [RGBA][RGBA]...
	*/
	assert(SUCCEEDED(hr));

	// ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	/*
	scratchImageのデータを　mipImages　にコピーしつつ、
	データを見ながらmipImage（512x512なら10枚）を作り、
	mipImages　の中にある m_memory　に１0枚分の場所を確保し、格納。
	それに対応した数（１０）のアドレスをimage配列に格納（サイズなどの情報もセット）

	出来たmipImagesを返す
	*/
	assert(SUCCEEDED(hr));

	// ミップマップ付きのデータを返す
	return mipImages;
	/*
	【返すデータ：mipImages（DirectX::ScratchImage）】
	CPU側のメインメモリ(RAM)にある、ミップマップ付きテクスチャの入れ物
	※ ScratchImageはコピー不可のため、returnではムーブ（中身の持ち主だけが呼び出し元へ移る）
	※ COMではないのでReleaseは不要。変数が破棄されるとデストラクタが自動でメモリを解放する

	■ 中身（例：512x512の画像）
	mipImages
	├── m_metadata（全体の情報）……… GetMetadata() で取得
	│     width=512, height=512, depth=1, arraySize=1, mipLevels=10
	│     format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, dimension=TEX_DIMENSION_TEXTURE2D
	│
	├── m_image（Image構造体の配列：10個）……… GetImages() / GetImage(mip, item, slice) で取得
	│     [0] {width=512, height=512, format, rowPitch=2048, slicePitch=1048576, pixels=●}
	│     [1] {width=256, height=256, format, rowPitch=1024, slicePitch=262144,  pixels=●}
	│     ...
	│     [9] {width=1,   height=1,   format, rowPitch=4,    slicePitch=4,       pixels=●}
	│       rowPitch  : 1行のバイト数（幅 × 4バイト）
	│       slicePitch: 1枚のバイト数（rowPitch × 高さ）
	│       pixels    : m_memory内の、その段のピクセルの開始アドレス
	│
	└── m_memory（ピクセル本体）……… 全段が1本の連続したメモリに並んでいる（元画像の約1.33倍）
		  [512x512分][256x256分][128x128分]...[1x1分]
		  1ピクセル = [R G B A] 各1バイト（sRGB）

	■ この後の使われ方
	・metadata                      → CreateTextureResource : GPU側のテクスチャ(空の箱)の設定
															   （幅・高さ・ミップ数・フォーマット・次元）
									→ SRVの作成              : Format / MipLevels の指定
	・Image（各段の pixels / rowPitch / slicePitch）
									→ UploadTextureData      : ミップ1段ずつ、GPU側のテクスチャへ転送
															   （WriteToSubresource に渡す）
	・転送が終わったら、CPU側のこのデータは不要になる
	*/
}

// ════════════════════════════════════════
// CreateTextureResource関数
// TexMetadataの情報をもとに、GPU側にテクスチャ用のResource(空の箱)を作る
// ※ 中身はまだ空。データの転送は UploadTextureData で行う
// ════════════════════════════════════════
ComPtr<ID3D12Resource> CreateTextureResource(const ComPtr<ID3D12Device>& device, const DirectX::TexMetadata& metadata)
{
	// metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width); // Textureの幅
	resourceDesc.Height = UINT(metadata.height); // Textureの高さ
	resourceDesc.MipLevels = UINT16(metadata.mipLevels); // mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize); // 奥行き or 配列Textureの配列数
	resourceDesc.Format = metadata.format; // TextureのFormat
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textureの次元数。普段使っているのは2次元

	// 2. 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;   // VRAM上に作る


	// 3. Resourceを生成する
	ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,                  // Heapの設定
		D3D12_HEAP_FLAG_NONE,             // Heapの特殊な設定。特になし。
		&resourceDesc,                    // Resourceの設定
		D3D12_RESOURCE_STATE_COPY_DEST,   // データコピーを受け入れる設定
		nullptr,                          // Clear最適値。使わないのでnullptr
		IID_PPV_ARGS(&resource));         // 作成するResourceポインタの格納先
	assert(SUCCEEDED(hr));
	return resource;
}


[[nodiscard]]
ComPtr<ID3D12Resource> UploadTextureData(const ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages, const ComPtr<ID3D12Device>& device, const ComPtr<ID3D12GraphicsCommandList>& commandList)
{
	/*
		＜部分＞                   ＜意味＞
		[[nodiscard]]	         「戻り値を捨てたら警告を出して」というC++の指定。中間リソースを受け取り忘れると、Releaseできずにリークするので、それを防ぐ
		戻り値 ComPtr<ID3D12Resource>  作った中間リソース。void から変わった
		device（追加）	          中間リソースを作るのに必要
		commandList（追加）	      コピー命令とバリアを積むのに必要
	*/
	// 読み込んだ画像データからDirectXTexの形式で作った複数のmipImagesの配列を、
	// DX12が読める形式に書き換え格納する変数（D3D12_SUBRESOURCE_DATAの型にする）
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources); // DX12が読める形式に書き換えてsubresourcesに格納

	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size())); // テクスチャのサブリソースなどから格納に必要なサイズを図り、中間リソースのサイズを決める
	ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(device, intermediateSize); // 格納に必要なサイズを渡し、GPUへの通り道のリソースを作成（中間リソース）

	UpdateSubresources(commandList.Get(), texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data()); //「指定したコピー先へテクスチャデータのコピーしろ」と命令を積む
	/*
   ＜引数＞	＜値＞                       ＜意味＞
	 ①	    commandList	                コピー命令を積む先
	 ②	    texture	                    コピー先（VRAM）
	 ③	    intermediateResource        中継地点（UPLOAD）
	 ④	    0	                        中間リソースの何バイト目から使うか（先頭から）
	 ⑤	    0	                        テクスチャの何番目のサブリソースから
	 ⑥	    UINT(subresources.size())	何個分（10）
	 ⑦	    subresources.data()	        PrepareUploadで作った案内板の配列の先頭アドレス
	*/

	// Tetureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceStateを変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;   // コピー受け入れのリソースから
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ; // 読むリソースへ遷移させる命令
	commandList->ResourceBarrier(1, &barrier);
	return intermediateResource;
}


// ════════════════════════════════════════
// CreateDepthStencilTextureResource関数
// 奥行きを記録するための深度バッファ(DepthStencilTexture)をVRAM上に作る
// width / height : 画面(RTV)と同じサイズにする
// ════════════════════════════════════════
ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const ComPtr<ID3D12Device>& device, int32_t width, int32_t height)
{
	// 1. 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width; // Textureの幅
	resourceDesc.Height = height; // Textureの高さ
	resourceDesc.MipLevels = 1; // mipmapの数
	resourceDesc.DepthOrArraySize = 1; // 奥行き or 配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

	// 2. 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	// 3. 深度値のクリア最適化設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f; // 1.0f（最大値）でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

	// 4. Resourceの生成
	ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, // Heapの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし。
		&resourceDesc,  // Resourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
		&depthClearValue, // Clear最適値
		IID_PPV_ARGS(&resource)); // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));

	return resource;
}

// ════════════════════════════════════════
// LoadMaterialTemplateFile関数
// mtlファイル(テキスト)を読み、使用するテクスチャのパスを取り出す
// directoryPath : ファイルが置いてあるフォルダ (例 "resources")
// filename      : mtlのファイル名             (例 "plane.mtl")
// ════════════════════════════════════════
MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
	// ────────────────────────────────────────
	// 1. 必要な変数の宣言
	// ────────────────────────────────────────
	MaterialData materialData;   // 構築して返す完成品
	std::string line;            // ファイルから読んだ1行を格納するもの

	// ────────────────────────────────────────
	// 2. ファイルを開く
	// ────────────────────────────────────────
	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());   // 開けなかったら止める(objと同じ階層にmtlが無い等)

	// ────────────────────────────────────────
	// 3. ファイルを読み、MaterialDataを構築していく
	// ────────────────────────────────────────
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		// ─── 拡散色に使うテクスチャ ───
		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;
			// mtlにはファイル名しか書かれていないので、ディレクトリ名と連結してパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
		// 上記以外( newmtl / Kd / Ks / Ns / illum など )は今は無視する
	}

	// ────────────────────────────────────────
	// 4. MaterialDataを返す
	// ────────────────────────────────────────
	return materialData;
}

// ════════════════════════════════════════
// LoadObjFile関数
// objファイル(テキスト)を1行ずつ読み、VertexDataの配列(ModelData)を構築する
// directoryPath : ファイルが置いてあるフォルダ (例 "resources")
// filename      : ファイル名                  (例 "plane.obj")
// ════════════════════════════════════════
ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename)
{
	// ────────────────────────────────────────
	// 1. 必要な変数の宣言
	// ────────────────────────────────────────
	ModelData modelData;                 // 構築して返す完成品
	std::vector<Vector4> positions;      // objの v  を溜める作業用配列
	std::vector<Vector3> normals;        // objの vn を溜める作業用配列
	std::vector<Vector2> texcoords;      // objの vt を溜める作業用配列
	std::string line;                    // ファイルから読んだ1行を格納するもの

	// ────────────────────────────────────────
	// 2. ファイルを開く
	// ifstreamはデストラクタで自動的に閉じるのでcloseは不要
	// ────────────────────────────────────────
	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());   // 開けなかったら止める(パス間違い・作業ディレクトリ違いが原因)

	// ────────────────────────────────────────
	// 3. ファイルを読み、ModelDataを構築していく
	// ────────────────────────────────────────
	while (std::getline(file, line)) {          // ファイルの終端まで1行ずつ読む
		std::string identifier;                  // 行の先頭の識別子(v / vt / vn / f など)
		std::istringstream s(line);              // 1行を空白区切りで読むためのstream
		s >> identifier;                         // 先頭の識別子を読む

		// ─── 頂点位置 ───
		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;                   // VSには同次座標で送るのでwは1
			positions.push_back(position);

			// ─── テクスチャ座標 ───
		} else if (identifier == "vt") {
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			// ════════════════════════════════════════
			// 左下原点Texture座標系(obj) → 左上原点Texture座標系(DirectX)
			// uは両方とも右向きなので変換不要。vの向きだけが逆なので反転する
			// 0〜1の範囲なので 1-v で真ん中を軸に折り返せばよい
			// ※ 3D座標系(手系)の変換とはまったく別の話。positionのyは触らない
			// ════════════════════════════════════════
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);

			// ─── 法線 ───
		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);

			// ─── 面(三角形) ───
		} else if (identifier == "f") {

			// 右手系→左手系の変換で回り順を逆にするため、一度ここに溜めてから登録する
			VertexData triangle[3];

			// 面は三角形限定。四角形以上のポリゴンは未対応
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition;    // "2/1/1" のような1頂点ぶんの定義
				s >> vertexDefinition;

				// 「位置/UV/法線」の3つのIndexを '/' 区切りで分解する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for (int32_t element = 0; element < 3; ++element) {
					std::string index;
					std::getline(v, index, '/'); // 第3引数で区切り文字を'/'に指定
					elementIndices[element] = std::stoi(index);  // 文字列→整数
				}

				// Indexから実際の値を取り出して1頂点を組み立てる
				// objのIndexは1始まりなので、配列の添字にするときは -1 する
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];


				// ════════════════════════════════════════
				// 右手座標系(obj) → 左手座標系(DirectX) へ変換 ①
				// 手系を変えるには軸を1本だけ反転すればよい（x/y/zどれでも数学的には成立する）
				// 授業の指定どおり x を反転する。この選び方では
				// 「Blenderで見た向きの真後ろから見た状態」になるため、
				// plane.objのような片面モデルは初期状態で裏面を向く（y軸180度回して確認する）
				// 位置だけでなく法線も反転しないとライティングが壊れる
				// ════════════════════════════════════════
				position.x *= -1.0f;
				normal.x *= -1.0f;

				// この場ではpush_backせず、一時配列に溜める
				triangle[faceVertex] = { position, texcoord, normal };
			}

			// ════════════════════════════════════════
			// 右手座標系(obj) → 左手座標系(DirectX) へ変換 ②
			// x反転で鏡像になり表裏が逆転するので、頂点の回り順も逆にして打ち消す
			// 右手系の表は反時計回り、左手系の表は時計回り
			// ════════════════════════════════════════
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);


		} else if (identifier == "mtllib") {
			// materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;
			// 基本的にobjファイルと同一階層にmtlは存在させるので、ディレクトリ名とファイル名を渡す
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
		// 上記以外の識別子( # / o / s / usemtl )は今は無視する
	}

	// ────────────────────────────────────────
	// 4. ModelDataを返す
	// ※ 中身はstd::vectorなのでムーブされる。Releaseもdeleteも不要
	// ────────────────────────────────────────
	return modelData;
}


// ════════════════════════════════════════
// 円周率
// ════════════════════════════════════════
const float kPi = 3.14159265358979323846f;

// ════════════════════════════════════════
// CreateSphereModelData関数
// 緯度経度で分割した球をプロシージャル生成する
// kSubdivision : 分割数。頂点は (n+1)^2 個、インデックスは n*n*6 個になる
// ════════════════════════════════════════
ModelData CreateSphereModelData(uint32_t kSubdivision)
{
	ModelData modelData;

	// プロシージャル生成なのでmtlファイルが存在しない。
	// LoadTexture(modelData.material.textureFilePath) が空文字列で落ちないよう、既定のテクスチャを指定しておく
	modelData.material.textureFilePath = "resources/uvChecker.png";

	const uint32_t vertexCount = (kSubdivision + 1) * (kSubdivision + 1);  // 格子点の数
	const uint32_t indexCount = kSubdivision * kSubdivision * 6;          // マス数×6
	modelData.vertices.resize(vertexCount);   // 先に必要な数だけ確保しておく
	modelData.indices.resize(indexCount);

	const float kLonEvery = 2.0f * kPi / float(kSubdivision);   // 経度1つ分の角度 φd
	const float kLatEvery = kPi / float(kSubdivision);          // 緯度1つ分の角度 θd

	// ────────────────────────────────────────
	// 頂点データ（格子点を1個ずつ置いていく）
	// ────────────────────────────────────────
	// 緯度方向に分割　-π/2 ～ π/2
	for (uint32_t latIndex = 0; latIndex <= kSubdivision; ++latIndex) {       // <= （端の点も作る）
		float lat = -kPi / 2.0f + kLatEvery * float(latIndex);                // 現在の緯度 θ

		// 経度方向に分割　0 ～ 2π
		for (uint32_t lonIndex = 0; lonIndex <= kSubdivision; ++lonIndex) {   // <= （一周を閉じる）
			float lon = kLonEvery * float(lonIndex);                          // 現在の経度 φ

			// この格子点の番号。横に (kSubdivision+1) 個ならんでいる
			uint32_t index = latIndex * (kSubdivision + 1) + lonIndex;

			// 単位球（半径1）上の座標
			modelData.vertices[index].position.x = std::cos(lat) * std::cos(lon);
			modelData.vertices[index].position.y = std::sin(lat);
			modelData.vertices[index].position.z = std::cos(lat) * std::sin(lon);
			modelData.vertices[index].position.w = 1.0f;

			// uv。uは経度が一周で0→1、vは緯度が下から上で1→0
			modelData.vertices[index].texcoord.x = float(lonIndex) / float(kSubdivision);
			modelData.vertices[index].texcoord.y = 1.0f - float(latIndex) / float(kSubdivision);

			// 単位球なので中心から表面へのベクトル＝位置。それがそのまま法線になる
			modelData.vertices[index].normal.x = modelData.vertices[index].position.x;
			modelData.vertices[index].normal.y = modelData.vertices[index].position.y;
			modelData.vertices[index].normal.z = modelData.vertices[index].position.z;
		}
	}

	// ────────────────────────────────────────
	// インデックスデータ（マスごとに6個）
	// ────────────────────────────────────────
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {        // < （マスは16個）
		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {    // < （マスは16個）

			// このマスが書き込むインデックス配列の開始位置（1マスにつき6個）
			uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;

			// このマスの四隅の「格子点の番号」
			uint32_t a = (latIndex + 0) * (kSubdivision + 1) + (lonIndex + 0);   // 左下
			uint32_t b = (latIndex + 1) * (kSubdivision + 1) + (lonIndex + 0);   // 左上
			uint32_t c = (latIndex + 0) * (kSubdivision + 1) + (lonIndex + 1);   // 右下
			uint32_t d = (latIndex + 1) * (kSubdivision + 1) + (lonIndex + 1);   // 右上

			// 三角形1枚目　a → b → c
			modelData.indices[start + 0] = a;
			modelData.indices[start + 1] = b;
			modelData.indices[start + 2] = c;
			// 三角形2枚目　c → b → d
			modelData.indices[start + 3] = c;
			modelData.indices[start + 4] = b;
			modelData.indices[start + 5] = d;
		}
	}

	return modelData;
}

// Windowsアプリでのエントリーポイント（main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// ════════════════════════════════════════
	// リークチェッカー
	// 一番最初に作る＝デストラクタが一番最後に呼ばれるので、
	// すべてのComPtrが解放された後にリークチェックが走る
	// ════════════════════════════════════════
	D3DResourceLeakChecker leakCheck;

	// COMの初期化（WinMainの一番最初）
	CoInitializeEx(0, COINIT_MULTITHREADED);


	// 誰も捕捉しなかった場合に(Unhandled)、捕捉する関数を登録
	// main関数がはじまってすぐに登録すると良い
	SetUnhandledExceptionFilter(ExportDump);

	// ログのディレクトリを用意 (image_247d6d)
	std::filesystem::create_directory("logs");

	// 現在時刻を取得 (UTC時刻) (image_247d8c)
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	// ログファイルの名前にコンマ何秒はいらないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	// 日本時間 (PCの設定時間) に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	// formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	// 時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";
	// ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);


	WNDCLASS wc{};
	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名(なんでも良い)
	wc.lpszClassName = L"CG2WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);

	// クライアント領域
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	// ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	// クライアント領域を元に実際のサイズにwrcをを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,      // 利用するクラス名
		L"CG2",                // タイトルバーの文字（何でも良い）
		WS_OVERLAPPEDWINDOW,   // よく見るウィンドウスタイル
		CW_USEDEFAULT,         // 表示X座標 (Windowsに任せる)
		CW_USEDEFAULT,         // 表示Y座標 (WindowsOSに任せる)
		wrc.right - wrc.left,  // ウィンドウ横幅
		wrc.bottom - wrc.top,  // ウィンドウ縦幅
		nullptr,               // 親ウィンドウハンドル
		nullptr,               // メニューハンドル
		wc.hInstance,          // インスタンスハンドル
		nullptr);              // オプション

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	// 新しく作ったLog関数を使ってファイル出力とデバッグ出力を同時に行う
	Log(logStream, "hello DirectX");

	// ════════════════════════════════════════
	// DXGIファクトリーの生成
	// ════════════════════════════════════════
	ComPtr<IDXGIFactory7> dxgiFactory = nullptr; // DXGIFactoryのアドレスを入れる変数
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory)); // 作ったファクトリーのアドレスをdxgiFactoryに格納（本体はメインメモリに生成）

	assert(SUCCEEDED(hr)); //SUCCEEDED(0)であれば正常　(上の生成が成功したらhrに０が入る）


	// ════════════════════════════════════════
	// 使用するアダプタ(GPU)を決定する
	// ════════════════════════════════════════

	// 使用するアダプタのアドレス格納用変数
	ComPtr<IDXGIAdapter4> useAdapter = nullptr;

	// 高性能な順にアダプタを頼む
	for (UINT i = 0;
		dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; //　パソコン搭載のGPUをハイスペック順に仮にuseAdapterに入れて調べる
		++i) {

		// アダプタの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr)); // 取得できないのは一大事
		// ソフトウェアアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			// 採用したアダプタの情報をログに出力。wstringの方なので注意
			Log(logStream, std::format(L"Use Adapter:{}", adapterDesc.Description));
			break;
		}
		// ソフトウェアアダプタは手放して次へ(ComPtrにnullptrを入れると内部でReleaseされる)
		useAdapter = nullptr;
	}
	// 適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);


	// ════════════════════════════════════════
	// デバッグレイヤーの有効化
	// 必ずDevice生成より前に行う(後からでは適用されない)
	// ════════════════════════════════════════
#ifdef _DEBUG
	ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	// ════════════════════════════════════════
	// D3D12Deviceの生成
	// ════════════════════════════════════════
	// 生成したデバイスのアドレス格納用の変数(
	ComPtr<ID3D12Device> device = nullptr;

	// 機能レベルとログ出力用の文字列(同じ番号同士が対応する)
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		// 採用したアダプタでデバイスを生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device)); //　deviceに生成されたデバイスのアドレスが格納されるぞ！！(本体はメインメモリ上)
		// 指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr)) {
			// 生成できたのでログ出力を行ってループを抜ける
			Log(logStream, std::format("FeatureLevel : {}", featureLevelStrings[i]));
			break;
		}
	}
	// デバイスの生成がうまくいかなかったので起動できない
	assert(device != nullptr);
	// 初期化完了のログを出す
	Log(logStream, "Complete create D3D12Device!!!");

	// ════════════════════════════════════════
	// エラー・警告が出たら即停止させる
	// DirectX12のエラーはコンパイル時に分からないので、実行時に止めて気づけるようにする
	// ════════════════════════════════════════
#ifdef _DEBUG
	ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// ヤバイエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// 警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// ────────────────────────────────────────
		// エラーと警告の抑制
		// どうにもならないものだけをここに入れる。まずは対処を試みること
		// ────────────────────────────────────────
		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};
		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);

		// 解放はComPtrが自動で行う
	}
#endif

	// ════════════════════════════════════════
	// コマンドキューを生成する
	// 命令群(CommandList)をGPUに投げて実行させる担当
	// ════════════════════════════════════════
	ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	// 設定用の構造体。今は既定値(DIRECT)のままでよいので中身は触らない
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue)); // (１．設定を書き込む構造体、２．生成したコマンドキューのアドレスを入れる変数）
	// コマンドキューの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));


	// ════════════════════════════════════════
	// コマンドアロケータを生成する
	// 命令を書き溜めておくメモリそのものを管理する担当
	// ════════════════════════════════════════
	ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)); // (１．生成したアロケータのアドレスを入れる変数）
	// コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));


	// ════════════════════════════════════════
	// コマンドリストを生成する
	// アロケータのメモリに命令を書き込んでいく担当
	// ════════════════════════════════════════
	ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	hr = device->CreateCommandList(
		0,                                   // 
		D3D12_COMMAND_LIST_TYPE_DIRECT,      // アロケーターと同じ設定にする
		commandAllocator.Get(),              // どのノートに書くか。紐付け   
		nullptr,                             // 初期パイプラインステート、後々設定していく   
		IID_PPV_ARGS(&commandList));         // 生成したコマンドリストのアドレスを入れる変数

	// コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// ════════════════════════════════════════
   // スワップチェーンを生成する
   // 描いた絵をモニタに映すための「表と裏の画面」を用意する担当
   // ════════════════════════════════════════
	ComPtr<IDXGISwapChain4> swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;                            // 画面の幅。ウィンドウのクライアント領域と同じものにしておく
	swapChainDesc.Height = kClientHeight;                          // 画面の高さ。ウィンドウのクライアント領域と同じものにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;             // 色の形式
	swapChainDesc.SampleDesc.Count = 1;                            // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;   // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2;                                 // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;      // モニタにうつしたら、中身を破棄
	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.Get(), // どのキューの描画完了を待つか
		hwnd,              // どのウィンドウに映すか
		&swapChainDesc,    // スワップチェーン設定書
		nullptr,           // フルスクリーン設定(既定通りでいい）
		nullptr,           // 出力先モニタ設定(既定通りでいい）
		reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf())); // ComPtrが持つポインタのアドレスが必要なのでGetAddressOf
	assert(SUCCEEDED(hr));

	// ════════════════════════════════════════
	// ディスクリプタヒープの生成
	// 「このリソースをこう使う」という札(Descriptor)を並べておく棚
	// ════════════════════════════════════════
	// RTV用のヒープでディスクリプタの数は2。RTVはShader内で触るものではないので、ShaderVisibleはfalse
	ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// SRV用のヒープでディスクリプタの数は128。SRVはShader内で触るものなので、ShaderVisibleはtrue
	// SRV、CBV、UAVを一緒に格納できるディスクリプターヒープ　（入れられるViewの種類が多い分、サイズが大きくなる）
	ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// ════════════════════════════════════════
	// DescriptorSizeを取得しておく
	// deviceごとに固有で実行中に変わらないので、ここで1回だけ取得する
	// ════════════════════════════════════════
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// ════════════════════════════════════════
	// SwapChainからResourceを引っ張ってくる
	// SwapChainが内部に作った2枚の画面そのものを受け取る
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> swapChainResources[2];
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	// うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// ════════════════════════════════════════
	// RTV(レンダーターゲットビュー)を作る
	// 「このリソースに描き込む」という札を、ヒープの何番目に置くか自分で指定する
	// ════════════════════════════════════════
	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;        // 出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;   // 2dテクスチャとして書き込む

	// RTVを2つ作るのでディスクリプタを2つ用意
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	// まず1つ目を作る。1つ目は最初のところに作る。作る場所をこちらで指定してあげる必要がある
	rtvHandles[0] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 0);
	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]); // (１．どのリソースに対してのView、２．描画の仕方の設定　３．生成されるViewの格納場所）
	// 2つ目のディスクリプタハンドルを得る(自力で)
	rtvHandles[1] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 1);
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]); // (１．どのリソースに対してのView、２．描画の仕方の設定　３．生成されるViewの格納場所）


	// ════════════════════════════════════════
	// DepthStencilTexture(深度バッファ)を作る
	// 画面と1対1で対応させるため、ウィンドウと同じサイズで作成する
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

	// ════════════════════════════════════════
	// DSV用のDescriptorHeapとDSVを作る
	// DSVはシェーダーから触らないのでShaderVisibleはfalse。深度バッファは1枚なので数は1
	// ════════════════════════════════════════

	// DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないので、ShaderVisibleはfalse
	ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;          // Resourceと同じフォーマット
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;   // 2Dテクスチャとして見る

	// DSVHeapの先頭([0])にDSVを作る
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetCPUDescriptorHandle(dsvDescriptorHeap, descriptorSizeDSV, 0);
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvHandle);


	// ════════════════════════════════════════
   // FenceとEventを生成する
   // GPUの実行完了をCPU側で待つための仕組み
   // ════════════════════════════════════════
   // 初期値0でFenceを作る
	ComPtr<ID3D12Fence> fence = nullptr;
	uint64_t fenceValue = 0; // 次にGPUに書き込んでもらう予定の値。CPU側のカウンタ
	hr = device->CreateFence(
		fenceValue,                // Fenceの初期値
		D3D12_FENCE_FLAG_NONE,     // フラグ
		IID_PPV_ARGS(&fence));     // 生成したFenceのアドレスを入れる変数
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントを作成する(EventはWindowsの持ち物)
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);


	// ════════════════════════════════════════
	// DXCの初期化
	// Shaderをコンパイルするためのもの。DirectX12とは別物なので独立して初期化する
	// ════════════════════════════════════════
	// dxcCompilerを初期化
	ComPtr<IDxcUtils> dxcUtils = nullptr; // dxcUtils が hlsl を読んでメモリに置く <<(補助機)>>
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)); //(１，何を生成するか . ２，アドレスの格納先)
	assert(SUCCEEDED(hr));


	ComPtr<IDxcCompiler3> dxcCompiler = nullptr; // dxcCompiler が arguments の設定で翻訳する　<<(翻訳機>>
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)); //(１，何を生成するか . ２，アドレスの格納先)
	assert(SUCCEEDED(hr));


	// 現時点でincludeはしないが、includeに対応するための設定を行っておく
	ComPtr<IDxcIncludeHandler> includeHandler = nullptr; //  途中で #include があれば、DXCに呼ばれた includeHandler が探しに行く（扱
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));


	// ════════════════════════════════════════
	// RootSignatureの生成
	// シェーダとリソースをどう関連付けるかを示した受け渡し表。今回は中身なし
	// ════════════════════════════════════════
	// RootSignatureの設定
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{}; // 設定を書き込む構造体
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT; // 「InputAssembler（頂点入力）を使ってよい」という許可


	// RootParameter作成。複数設定できるので配列。シェーダーのレジスタの数に応じて上昇
	//  rootParameters[0] -> PS.register(b0) : rootParameters[1] -> VS.register(b0) : rootParameters[2] -> PS.register(t0~)
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	// Material
	// -----------------------------------------
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0;                     // レジスタ番号0とバインド

	// TransformationMatrix
	// -----------------------------------------
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
	rootParameters[1].Descriptor.ShaderRegister = 0;                     // レジスタ番号0とバインド

	// Texture
	// -----------------------------------------
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {}; // register(t0)
	/*
	1回の描画（1つのピクセル）で複数のテクスチャを読みたいときに、registerの数が増えていく（t1,t2,t3・・・)>>> 伴ってdescriptorRangeの要素も増える
	* 同時に複数枚使う代表例
	   ＜用途＞	                    ＜同時に読むテクスチャ＞
	  PBR（リアルな質感）	        ベースカラー + 法線マップ + ラフネス/メタリック（+ AO）→ 3〜4枚
	  IBL（環境光）	            ベースカラー + 環境マップ（キューブマップ）+ BRDF LUT など
	  影	                        ベースカラー + シャドウマップ
	  エフェクト	                ベースカラー + ディゾルブ用のノイズ画像 / マスク画像
	  地形	                    草・土・岩の画像 + どこに何を塗るかのブレンド用画像
	  ポストエフェクト	            描画結果の画面 + 深度（被写界深度など）
	*/
	descriptorRange[0].BaseShaderRegister = 0;  // 0から始まる（何番のレジスターから始まるか、>>今回はt0だから0)
	descriptorRange[0].NumDescriptors = 1;  // 数は1つ（同タイプのレジスターの数、>>今回はt0のみ）
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算


	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;  // Tableの中身の配列を指定　(！！実質ここでレジスタ指定！！）
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数

	// DirectionalLight
	// -----------------------------------------
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
	rootParameters[3].Descriptor.ShaderRegister = 1;                     // レジスタ番号1とバインド

	descriptionRootSignature.pParameters = rootParameters;               // ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters);   // 配列の長さ
	// descriptionRootSignature が　パラメーターが格納されていく箱（パラメーターが増えるごとに大きくなる）


	// ════════════════════════════════════════
	// サンプラー設定
	// ════════════════════════════════════════
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {}; // register(s0)
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;   // 0~1の範囲外をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; // 比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;   // ありったけのMipmapを使う
	staticSamplers[0].ShaderRegister = 0;   // レジスタ番号0を使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);


#pragma region MyRegion
	/*
	 ＜記号＞	    ＜由来＞	                    ＜何を置くか＞	                        ＜HLSL の例＞
	   b	    Buffer（Constant Buffer）	CPU から渡す定数（行列・色・ライト）	    ConstantBuffer<Material> gMaterial : register(b0);
	   t	    Texture	                    シェーダが読むリソース（SRV）	        Texture2D<float32_t4> gTexture : register(t0);
	   s	    Sampler	                    テクスチャの読み方（補間・繰り返し）	    SamplerState gSampler : register(s0);
	   u	    Unordered Access（UAV）	    シェーダから読み書きするリソース	        RWTexture2D<float4> gOutput : register(u0);

	*/
#pragma endregion
	// シリアライズしてバイナリにする
	ComPtr<ID3DBlob> signatureBlob = nullptr; // 成功したときのバイナリを受け取る変数
	ComPtr<ID3DBlob> errorBlob = nullptr; // 失敗したときのエラーメッセージを受け取る変数

	// そのままGPUなどに渡しても、相手はポインタの先が見えないので、指している先の中身まで全部たどって、1本のバイト列に詰め直す！！！　-> バイナリ
	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature, // 設計図
		D3D_ROOT_SIGNATURE_VERSION_1, // 設計図の書式のバージョン
		&signatureBlob, // 成功時のバイナリアドレスを入れる箱
		&errorBlob); // 失敗時のエラー文を入れる箱

	// エラー確認
	if (FAILED(hr)) {
		Log(logStream, reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	// バイナリを元に生成
	ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(
		0,                                  // nodeMask。GPU が1台なら 0
		signatureBlob->GetBufferPointer(),  // バイナリ本体の場所
		signatureBlob->GetBufferSize(),     // バイナリのバイト数
		IID_PPV_ARGS(&rootSignature));      // 完成品を受け取る箱アドレス
	assert(SUCCEEDED(hr));


	// ════════════════════════════════════════
	// InputLayoutの設定
	// VertexShaderへ渡す頂点データがどのようなものかを指定する
	// ════════════════════════════════════════
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};                     // 頂点の1つの要素（メンバ1つ分）の説明の配列。positionとtexcoordで２

#pragma region MyRegion
	//   HLSL の POSITION0 を、名前 "POSITION" と番号 0 に分けて指定
	//   番号が分かれているのは、同じ種類のデータを複数持つことがあるからです。
	/*
		<HLSL 側>	                <C++ 側>	        <値>
		POSITION	                SemanticName	"POSITION"
		0（POSITION0 の数字部分）	    SemanticIndex	 0

		<hlsl>
		float32_t2 uv0 : TEXCOORD0;   // SemanticName = "TEXCOORD", SemanticIndex = 0
		float32_t2 uv1 : TEXCOORD1;   // SemanticName = "TEXCOORD", SemanticIndex = 1
	*/
#pragma endregion

	inputElementDescs[0].SemanticName = "POSITION";  // 位置座標                   
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;           //「このデータは何ビットの何型が何個か(RGBAで表現）」  <<<<float が 4つ = float32_t4 = 16バイト 
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;  // (この要素は、頂点1個の先頭から何バイト目にあるか = 前の要素の直後に置いて);
	inputElementDescs[1].SemanticName = "TEXCOORD";  // UV座標                      
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;           //「このデータは何ビットの何型が何個か(RGBAで表現）」  <<<<float が 4つ = float32_t4 = 16バイト 
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";  // UV座標                      
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;           //「このデータは何ビットの何型が何個か(RGBAで表現）」  <<<<float が 4つ = float32_t4 = 16バイト 
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};                               // 説明書の表紙。「要素の配列はここから○個」
#pragma region  MyRegion
	/*
	*
	inputLayoutDesc は、
		「頂点1個の形の説明は、この配列のここから○行ぶんだよ」

		*/
#pragma endregion

	inputLayoutDesc.pInputElementDescs = inputElementDescs;                 // 要素の配列の先頭アドレス              
	inputLayoutDesc.NumElements = _countof(inputElementDescs);              // 要素の数。_countof で自動計算（今は 1）


	//////////////////////////////////////////////////////////////////////////////三角形の描画ができたらここからの項目ちょっといじってみて by過去の自分
	// ════════════════════════════════════════
	// BlendStateの設定
	// PixelShaderからの出力を画面にどのように書き込むかを設定する
	// ════════════════════════════════════════
	D3D12_BLEND_DESC blendDesc{}; //PS が「このピクセルは白」と出力したとき、すでに画面にある色（青いクリア色）とどう合わせるかを決めます。
	// すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;


	// ════════════════════════════════════════
	// RasterizerStateの設定
	// 三角形の内部をピクセルに分解する処理(Rasterizer)に対する設定
	// ════════════════════════════════════════
#pragma region  MyRegion
/*
<< Rasterizerとは
・VS が出力した三角形の頂点3つを受け取り、「画面上のどのピクセルがその三角形の内側か

<< GPU は頂点を並べた順番で表裏を判定します。DirectX の既定は次のとおりです。>>

・画面上で時計回りに並んだ三角形    → 表（描かれる）
・画面上で反時計回りに並んだ三角形  → 裏（BACK なら捨てられる）

※三角形を描いたのに何も出ないときは、頂点の順番を疑ってください。
	デバッグ中は一時的に CULL_MODE_NONE にして、表示されるか確かめるのが定番の切り分けです。

*/
#pragma endregion
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	// 裏面を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;


	// ════════════════════════════════════════
	// DepthStencilStateの設定
	// PixelShaderからの出力を画面にどのように書き込むかを設定する
	// ════════════════════════════════════════
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;                            // 深度テストを有効にする
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;   // 深度を書き込む
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;  // 近い(または同じ)なら描画する


	// ════════════════════════════════════════
	// Shaderをコンパイルする(各シェーダーをGPUが読めるように翻訳）
	// ════════════════════════════════════════
	ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, includeHandler, logStream);
	assert(vertexShaderBlob != nullptr);

	ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl",
		L"ps_6_0", dxcUtils, dxcCompiler, includeHandler, logStream);
	assert(pixelShaderBlob != nullptr);


	// ════════════════════════════════════════
	// PSOを生成する（すべての設計書の集大成！！！！！！！！！！！！！）
	// ════════════════════════════════════════
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();    // RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;           // InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize() };                           // VertexShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize() };                            // PixelShader
	graphicsPipelineStateDesc.BlendState = blendDesc;                  // BlendState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;        // RasterizerState
	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1; // PixelShaderOutput の SV_TARGET0 に対応
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // RTV を作ったときの rtvDesc.Format と一致させる必要があります。
	// 利用するトポロジ(形状)のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;     // POINT,LINEなどもある
	// どのように画面に色を打ち込むかの設定(気にしなくて良い)
	graphicsPipelineStateDesc.SampleDesc.Count = 1; // SwapChain で SampleDesc.Count = 1 にしたのと揃えています
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 実際に生成
	ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState)); // 生成したPSOのアドレスをgraphicsPipelineStateに格納
	assert(SUCCEEDED(hr));


	// ════════════════════════════════════════════════════
	// モデルを用意する
	// 使いたい方のコメントを外す（ModelDataを返すので以降のコードは共通）
	// ════════════════════════════════════════════════════
	ModelData modelData = LoadObjFile("resources", "axis.obj");
	//ModelData modelData = CreateSphereModelData(16);
	// 
	// ════════════════════════════════════════════════════
	// モデルの VertexResource を生成してデータを書き込む
	// ════════════════════════════════════════════════════
	ComPtr<ID3D12Resource> vertexResourceModel = CreateBufferResource(device, sizeof(VertexData) * modelData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewModel{};
	// リソースの先頭のアドレスから使う
	vertexBufferViewModel.BufferLocation = vertexResourceModel->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点数ぶんのサイズ（size_t→UINTのキャストが必要）
	vertexBufferViewModel.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	// 1頂点あたりのサイズ
	vertexBufferViewModel.StrideInBytes = sizeof(VertexData);

	// データはもう modelData.vertices に完成しているので、リソースにmemcpyでまるごとコピーする
	VertexData* vertexDataModel = nullptr;
	vertexResourceModel->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataModel));
	std::memcpy(vertexDataModel, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	// ════════════════════════════════════════════════════
	// モデルの IndexResource を生成してデータを書き込む
	// ※ indices が空のモデル（obj）では作らない。nullptrのままにしておく
	// ════════════════════════════════════════════════════
	ComPtr<ID3D12Resource> indexResourceModel = nullptr;
	D3D12_INDEX_BUFFER_VIEW indexBufferViewModel{};

	if (!modelData.indices.empty()) {
		indexResourceModel = CreateBufferResource(device, sizeof(uint32_t) * modelData.indices.size());
		// リソースの先頭のアドレスから使う
		indexBufferViewModel.BufferLocation = indexResourceModel->GetGPUVirtualAddress();
		// 使用するリソースのサイズはインデックス数ぶんのサイズ
		indexBufferViewModel.SizeInBytes = UINT(sizeof(uint32_t) * modelData.indices.size());
		// インデックス1個の型。uint32_t なので R32_UINT（Strideは不要）
		indexBufferViewModel.Format = DXGI_FORMAT_R32_UINT;

		uint32_t* indexDataModel = nullptr;
		indexResourceModel->Map(0, nullptr, reinterpret_cast<void**>(&indexDataModel));
		std::memcpy(indexDataModel, modelData.indices.data(), sizeof(uint32_t) * modelData.indices.size());
	}


	// ════════════════════════════════════════
	// TransformationMatrix用のResourceを作る
	// Matrix4x4 1つ分のサイズを用意する
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> transformationMatrixResourceModel = CreateBufferResource(device, sizeof(TransformationMatrix));
	// データを書き込む
	TransformationMatrix* transformationMatrixDataModel = nullptr;
	// 書き込むためのアドレスを取得
	transformationMatrixResourceModel->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataModel));
	// 単位行列を書きこんでおく
	transformationMatrixDataModel->WVP = MakeIdentity4x4();
	transformationMatrixDataModel->World = MakeIdentity4x4();

	// Transform変数を作る
	Transform transformModel{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	// カメラのTransformを作る。z = -5 の位置で z+ 方向を向いているカメラ
	Transform cameraTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };


	// ════════════════════════════════════════════════════
	// スプライトの VertexResource を生成する
	// ════════════════════════════════════════════════════
	ComPtr<ID3D12Resource> vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 4);

	// ════════════════════════════════════════════════════
	// スプライトの VertexBufferView を作成する
	// ════════════════════════════════════════════════════
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	// リソースの先頭のアドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;
	// 1頂点あたりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);


	// ════════════════════════════════════════════════════
	// スプライトの IndexResource を生成する
	// ════════════════════════════════════════════════════
	ComPtr<ID3D12Resource> indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

	// ════════════════════════════════════════════════════
	// スプライトの indexBufferView を生成する
	// ════════════════════════════════════════════════════
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;   // インデックス1つが32bit整数


	// ════════════════════════════════════════════════════
	// スプライトの頂点データを書き込む
	// ════════════════════════════════════════════════════
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// [0] 左下
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };
	// [1] 左上
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };
	// [2] 右下
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };
	// [3] 右上
	vertexDataSprite[3].position = { 640.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[3].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };

	// ２か所の頂点が重なってるのでデータを再利用することになる


	// ════════════════════════════════════════════════════
	// スプライトのインデックスデータを書き込む
	// ════════════════════════════════════════════════════
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;  indexDataSprite[1] = 1;  indexDataSprite[2] = 2;   // 1枚目：左下→左上→右下
	indexDataSprite[3] = 1;  indexDataSprite[4] = 3;  indexDataSprite[5] = 2;   // 2枚目：左上→右上→右下


	// ════════════════════════════════════════
	// Sprite用のTransformationMatrixのResourceとTransformを作る
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix));
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	transformationMatrixDataSprite->WVP = MakeIdentity4x4();
	transformationMatrixDataSprite->World = MakeIdentity4x4();
	// CPUで動かす用のTransform
	Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	// ════════════════════════════════════════
	// 平行光源用のResourceを作る
	// シーンに1つだけ。球とSpriteで共通
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };  // 白
	directionalLightData->direction = Vector3{ 0.0f, -1.0f, 0.0f };   // 真上から下へ
	directionalLightData->intensity = 1.0f;

	// ════════════════════════════════════════
	// Material用のResourceを作る
	// 今回はcolor1つ分のサイズを用意する
	// ════════════════════════════════════════
	// -----------------------------------------
	// 球体用マテリアル
	// -----------------------------------------
	ComPtr<ID3D12Resource> modelMaterialResource = CreateBufferResource(device, sizeof(Material));
	// マテリアルにデータを書き込む
	Material* modelMaterialData = nullptr;
	// 書き込むためのアドレスを取得
	modelMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&modelMaterialData)); // CPUからでもデータを書ける住所をmaterialDataに受け取る
	// 今回は赤を書き込んでみる
	modelMaterialData->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };   // 白
	modelMaterialData->enableLighting = true;                      // Lightingしない
	modelMaterialData->uvTransform = MakeIdentity4x4();

	Transform uvTransformModel{ // UV移動用
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f },
	};
	// -----------------------------------------
	// スプライト用マテリアル
	// -----------------------------------------
	ComPtr<ID3D12Resource> spriteMaterialResource = CreateBufferResource(device, sizeof(Material));
	// マテリアルにデータを書き込む
	Material* spriteMaterialData = nullptr;
	// 書き込むためのアドレスを取得
	spriteMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&spriteMaterialData)); // CPUからでもデータを書ける住所をmaterialDataに受け取る
	// 今回は赤を書き込んでみる
	spriteMaterialData->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };   // 白
	spriteMaterialData->enableLighting = false;                      // Lightingしない
	spriteMaterialData->uvTransform = MakeIdentity4x4();

	Transform uvTransformSprite{ // UV移動用
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f },
	};
	// ════════════════════════════════════════
	// Texture用のResourceを作る
	// Textureを読み込み、VRAM上のテクスチャに転送する
	// ════════════════════════════════════════
	// -----------------------------------------
	// uvChecker
	// -----------------------------------------
	// テクスチャファイルを読み込み、ミップマップ付きのデータをCPU側(RAM)に作る
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	// テクスチャ全体の情報(幅・高さ・ミップ数・フォーマットなど)を取得
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	// metadataを基に、VRAM上にテクスチャ用のResource(中身は空・COPY_DEST)を作る
	ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device, metadata);
	// 中間リソースにデータを書き込み、VRAM上のResourceへのコピー命令とバリアをコマンドリストに積む
	ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(textureResource, mipImages, device, commandList);

	// -----------------------------------------
	// monsterBall
	// -----------------------------------------
	   // テクスチャファイルを読み込み、ミップマップ付きのデータをCPU側(RAM)に作る
	DirectX::ScratchImage mipImages2 = LoadTexture(modelData.material.textureFilePath);
	// テクスチャ全体の情報(幅・高さ・ミップ数・フォーマットなど)を取得
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	// metadataを基に、VRAM上にテクスチャ用のResource(中身は空・COPY_DEST)を作る
	ComPtr<ID3D12Resource> textureResource2 = CreateTextureResource(device, metadata2);
	// 中間リソースにデータを書き込み、VRAM上のResourceへのコピー命令とバリアをコマンドリストに積む
	ComPtr<ID3D12Resource> intermediateResource2 = UploadTextureData(textureResource2, mipImages2, device, commandList);

	// ════════════════════════════════════════
	// 主にテクスチャデータをVRAM上のリソースに送るためのコマンドキック
	// ════════════════════════════════════════
	hr = commandList->Close();
	assert(SUCCEEDED(hr));
	ID3D12CommandList* uploadCommandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, uploadCommandLists);

	fenceValue++;
	commandQueue->Signal(fence.Get(), fenceValue);
	if (fence->GetCompletedValue() < fenceValue) {
		fence->SetEventOnCompletion(fenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));
	intermediateResource.Reset();  // VRAM上のリソースへ転送が完了したので <CPU> と <VRAM上リソース> の橋となっていた中間リソースはもういらない
	intermediateResource2.Reset(); // ComPtrなのでReleaseではなくReset（明示的に手放す）

	// ════════════════════════════════════════
	// Texture用のSRVを作る
	// 先頭(index 0)はImGuiのフォントが使用
	// ════════════════════════════════════════

	// -----------------------------------------
	// uvChecker      : srvDescriptorHeapの2番目(index 1)に作成する
	// -----------------------------------------
	// metaDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; //2Dテクスチャ
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// SRVを作成するDescriptorHeapの場所を決める
	// 先頭はImGuiが使っているのでその次[1]を使う
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);

	// SRVの生成
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

	// -----------------------------------------
	// monsterBall      : srvDescriptorHeapの3番目(index 2)に作成する
	// -----------------------------------------
	// metaDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; //2Dテクスチャ
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	// SRVを作成するDescriptorHeapの場所を決める
	// [2]を使う
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);

	// SRVの生成
	device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);

	// ════════════════════════════════════════
	// ViewportとScissor
	// Viewport: NDCをウィンドウのどこに写すか / Scissor: 描画してよい範囲の切り抜き
	// ════════════════════════════════════════
	// ビューポート
	D3D12_VIEWPORT viewport{};
	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形
	D3D12_RECT scissorRect{};
	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;


	// ════════════════════════════════════════
	// ImGuiの初期化
	// ════════════════════════════════════════
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark(); // 他の色種類もある＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞試してみ　by　過去の自分
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device.Get(), swapChainDesc.BufferCount, rtvDesc.Format, srvDescriptorHeap.Get(),
		GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 0),
		GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 0));
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif

	//════════════════════════════════════════════════════
	// 入力の初期化
	//════════════════════════════════════════════════════
	Input input;
	input.Initialize(wc.hInstance, hwnd);

	// Texture切り替え変数
	bool useMonsterBall = true;

	MSG msg{};
	// ウィンドウの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT) {
		// Windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			// ゲームの処理

			//════════════════════════════════════════════════════
			// 入力の更新（毎フレーム処理の先頭）
			//════════════════════════════════════════════════════
			input.Update();

			//════════════════════════════════════════════════════
			// 入力テスト（動作確認用・確認後に削除）
			//════════════════════════════════════════════════════
			if (input.PushKey(DIK_0)) {
				Log(logStream, "0 PUSH");
			}

			if (input.TriggerKey(DIK_SPACE)) {
				Log(logStream, "SPACE TRIGGER");
			}

			if (input.ReleaseTriggerKey(DIK_SPACE)) {
				Log(logStream, "SPACE RELEASE");
			}

			// ────────────────────────────────────────
			// ImGui開始処理
			// ────────────────────────────────────────
#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
#endif

			// カメラとスフィアの設定
#ifdef USE_IMGUI
			ImGui::Begin("Settings");
			ImGui::DragFloat3("camera translate", &cameraTransform.translate.x, 0.01f);
			ImGui::DragFloat3("camera rotate", &cameraTransform.rotate.x, 0.01f);
			ImGui::DragFloat3("sphere rotate", &transformModel.rotate.x, 0.01f);
			ImGui::ColorEdit4("color", &modelMaterialData->color.x);
			ImGui::DragFloat3("light direction", &directionalLightData->direction.x, 0.01f);
			ImGui::DragFloat("light intensity", &directionalLightData->intensity, 0.01f);
			ImGui::ColorEdit4("light color", &directionalLightData->color.x);
			directionalLightData->direction = Normalize(directionalLightData->direction);   // 毎フレーム正規化
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);

			// ────────────────────────────────────────
			// SphereとSpriteのUVTransform
			// UV座標はxy平面なので、移動・拡縮はxyの2成分、回転はz軸まわりのみ
			// ────────────────────────────────────────
			ImGui::DragFloat2("SpriteUVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("SpriteUVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("SpriteUVRotate", &uvTransformSprite.rotate.z);
			ImGui::DragFloat2("ModelUVTranslate", &uvTransformModel.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("ModelUVScale", &uvTransformModel.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("ModelUVRotate", &uvTransformModel.rotate.z);
			ImGui::End();
#endif

			// ────────────────────────────────────────
			// ＜3Dモデル＞     レンダ（クリップ空間まで変換）（３D）
			// ────────────────────────────────────────
			Matrix4x4 worldMatrixModel = MakeAffineMatrix(transformModel.scale, transformModel.rotate, transformModel.translate); // 拡縮、回転、移動
			Matrix4x4 cameraMatrixModel = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate); // ワールド基準で、カメラはどこにいるか
			Matrix4x4 viewMatrixModel = Inverse(cameraMatrixModel);  // カメラ基準で、ワールドはどこにあるか(ワールドをカメラに合わせる）
			/*
			  やりたいこと    ： カメラ基準の座標が欲しい
			  計算方法       ： 世界全体を、カメラが原点に来るようにずらす
			  結果          ： ずらした後の座標 ＝ カメラ基準の座標
			*/

			Matrix4x4 projectionMatrix = MakePerspectiveMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f); // 透視投影 で遠いほど小さく見える処理を加え、２D上でも奥行を出す（ 視錐台切り取りカメラ ）
			/*
			* MakePerspectiveMatrix(0.45f, 1280.0f / 720.0f, 0.1f, 100.0f);
									   ↑         ↑            ↑      ↑
									  fovY    aspectRatio   nearClip farClip


				引数	            値	                    意味
				fovY	        0.45（ラジアン、約26度）	縦方向の画角。カメラのレンズの広さ
				aspectRatio	    1.778（16:9）	        画面の横÷縦
				nearClip	    0.1	                    これより近いものは映さない
				farClip	        100.0	                これより遠いものは映さない
			*/
			transformationMatrixDataModel->WVP = MakeWVPMatrix(worldMatrixModel, viewMatrixModel, projectionMatrix); // これらの行列を掛け合わせる
			transformationMatrixDataModel->World = worldMatrixModel;

			// ────────────────────────────────────────
			// ＜3Dモデル＞　UVTransform行列を作る
			// 座標変換の基本どおり Scale → Rotate → Translate (SRT) の順に掛ける
			// ────────────────────────────────────────
			Matrix4x4 uvTransformMatrixSphere = MakeAffineMatrix(uvTransformModel.scale, uvTransformModel.rotate, uvTransformModel.translate);
			modelMaterialData->uvTransform = uvTransformMatrixSphere;

			// ────────────────────────────────────────
			// ＜スプライト＞　レンダ（クリップ空間まで変換）（２D）
			// ────────────────────────────────────────
			// Sprite用のWorldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();   // 2D用カメラ（原点・動かさない）
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f); // 平行投影 で完全にカメラから平行に映す処理（ 直方体カメラ＜0〜1280 × 0〜720 × 0〜100＞）
			transformationMatrixDataSprite->WVP = MakeWVPMatrix(worldMatrixSprite, viewMatrixSprite, projectionMatrixSprite);
			transformationMatrixDataSprite->World = worldMatrixSprite;

			// ────────────────────────────────────────
			// ＜スプライト＞　UVTransform行列を作る
			// 座標変換の基本どおり Scale → Rotate → Translate (SRT) の順に掛ける
			// ────────────────────────────────────────
			Matrix4x4 uvTransformMatrixSprite = MakeAffineMatrix(uvTransformSprite.scale, uvTransformSprite.rotate, uvTransformSprite.translate);
			spriteMaterialData->uvTransform = uvTransformMatrixSprite;
#ifdef USE_IMGUI
			// 開発用UIの処理
			ImGui::ShowDemoWindow();

			// ImGuiの内部コマンドを生成する（描画処理の前）
			ImGui::Render();
#endif



			// ────────────────────────────────────────
			// コマンドを積み込んで確定させる
			// ────────────────────────────────────────
			// これから書き込むバックバッファのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();


			// ────────────────────────────────────────
			// TransitionBarrierを張る(PRESENT → RENDER_TARGET)
			// ────────────────────────────────────────
			D3D12_RESOURCE_BARRIER barrier{};
			// 今回のバリアはTransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			// Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			// バリアを張る対象のリソース。現在のバックバッファに対して行う
			barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
			// 遷移前(現在)のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; // スワップチェーンの持つ２つのリソースは初期状態 PRESENT
			// 遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);


			// 描画先のRTVとDSVを設定する
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);   // nullptr → &dsvHandle

			// 指定した色で画面全体をクリアする
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; // 青っぽい色。RGBAの順
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			// 指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			//srvDescriptorHeapは　CBV_SRV_UAV　をまとめて管理する（SAMPLERヒープは別で descriptorHeaps[] に格納）
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
			commandList->SetDescriptorHeaps(1, descriptorHeaps); // srvDescriptorHeapを設定 = 格納されたCBV, SRV, UAVをシェーダーに見せれる


			commandList->RSSetViewports(1, &viewport);          // Viewportを設定　＞＞＞　個数 + 配列」の形なのは、画面分割などで複数設定できるようにするため
			commandList->RSSetScissorRects(1, &scissorRect);    // Scissorを設定   ＞＞＞　個数 + 配列」の形なのは、画面分割などで複数設定できるようにするため
			// RootSignatureを設定。PSOに設定しているけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(graphicsPipelineState.Get());   // PSOを設定
			// 形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけば良い
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
#pragma region  MyRegion
			/*
			＜値＞	        ＜頂点の使い方＞
			TRIANGLELIST	3つずつ区切って、独立した三角形にする
			TRIANGLESTRIP	1つずらしながら、前の2頂点と組み合わせて連続した三角形にする
			*/
#pragma endregion

#pragma region  MyRegion
			/*
			rootParameters配列の０番目に紐付けられたシェーダーのレジスタに、         （今回はPSシェーダーのgMaterial:b0)
			第二引数のリソース本体のアドレスを送るぜよ                           （ここにコードで指定した色データが入っており、シェーダのmain関数へ）
			*/
#pragma endregion

			// 平行光源はシーン共通なので、描画の最初に1回設定すればよい
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[3]を介して、 directionalLightResourceアドレスを渡す

			// ────────────────────────────────────────
			// モデルを描画する
			// ────────────────────────────────────────
			//	モデルはテクスチャをImGuiで切り替えられる

			commandList->SetGraphicsRootConstantBufferView(0, modelMaterialResource->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[0]を介して、sphereMaterialResourceアドレスを渡す
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU); // シェーダーにルートパラメータ[2]を介して、srvDescripterHeap内の描画予定のTextureアドレスを渡す
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewModel);   // モデル用VBVを設定
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceModel->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[1]を介して、TransformationMatrixResourceSphereアドレスを渡す

			// 描画！(DrawCall/ドローコール)
			// インデックスを持っているモデル(球)と、持っていないモデル(obj)で呼ぶ関数が変わる
			/*
			第1引数：読むインデックスの数。6個 ÷ 3 = 三角形2枚 = 四角形1枚
			第2引数：インスタンス数。同じ形を何体描くか。今は1体
			第3引数：インデックスバッファの何番目から読むか。先頭からなので0
			第4引数：インデックスの値に足すオフセット。頂点バッファの先頭から使うので0
			第5引数：SV_InstanceID の開始番号。インスタンシングしないので0
			*/
			if (indexResourceModel != nullptr) {
				commandList->IASetIndexBuffer(&indexBufferViewModel);   // モデル用IBVを設定 (現在は自作モデルのみ）
				commandList->DrawIndexedInstanced(UINT(modelData.indices.size()), 1, 0, 0, 0);
			} else {
				commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
			}

			// ────────────────────────────────────────
			// スプライトを描画する
			// ────────────────────────────────────────

			commandList->SetGraphicsRootConstantBufferView(0, spriteMaterialResource->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[0]を介して、spriteMaterialResourceアドレスを渡す
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);   // スプライト用VBVを設定
			commandList->IASetIndexBuffer(&indexBufferViewSprite);            // スプライト用IBVを設定
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[1]を介して、transformationMatrixResourceSpriteアドレスを渡す

			// 描画！(DrawCall/ドローコール)

			commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);                   // DrawInstanced → DrawIndexedInstanced

#pragma region  MyRegion
			/*
			＜引数＞	                  ＜値＞	    ＜意味＞
			VertexCountPerInstance	    3	    1回の描画で使う頂点の数
			InstanceCount	            1	    同じものを何個描くか
			StartVertexLocation	        0	    頂点バッファの何番目から使うか
			StartInstanceLocation	    0	    インスタンス番号をいくつから数えるか
			*/
#pragma endregion
			// ===== ImGuiの描画（これも一応BackBufferリソースに書き込む要素なのでbarrierのPRESENTへの遷移よりは上に書く） =====　
#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get()); // ImGui::Renderのデータと使っているコマンドリストを引数に
#endif
			// 画面に描く処理はすべて終わり、画面に映すので、状態を遷移
			// 今回はRenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// ────────────────────────────────────────
			// コマンドをキックする
			// ────────────────────────────────────────
			// GPUにコマンドリストの実行を行わせる
			ID3D12CommandList* commandLists[] = { commandList.Get() };
			commandQueue->ExecuteCommandLists(1, commandLists);

			// GPUとOSに画面の交換を行うよう通知する
			swapChain->Present(1, 0); // 描き終わったら表と入れ替えてっていう命令


			// ────────────────────────────────────────
			// GPUの実行完了を待つ(この後Resetするので、確実に待ってから行う)
			// ────────────────────────────────────────
			// Fenceの値を更新
			fenceValue++;
			// GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
			commandQueue->Signal(fence.Get(), fenceValue);

			// Fenceの値が指定したSignal値にたどり着いているか確認する
			// GetCompletedValueの初期値はFence作成時に渡した初期値
			if (fence->GetCompletedValue() < fenceValue) {
				// 指定したSignalにたどりついていないので、たどり着くまで待つようにイベントを設定する
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				// イベント待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}


			// 次のフレーム用のコマンドリストを準備
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator.Get(), nullptr);
			assert(SUCCEEDED(hr));
		}
	}

	// ════════════════════════════════════════
	// 終了処理
	// COMオブジェクトはComPtrが自動で解放するので、ここでやることはごくわずか
	// ════════════════════════════════════════
	// ─── ImGui（内部でdevice・srvDescriptorHeapを掴んでいるので、必ず先に終了させる）───
#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	// ─── COMオブジェクトの解放 ───
	// すべてComPtrで管理しているので、WinMainを抜けるときにデストラクタが
	// 「構築したのと逆の順番」で自動的にReleaseを呼んでくれる。
	// ここに Release() を書くと二重解放になるので絶対に書かないこと。

	// ─── ComPtrで扱えないものだけ、ここで後始末する ───
	CloseHandle(fenceEvent);   // EventはCOMではないのでReleaseではなくCloseHandle
	CloseWindow(hwnd);         // Win32APIのオブジェクトなのでCloseWindow

	// ※ リソースリークチェックは D3DResourceLeakChecker のデストラクタで行う。
	//    ここで行うと、まだComPtrが解放されていないので「漏れている」と誤検出される。

	CoUninitialize();
	return 0;
}
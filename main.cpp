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
#include "DebugCamera.h"
#include "PrimitiveDrawer.h"
#include "Logger.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "TextureManager.h" 
// imgui用のincludeと外部関数
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

// Debug用のあれやこれやを使えるようにする
#include <dbghelp.h>
// StringCchPrintfWを使うため
#include <strsafe.h>

// リソースリークチェック用
#include <dxgidebug.h>
// ComPtr（COMオブジェクトの参照カウントを自動で管理してくれるクラス）用
#include <wrl.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

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
struct VertexData {	  // 全体＜32＞バイト
	Vector3 position; // 座標       (float<3> * 3) = 12バイト  
	Vector2 texcoord; // UV座標     (float<2> * 2) = 8バイト
	Vector3 normal;   // 頂点の向き  (float<3> * 3) = 12バイト
};

// ════════════════════════════════════════
// 線描画用の頂点
//   位置と色だけ。テクスチャ座標も法線も使わない
// ════════════════════════════════════════
struct LineVertex {   // 全体＜28＞バイト
	Vector3 position;   // 座標 (float<3> * 3) = 12バイト  
	Vector4 color;      // 色   (float<4> * 4) = 16バイト
};

// ════════════════════════════════════════
// MaterialData構造体
// mtlファイルから読み込んだマテリアル情報
// ※ 今はテクスチャのパスだけ。将来 Kd(色) や法線マップのパスが増える
// ════════════════════════════════════════
struct MaterialData {

	// マテリアル名（mtl の newmtl。obj の usemtl と照合するのに使う）
	std::string name;

	std::string textureFilePath;   // 使用するテクスチャの相対パス（"resources/uvChecker.png" の形）

	// 拡散色（mtl の Kd）＋ 不透明度（mtl の d）
	//   mtl に書かれていない場合は白・不透明のまま
	Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

// ════════════════════════════════════════
// ModelData構造体
// objファイルから読み取ったモデル全体のデータ
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
// mtlファイル(テキスト)を読み、マテリアル情報を取り出す
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
		// ─── 拡散色（RGB） ───
		else if (identifier == "Kd") {
			s >> materialData.color.x >> materialData.color.y >> materialData.color.z;
		}
		// ─── 不透明度（1.0 = 不透明） ───
		else if (identifier == "d") {
			s >> materialData.color.w;
		}
		// ─── 透明度（d の逆。書き出すソフトによってはこちら） ───
		else if (identifier == "Tr") {
			float transparency = 0.0f;
			s >> transparency;
			materialData.color.w = 1.0f - transparency;
		}
		// 上記以外( newmtl / Ka / Ks / Ns / illum など )は今は無視する
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
	std::vector<Vector3> positions;      // objの v  を溜める作業用配列
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
			Vector3 position;
			s >> position.x >> position.y >> position.z;
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
				Vector3 position = positions[elementIndices[0] - 1];
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
	// 5. ModelDataを返す
	// ────────────────────────────────────────
	return modelData;
}


// ════════════════════════════════════════
// 円周率
// ════════════════════════════════════════
const float kPi = 3.14159265358979323846f;

// ════════════════════════════════════════
// 基準グリッド（Blenderの床グリッド相当）の頂点を作る
//   中心から ±kGridHalfCount マス、間隔 kGridSpacing
//   X軸=赤 / Y軸=緑 / Z軸=青、それ以外はグレー
// ════════════════════════════════════════
const int32_t  kGridHalfCount = 10;     // 中心から何マス伸ばすか
const float    kGridSpacing = 1.0f;   // 1マスの大きさ

// ════════════════════════════════════════════════════════════════════
// グリッドを PrimitiveDrawer に予約する
//   旧 CreateGridVertices を「配列に書く」から「線を頼む」に書き換えたもの
// ════════════════════════════════════════════════════════════════════
void DrawGrid(PrimitiveDrawer& drawer) {
	const Vector4 kGray = { 0.35f, 0.35f, 0.35f, 1.0f };
	const Vector4 kRed = { 0.75f, 0.25f, 0.25f, 1.0f };
	const Vector4 kGreen = { 0.25f, 0.65f, 0.35f, 1.0f };
	const Vector4 kBlue = { 0.25f, 0.45f, 0.80f, 1.0f };

	const float half = kGridHalfCount * kGridSpacing;

	for (int32_t n = -kGridHalfCount; n <= kGridHalfCount; ++n) {
		const float p = n * kGridSpacing;

		// Z方向に伸びる線（X = p の位置）
		const Vector4 colorAlongZ = (n == 0) ? kBlue : kGray;
		drawer.DrawLine3d({ p, 0.0f, -half }, { p, 0.0f, half }, colorAlongZ);

		// X方向に伸びる線（Z = p の位置）
		const Vector4 colorAlongX = (n == 0) ? kRed : kGray;
		drawer.DrawLine3d({ -half, 0.0f, p }, { half, 0.0f, p }, colorAlongX);
	}

	// Y軸（上向き）
	drawer.DrawLine3d({ 0.0f, 0.0f, 0.0f }, { 0.0f, half, 0.0f }, kGreen);
}

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


	// ════════════════════════════════════════════════════════════════
	// ウィンドウの生成
	// ════════════════════════════════════════════════════════════════
	WinApp winApp;
	winApp.Initialize();


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


	// 新しく作ったLog関数を使ってファイル出力とデバッグ出力を同時に行う
	Log(logStream, "hello DirectX");

	// ════════════════════════════════════════════════════════════════
	// DirectX 基盤の初期化
	// ════════════════════════════════════════════════════════════════
	DirectXCommon dxCommon;
	dxCommon.Initialize(&winApp, logStream);

	// ────────────────────────────────────────────────────────────────
	// 以降のコードを今まで通りの名前で書けるようにする別名
	//   実体は dxCommon の中。参照なのでコピーは起きない
	// ────────────────────────────────────────────────────────────────
	const ComPtr<ID3D12Device>& device = dxCommon.GetDevice();
	const ComPtr<ID3D12GraphicsCommandList>& commandList = dxCommon.GetCommandList();
	const ComPtr<ID3D12DescriptorHeap>& srvDescriptorHeap = dxCommon.GetSrvDescriptorHeap();


	// ════════════════════════════════════════════════════════════════
	// テクスチャマネージャの初期化
	// ════════════════════════════════════════════════════════════════
	TextureManager textureManager;
	textureManager.Initialize(&dxCommon);


	// 以降で使い回す HRESULT（元は DXGIFactory 生成時に宣言していた）
	HRESULT hr{};



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
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;           //「このデータは何ビットの何型が何個か(RGBAで表現）」  <<<<float が 4つ = float32_t4 = 16バイト 
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
	ComPtr<IDxcBlob> vertexShaderBlob = dxCommon.CompileShader(L"Object3d.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	ComPtr<IDxcBlob> pixelShaderBlob = dxCommon.CompileShader(L"Object3d.PS.hlsl", L"ps_6_0");
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
	ModelData modelData = LoadObjFile("resources", "barrel.obj");
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
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f };
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };
	// [1] 左上
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f };
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };
	// [2] 右下
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f };
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };
	// [3] 右上
	vertexDataSprite[3].position = { 640.0f, 0.0f, 0.0f };
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
	// グリッドのWVP行列用（transformationMatrix）リソース
	// ════════════════════════════════════════
	ComPtr<ID3D12Resource> transformationMatrixResourceGrid =
		CreateBufferResource(device, sizeof(TransformationMatrix));

	TransformationMatrix* transformationMatrixDataGrid = nullptr;
	transformationMatrixResourceGrid->Map(0, nullptr,
		reinterpret_cast<void**>(&transformationMatrixDataGrid));
	transformationMatrixDataGrid->WVP = MakeIdentity4x4();
	transformationMatrixDataGrid->World = MakeIdentity4x4();


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
	// モデル用マテリアル
	// -----------------------------------------
	ComPtr<ID3D12Resource> modelMaterialResource = CreateBufferResource(device, sizeof(Material));
	// マテリアルにデータを書き込む
	Material* modelMaterialData = nullptr;
	// 書き込むためのアドレスを取得
	modelMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&modelMaterialData)); // CPUからでもデータを書ける住所をmaterialDataに受け取る
	// 今回は赤を書き込んでみる
	modelMaterialData->color = modelData.material.color;    // 白
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


	// ════════════════════════════════════════════════════════════════
	// Texture の読み込み
	//   パスを渡すと「SRV ヒープの何番に置いたか」が返る
	//   読み込み・VRAM への転送・SRV の生成は TextureManager がやってくれる
	// ════════════════════════════════════════════════════════════════
	uint32_t uvCheckerTextureIndex = textureManager.Load("resources/uvChecker.png");
	uint32_t modelTextureIndex = textureManager.Load(modelData.material.textureFilePath);


	// ════════════════════════════════════════
	// ImGuiの初期化
	// ════════════════════════════════════════
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark(); // 他の色種類もある＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞＞試してみ　by　過去の自分
	ImGui_ImplWin32_Init(winApp.GetHwnd());

	// ImGui のフォントテクスチャ用に1つ確保する
	uint32_t imguiSrvIndex = dxCommon.AllocateSrv();

	ImGui_ImplDX12_Init(
		device.Get(),
		DirectXCommon::kBackBufferCount,          // swapChainDesc.BufferCount から変更
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,          // rtvDesc.Format から変更
		srvDescriptorHeap.Get(),
		dxCommon.GetSrvCpuDescriptorHandle(imguiSrvIndex),
		dxCommon.GetSrvGpuDescriptorHandle(imguiSrvIndex));
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif


	// ════════════════════════════════════════
	// 入力・デバッグカメラの初期化
	// ════════════════════════════════════════
	Input input;
	input.Initialize(winApp.GetHInstance(), winApp.GetHwnd());

	DebugCamera debugCamera;
	debugCamera.Initialize(&input, WinApp::kClientWidth, WinApp::kClientHeight);

	// デバッグカメラの有効／無効（F1キーで切り替え）
	bool useDebugCamera = false;

	// ════════════════════════════════════════════════════════════════
	// プリミティブドロワーの初期化
	// ════════════════════════════════════════════════════════════════
	PrimitiveDrawer primitiveDrawer;
	primitiveDrawer.Initialize(&dxCommon);


	// Texture切り替え変数
	bool useMonsterBall = true;

	// ウィンドウの×ボタンが押されるまでループ
	while (true) {
		if (winApp.ProcessMessage()) {
			break;
		}
		// ゲームの処理

		// ════════════════════════════════════════
		// 入力の更新（毎フレーム処理の先頭）
		// ════════════════════════════════════════
		input.Update();

		// ────────────────────────────────────────
		// ImGui開始処理
		// ────────────────────────────────────────
#ifdef USE_IMGUI
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
#endif

		// ════════════════════════════════════════
		// デバッグカメラの更新
		//   WantCaptureMouse は NewFrame() の中で計算されるので、必ずその後で読む
		// ════════════════════════════════════════
		// F1キーで通常カメラと切り替え
		if (input.TriggerKey(DIK_F1)) {
			useDebugCamera = !useDebugCamera;
		}

		// ImGuiを操作している最中はカメラを動かさない
#ifdef USE_IMGUI
		bool imguiUsingMouse = ImGui::GetIO().WantCaptureMouse;
#else
		bool imguiUsingMouse = false;
#endif
		if (useDebugCamera && !imguiUsingMouse) {
			debugCamera.Update();
		}

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
		ImGui::Text("Lines: %u", primitiveDrawer.GetLineCount());
		ImGui::End();

		// ────────────────────────────────────────
		// デバッグカメラの状態表示
		// ────────────────────────────────────────
		ImGui::Begin("DebugCamera");
		ImGui::Text("F1 : %s", useDebugCamera ? "ON" : "OFF");
		ImGui::Text("pos    : %.2f, %.2f, %.2f",
			debugCamera.GetTranslation().x, debugCamera.GetTranslation().y, debugCamera.GetTranslation().z);
		ImGui::Text("target : %.2f, %.2f, %.2f",
			debugCamera.GetTarget().x, debugCamera.GetTarget().y, debugCamera.GetTarget().z);
		ImGui::Text("dist   : %.2f", debugCamera.GetDistance());
		ImGui::Separator();
		ImGui::Text("mouse  : %.0f, %.0f  wheel %.1f",
			input.GetMouseMove().x, input.GetMouseMove().y, input.GetMouseWheel());
		ImGui::End();
#endif


		// ────────────────────────────────────────
		// ＜3Dモデル＞     レンダ（クリップ空間まで変換）（３D）
		// ────────────────────────────────────────
		Matrix4x4 worldMatrixModel = MakeAffineMatrix(transformModel.scale, transformModel.rotate, transformModel.translate); // 拡縮、回転、移動
		// ────────────────────────────────────────
		// ビュー行列・射影行列（通常カメラ / デバッグカメラの切り替え）
		// ────────────────────────────────────────
		Matrix4x4 viewMatrixModel;
		Matrix4x4 projectionMatrix;

		if (useDebugCamera) {
			// デバッグカメラが自前で計算済みのものを受け取るだけ
			viewMatrixModel = debugCamera.GetViewMatrix();
			projectionMatrix = debugCamera.GetProjectionMatrix();
		} else {
			Matrix4x4 cameraMatrixModel = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate); // ワールド基準で、カメラはどこにいるか
			viewMatrixModel = Inverse(cameraMatrixModel);  // カメラ基準で、ワールドはどこにあるか(ワールドをカメラに合わせる）
			/*
			  やりたいこと    ： カメラ基準の座標が欲しい
			  計算方法       ： 世界全体を、カメラが原点に来るようにずらす
			  結果          ： ずらした後の座標 ＝ カメラ基準の座標
			*/

			projectionMatrix = MakePerspectiveMatrix(0.45f, float(WinApp::kClientWidth) / float(WinApp::kClientHeight), 0.1f, 100.0f); // 透視投影 で遠いほど小さく見える処理を加え、２D上でも奥行を出す（ 視錐台切り取りカメラ ）
		}
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
		Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 100.0f); // 平行投影 で完全にカメラから平行に映す処理（ 直方体カメラ＜0〜1280 × 0〜720 × 0〜100＞）
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
		// 描画前処理（バリア・クリア・各種設定）
		// ────────────────────────────────────────
		dxCommon.PreDraw();	


		// ════════════════════════════════════════════════════════════
		// PrimitiveDrawer（クラス） による描画
		// ════════════════════════════════════════════════════════════
		// 線の予約（毎フレーム、先頭でリセット）
		primitiveDrawer.Reset();
		DrawGrid(primitiveDrawer);

		primitiveDrawer.Draw(viewMatrixModel * projectionMatrix);


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
		commandList->SetGraphicsRootDescriptorTable(
			2, textureManager.GetSrvGpuHandle(useMonsterBall ? modelTextureIndex : uvCheckerTextureIndex)); // シェーダーにルートパラメータ[2]を介して、srvDescripterHeap内の描画予定のTextureアドレスを渡す
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
		commandList->SetGraphicsRootDescriptorTable(
			2, textureManager.GetSrvGpuHandle(uvCheckerTextureIndex));
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
		// ────────────────────────────────────────
		// 描画後処理（バリア・実行・Present・フェンス待ち）
		// ────────────────────────────────────────
		dxCommon.PostDraw();


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
	// ════════════════════════════════════════════════════════════════
	// windows後片付け
	// ════════════════════════════════════════════════════════════════
	dxCommon.Finalize();
	winApp.Finalize();


	// ※ リソースリークチェックは D3DResourceLeakChecker のデストラクタで行う。
	//    ここで行うと、まだComPtrが解放されていないので「漏れている」と誤検出される。

	CoUninitialize();
	return 0;
}
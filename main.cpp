#include <Windows.h>
#include <cstdint>
#include <string>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxcapi.h>
#include "MathUtility.h"
// imgui用のincludeと外部関数
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
// -------------------------

// Debug用のあれやこれやを使えるようにする
#include <dbghelp.h>
// StringCchPrintfWを使うため
#include <strsafe.h>

// リソースリークチェック用
#include <dxgidebug.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

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
IDxcBlob* CompileShader(
    // CompilerするShaderファイルへのパス
    const std::wstring& filePath,
    // Compilerに使用するProfile
    const wchar_t* profile,
    // 初期化で生成したものを3つ
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler,
    // ログ出力先(スライドにはない。自作Logに合わせて追加)
    std::ostream& os) {

    // ────────────────────────────────────────
    // 1. hlslファイルを読む
    // ────────────────────────────────────────
    // これからシェーダーをコンパイルする旨をログに出す
    Log(os, std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile));
    // hlslファイルを読む
    IDxcBlobEncoding* shaderSource = nullptr; // テキストの入れ物を指すアドレス
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
    IDxcResult* shaderResult = nullptr;
    hr = dxcCompiler->Compile(
        &shaderSourceBuffer,        // 読み込んだファイル
        arguments,                  // コンパイルオプション
        _countof(arguments),        // コンパイルオプションの数
        includeHandler,             // includeが含まれた諸々
        IID_PPV_ARGS(&shaderResult) // コンパイル結果の箱(エラー文とバイナリの詰め合わせ)のアドレスをshaderResultに格納
    );
    // コンパイルエラーではなくdxcが起動できないなど致命的な状況
    assert(SUCCEEDED(hr));

    // ────────────────────────────────────────
    // 3. 警告・エラーがでていないか確認する
    // ────────────────────────────────────────
    IDxcBlobUtf8* shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr); // コンパイル結果からエラー情報部分を取得
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(os, shaderError->GetStringPointer());
        // 警告・エラーダメゼッタイ
        assert(false);
    }

    // ────────────────────────────────────────
    // 4. Compile結果を受け取って返す
    // ────────────────────────────────────────
    IDxcBlob* shaderBlob = nullptr; // 翻訳後のバイナリを管理する入れ物(Blob)のアドレスを格納
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr); // 結果の箱から実行用バイナリの入れ物を取り出す
    assert(SUCCEEDED(hr));
    // 成功したログを出す
    Log(os, std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile));
    // もう使わないリソースを解放
    shaderSource->Release();
    shaderResult->Release();
    // 実行用のバイナリを返却
    return shaderBlob;
}

// ════════════════════════════════════════
// CreateBufferResource関数
// VertexBufferやConstantBufferなど、BufferResourceを作る共通処理
// ════════════════════════════════════════
ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
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
    ID3D12Resource* bufferResource = nullptr;
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
ID3D12DescriptorHeap* CreateDescriptorHeap(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
    ID3D12DescriptorHeap* descriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}


// Windowsアプリでのエントリーポイント（main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

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
    IDXGIFactory7* dxgiFactory = nullptr; // DXGIFactoryのアドレスを入れる変数
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory)); // 作ったファクトリーのアドレスをdxgiFactoryに格納（本体はメインメモリに生成）

    assert(SUCCEEDED(hr)); //SUCCEEDED(0)であれば正常　(上の生成が成功したらhrに０が入る）


    // ════════════════════════════════════════
    // 使用するアダプタ(GPU)を決定する
    // ════════════════════════════════════════

    // 使用するアダプタのアドレス格納用変数
    IDXGIAdapter4* useAdapter = nullptr;

    // 高性能な順にアダプタを頼む
    for (UINT i = 0; 
        dxgiFactory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; //　パソコン搭載のGPUをハイスペック順に仮にuseAdapterに入れて調べる
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
        // ソフトウェアアダプタは手放して次へ(参照カウントを減らす)
        useAdapter->Release();
        useAdapter = nullptr;
    }
    // 適切なアダプタが見つからなかったので起動できない
    assert(useAdapter != nullptr);


    // ════════════════════════════════════════
    // デバッグレイヤーの有効化
    // 必ずDevice生成より前に行う(後からでは適用されない)
    // ════════════════════════════════════════
    #ifdef _DEBUG
        ID3D12Debug1* debugController = nullptr;
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
    ID3D12Device* device = nullptr;

    // 機能レベルとログ出力用の文字列(同じ番号同士が対応する)
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0
    };
    const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
    // 高い順に生成できるか試していく
    for (size_t i = 0; i < _countof(featureLevels); ++i) {
        // 採用したアダプタでデバイスを生成
        hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device)); //　deviceに生成されたデバイスのアドレスが格納されるぞ！！(本体はメインメモリ上)
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
    ID3D12InfoQueue* infoQueue = nullptr;
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

        // 解放
        infoQueue->Release();
    }
#endif

    // ════════════════════════════════════════
    // コマンドキューを生成する
    // 命令群(CommandList)をGPUに投げて実行させる担当
    // ════════════════════════════════════════
    ID3D12CommandQueue* commandQueue = nullptr;
    // 設定用の構造体。今は既定値(DIRECT)のままでよいので中身は触らない
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue)); // (１．設定を書き込む構造体、２．生成したコマンドキューのアドレスを入れる変数）
    // コマンドキューの生成がうまくいかなかったので起動できない
    assert(SUCCEEDED(hr));


    // ════════════════════════════════════════
    // コマンドアロケータを生成する
    // 命令を書き溜めておくメモリそのものを管理する担当
    // ════════════════════════════════════════
    ID3D12CommandAllocator* commandAllocator = nullptr;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)); // (１．生成したアロケータのアドレスを入れる変数）
    // コマンドアロケータの生成がうまくいかなかったので起動できない
    assert(SUCCEEDED(hr));


    // ════════════════════════════════════════
    // コマンドリストを生成する
    // アロケータのメモリに命令を書き込んでいく担当
    // ════════════════════════════════════════
    ID3D12GraphicsCommandList* commandList = nullptr;
    hr = device->CreateCommandList(
        0,                                   // 
        D3D12_COMMAND_LIST_TYPE_DIRECT,      // アロケーターと同じ設定にする
        commandAllocator,                    // どのノートに書くか。紐付け   
        nullptr,                             // 初期パイプラインステート、後々設定していく   
        IID_PPV_ARGS(&commandList));         // 生成したコマンドリストのアドレスを入れる変数

    // コマンドリストの生成がうまくいかなかったので起動できない
    assert(SUCCEEDED(hr));

    // ════════════════════════════════════════
   // スワップチェーンを生成する
   // 描いた絵をモニタに映すための「表と裏の画面」を用意する担当
   // ════════════════════════════════════════
    IDXGISwapChain4* swapChain = nullptr;
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
        commandQueue,      // どのキューの描画完了を待つか
        hwnd,              // どのウィンドウに映すか
        &swapChainDesc,    // スワップチェーン設定書
        nullptr,           // フルスクリーン設定(既定通りでいい）
        nullptr,           // 出力先モニタ設定(既定通りでいい）
        reinterpret_cast<IDXGISwapChain1**>(&swapChain)); // 生成したスワップチェーンのアドレスを入れる変数
    assert(SUCCEEDED(hr));

    // ════════════════════════════════════════
    // ディスクリプタヒープの生成
    // 「このリソースをこう使う」という札(Descriptor)を並べておく棚
    // ════════════════════════════════════════
    // RTV用のヒープでディスクリプタの数は2。RTVはShader内で触るものではないので、ShaderVisibleはfalse
    ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

    // SRV用のヒープでディスクリプタの数は128。SRVはShader内で触るものなので、ShaderVisibleはtrue
    // SRV、CBV、UAVを一緒に格納できるディスクリプターヒープ　（入れられるViewの種類が多い分、サイズが大きくなる）
    ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

    // ════════════════════════════════════════
    // SwapChainからResourceを引っ張ってくる
    // SwapChainが内部に作った2枚の画面そのものを受け取る
    // ════════════════════════════════════════
    ID3D12Resource* swapChainResources[2] = { nullptr };
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
    // ディスクリプタの先頭を取得する
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    // RTVを2つ作るのでディスクリプタを2つ用意
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
    // まず1つ目を作る。1つ目は最初のところに作る。作る場所をこちらで指定してあげる必要がある
    rtvHandles[0] = rtvStartHandle;
    device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]); // (１．どのリソースに対してのView、２．描画の仕方の設定　３．生成されるViewの格納場所）
    // 2つ目のディスクリプタハンドルを得る(自力で)
    rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); // ディスクリプタのサイズを＋　（Viewタイプによって引数が変わる）
    // 2つ目を作る
    device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]); // (１．どのリソースに対してのView、２．描画の仕方の設定　３．生成されるViewの格納場所）


    // ════════════════════════════════════════
   // FenceとEventを生成する
   // GPUの実行完了をCPU側で待つための仕組み
   // ════════════════════════════════════════
   // 初期値0でFenceを作る
    ID3D12Fence* fence = nullptr;
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
    IDxcUtils* dxcUtils = nullptr; // dxcUtils が hlsl を読んでメモリに置く <<(補助機)>>
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)); //(１，何を生成するか . ２，アドレスの格納先)
    assert(SUCCEEDED(hr));


    IDxcCompiler3* dxcCompiler = nullptr; // dxcCompiler が arguments の設定で翻訳する　<<(翻訳機>>
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)); //(１，何を生成するか . ２，アドレスの格納先)
    assert(SUCCEEDED(hr));


    // 現時点でincludeはしないが、includeに対応するための設定を行っておく
    IDxcIncludeHandler* includeHandler = nullptr; //  途中で #include があれば、DXCに呼ばれた includeHandler が探しに行く（扱
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
    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
    rootParameters[0].Descriptor.ShaderRegister = 0;                     // レジスタ番号0とバインド
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;  // PixelShaderで使う
    rootParameters[1].Descriptor.ShaderRegister = 0;                     // レジスタ番号0とバインド
    descriptionRootSignature.pParameters = rootParameters;               // ルートパラメータ配列へのポインタ
    descriptionRootSignature.NumParameters = _countof(rootParameters);   // 配列の長さ

    // descriptionRootSignature が　パラメーターが格納されていく箱（パラメーターが増えるごとに大きくなる）
   
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
    ID3DBlob* signatureBlob = nullptr; // 成功したときのバイナリを受け取る変数
    ID3DBlob* errorBlob = nullptr; // 失敗したときのエラーメッセージを受け取る変数

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
    ID3D12RootSignature* rootSignature = nullptr;
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
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[1] = {};                     // 頂点の1つの要素（メンバ1つ分）の説明の配列。今はpositionだけなので1

                                                                            // <HLSL 側>	                <C++ 側>	        <値>
    inputElementDescs[0].SemanticName = "POSITION";                         // POSITION	                SemanticName	"POSITION"
    inputElementDescs[0].SemanticIndex = 0;                                 // 0（POSITION0 の数字部分）	SemanticIndex	0
#pragma region MyRegion
//   HLSL の POSITION0 を、名前 "POSITION" と番号 0 に分けて指定
//   番号が分かれているのは、同じ種類のデータを複数持つことがあるからです。
/*
    <hlsl>
    float32_t2 uv0 : TEXCOORD0;   // SemanticName = "TEXCOORD", SemanticIndex = 0
    float32_t2 uv1 : TEXCOORD1;   // SemanticName = "TEXCOORD", SemanticIndex = 1
*/
#pragma endregion
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;           //「このデータは何ビットの何型が何個か(RGBAで表現）」  <<<<float が 4つ = float32_t4 = 16バイト 
    inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;  // (この要素は、頂点1個の先頭から何バイト目にあるか = 前の要素の直後に置いて);

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
    // Shaderをコンパイルする(各シェーダーをGPUが読めるように翻訳）
    // ════════════════════════════════════════
    IDxcBlob* vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl",
        L"vs_6_0", dxcUtils, dxcCompiler, includeHandler, logStream);
    assert(vertexShaderBlob != nullptr);

    IDxcBlob* pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl",
        L"ps_6_0", dxcUtils, dxcCompiler, includeHandler, logStream);
    assert(pixelShaderBlob != nullptr);


    // ════════════════════════════════════════
    // PSOを生成する（すべての設計書の集大成！！！！！！！！！！！！！）
    // ════════════════════════════════════════
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature;          // RootSignature
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
    // 実際に生成
    ID3D12PipelineState* graphicsPipelineState = nullptr;
    hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
        IID_PPV_ARGS(&graphicsPipelineState)); // 生成したPSOのアドレスをgraphicsPipelineStateに格納
    assert(SUCCEEDED(hr));

    // ════════════════════════════════════════
    // VertexResourceを生成する
    // 頂点データを置くためのGPUから読めるメモリ(箱)を確保する
    // ════════════════════════════════════════
    // 実際に頂点リソースを作る
    ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(Vector4) * 3); // これから入れるデータのサイズを考えてサイズ指定しようね
  
    
    // ════════════════════════════════════════
    // VertexBufferViewを作成する
    // 頂点リソースの読み方を伝えるメモ。DescriptorHeapには置かない >>>> 描画時にコマンドリストへ直接渡します
    // ════════════════════════════════════════
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    // リソースの先頭のアドレスから使う
    vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
    // 使用するリソースのサイズは頂点3つ分のサイズ
    vertexBufferView.SizeInBytes = sizeof(Vector4) * 3;
    // 1頂点あたりのサイズ
    vertexBufferView.StrideInBytes = sizeof(Vector4);


    // ════════════════════════════════════════
    // Resourceにデータを書き込む
    // ════════════════════════════════════════
    Vector4* vertexData = nullptr;
    // 書き込むためのアドレスを取得
    vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData)); // CPUからでもデータを書ける住所をvertexDataに受け取る
    // 左下
    vertexData[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
    // 上
    vertexData[1] = { 0.0f, 0.5f, 0.0f, 1.0f };
    // 右下
    vertexData[2] = { 0.5f, -0.5f, 0.0f, 1.0f };



    // ════════════════════════════════════════
    // Material用のResourceを作る
    // 今回はcolor1つ分のサイズを用意する
    // ════════════════════════════════════════
    ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Vector4));
    // マテリアルにデータを書き込む
    Vector4* materialData = nullptr;
    // 書き込むためのアドレスを取得
    materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData)); // CPUからでもデータを書ける住所をmaterialDataに受け取る
    // 今回は赤を書き込んでみる
    *materialData = Vector4{ 1.0f, 0.0f, 0.0f, 1.0f };


    // ════════════════════════════════════════
    // TransformationMatrix用のResourceを作る
    // Matrix4x4 1つ分のサイズを用意する
    // ════════════════════════════════════════
    ID3D12Resource* wvpResource = CreateBufferResource(device, sizeof(Matrix4x4));
    // データを書き込む
    Matrix4x4* wvpData = nullptr;
    // 書き込むためのアドレスを取得
    wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
    // 単位行列を書きこんでおく
    *wvpData = MakeIdentity4x4();

    // Transform変数を作る
    Transform transform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    // カメラのTransformを作る。z = -5 の位置で z+ 方向を向いているカメラ
    Transform cameraTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };
#pragma region  MyRegion
    /*
    ??? なぜ行列を直接持たないのか ???

    ＞＞＞行列（16個の数値）を直接持っていると、「今どれくらい回転しているか」が人間には読めません。

    cpp
    // ✕ 行列を直接持つ
    Matrix4x4 world;   // 中身を見ても回転量が分からない。少しだけ回したい、もできない

    // ○ Transform を持つ
    transform.rotate.y += 0.03f;   // 「Y軸に0.03ラジアン足す」と読める
    */
#pragma endregion
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
    ImGui_ImplDX12_Init(device,
        swapChainDesc.BufferCount,
        rtvDesc.Format,
        srvDescriptorHeap,
        srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Build();
#endif
   


    MSG msg{};
    // ウィンドウの×ボタンが押されるまでループ
    while (msg.message != WM_QUIT) {
        // Windowにメッセージが来てたら最優先で処理させる
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            // ゲームの処理

            // ────────────────────────────────────────
            // ImGui開始処理
            // ────────────────────────────────────────
#ifdef USE_IMGUI
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
#endif


            // ────────────────────────────────────────
            // 三角形の回転
            // ────────────────────────────────────────
            transform.rotate.y += 0.03f;
            Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate); // 拡縮、回転、移動
            Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate); // ワールド基準で、カメラはどこにいるか
            Matrix4x4 viewMatrix = Inverse(cameraMatrix);  // カメラ基準で、ワールドはどこにあるか(ワールドをカメラに合わせる）
            /*
              やりたいこと    ： カメラ基準の座標が欲しい
              計算方法       ： 世界全体を、カメラが原点に来るようにずらす
              結果          ： ずらした後の座標 ＝ カメラ基準の座標
            */

            Matrix4x4 projectionMatrix = MakePerspectiveMatrix(0.45f,
                float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
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
            *wvpData = MakeWVPMatrix(worldMatrix, viewMatrix, projectionMatrix); // これらの行列を掛け合わせる

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
            barrier.Transition.pResource = swapChainResources[backBufferIndex];
            // 遷移前(現在)のResourceState
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; // スワップチェーンの持つ２つのリソースは初期状態 PRESENT
            // 遷移後のResourceState
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            // TransitionBarrierを張る
            commandList->ResourceBarrier(1, &barrier);


            // 描画先のRTVを設定する
            commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);
            // 指定した色で画面全体をクリアする
            float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; // 青っぽい色。RGBAの順
            commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

            //srvDescriptorHeapは　CBV_SRV_UAV　をまとめて管理する（SAMPLERヒープは別で descriptorHeaps[] に格納）
            ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
            commandList->SetDescriptorHeaps(1, descriptorHeaps); // srvDescriptorHeapを設定 = 格納されたCBV, SRV, UAVをシェーダーに見せれる

            // ────────────────────────────────────────
            // 三角形を描画する
            // ────────────────────────────────────────
            commandList->RSSetViewports(1, &viewport);          // Viewportを設定　＞＞＞　個数 + 配列」の形なのは、画面分割などで複数設定できるようにするため
            commandList->RSSetScissorRects(1, &scissorRect);    // Scissorを設定   ＞＞＞　個数 + 配列」の形なのは、画面分割などで複数設定できるようにするため
            // RootSignatureを設定。PSOに設定しているけど別途設定が必要
            commandList->SetGraphicsRootSignature(rootSignature);
            commandList->SetPipelineState(graphicsPipelineState);   // PSOを設定
            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);   // VBVを設定
            // 形状を設定。PSOに設定しているものとはまた別。同じものを設定すると考えておけば良い
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
#pragma region  MyRegion
            /*
            ＜値＞	        ＜頂点の使い方＞
            TRIANGLELIST	3つずつ区切って、独立した三角形にする
            TRIANGLESTRIP	1つずらしながら、前の2頂点と組み合わせて連続した三角形にする
            */
#pragma endregion

            commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[0]を介して、materialResourceアドレスを渡す
#pragma region  MyRegion
            /*
            rootParameters配列の０番目に紐付けられたシェーダーのレジスタに、         （今回はPSシェーダーのgMaterial:b0)
            第二引数のリソース本体のアドレスを送るぜよ                           （ここにコードで指定した色データが入っており、シェーダのmain関数へ）
            */
#pragma endregion
            commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress()); // シェーダーにルートパラメータ[1]を介して、wvpResourceアドレスを渡す


            // 描画！(DrawCall/ドローコール)。3頂点で1つのインスタンス。インスタンスについては今後
            commandList->DrawInstanced(3, 1, 0, 0);
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
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList); // ImGui::Renderのデータと使っているコマンドリストを引数に
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
            ID3D12CommandList* commandLists[] = { commandList };
            commandQueue->ExecuteCommandLists(1, commandLists);

            // GPUとOSに画面の交換を行うよう通知する
            swapChain->Present(1, 0); // 描き終わったら表と入れ替えてっていう命令


            // ────────────────────────────────────────
            // GPUの実行完了を待つ(この後Resetするので、確実に待ってから行う)
            // ────────────────────────────────────────
            // Fenceの値を更新
            fenceValue++;
            // GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
            commandQueue->Signal(fence, fenceValue);

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
            hr = commandList->Reset(commandAllocator, nullptr);
            assert(SUCCEEDED(hr));
        }
    }

    // ════════════════════════════════════════
    // 解放処理(生成と逆の順番で解放する)
    // ════════════════════════════════════════
#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
#endif
    wvpResource->Release();
    materialResource->Release();
    vertexResource->Release();
    graphicsPipelineState->Release();
    signatureBlob->Release();
    if (errorBlob) {
        errorBlob->Release();
    }
    rootSignature->Release();
    pixelShaderBlob->Release();
    vertexShaderBlob->Release();
    CloseHandle(fenceEvent);   // EventはCOMではないのでReleaseではなくCloseHandle
    fence->Release();
    srvDescriptorHeap->Release();
    rtvDescriptorHeap->Release();
    swapChainResources[0]->Release();
    swapChainResources[1]->Release();
    swapChain->Release();
    commandList->Release();
    commandAllocator->Release();
    commandQueue->Release();
    device->Release();
    useAdapter->Release();
    dxgiFactory->Release();
#ifdef _DEBUG
    debugController->Release();
#endif
    CloseWindow(hwnd);   // Win32APIのオブジェクトなのでCloseWindow


    // ════════════════════════════════════════
    // リソースリークチェック
    // 解放し忘れたオブジェクトが残っていないか、DXGIに問い合わせる
    // ════════════════════════════════════════
    IDXGIDebug1* debug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        debug->Release();
    }
    return 0;
}
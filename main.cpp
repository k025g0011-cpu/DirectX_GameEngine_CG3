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

// Debug用のあれやこれやを使えるようにする
#include <dbghelp.h>
// StringCchPrintfWを使うため
#include <strsafe.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
    WPARAM wparam, LPARAM lparam) {
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
    // ディスクリプタヒープの生成(RTV用)
    // 「このリソースをこう使う」という札(Descriptor)を並べておく棚
    // ════════════════════════════════════════
    ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc{};
    rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // レンダーターゲットビュー用
    rtvDescriptorHeapDesc.NumDescriptors = 2;                    // ダブルバッファ用に2つ。多くても別に構わない
    hr = device->CreateDescriptorHeap(
        &rtvDescriptorHeapDesc, // ヒープの設定
        IID_PPV_ARGS(&rtvDescriptorHeap)); // 生成したRTVディスクリプターヒープのアドレスを入れる変数
    // ディスクリプタヒープが作れなかったので起動できない
    assert(SUCCEEDED(hr));

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


    // ────────────────────────────────────────
    // CrashHandlerの動作確認用(確認したらコメントアウトに戻す)
    // 「デバッグなしで実行」で起動すること
    // ────────────────────────────────────────
    //uint32_t* p = nullptr;
    //*p = 100;

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
    swapChainResources[0]->Release();
    swapChainResources[1]->Release();
    rtvDescriptorHeap->Release();
    swapChain->Release();
    commandList->Release();
    commandAllocator->Release();
    commandQueue->Release();
    device->Release();
    useAdapter->Release();
    dxgiFactory->Release();

    return 0;
}
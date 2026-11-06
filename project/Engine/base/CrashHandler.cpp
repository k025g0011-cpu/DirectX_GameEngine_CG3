#include "CrashHandler.h"

#include <Windows.h>
#include <dbghelp.h>     // MiniDumpWriteDump
#include <strsafe.h>     // StringCchPrintfW
#include <d3d12.h>     
#include <dxgi1_3.h>  
#include <dxgidebug.h>   // IDXGIDebug1
#include <wrl.h>

// このファイルが必要とするライブラリを、このファイルで指定する
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")

// ════════════════════════════════════════════════════════════════════
// このファイルの中だけで使う関数
// ════════════════════════════════════════════════════════════════════
namespace {

	// ════════════════════════════════════════
	// 誰も SEH の例外を捕捉しなかった場合に実行され、ダンプファイルを出力する
	//   ※ この関数を呼ぶコードはどこにも無い。Windows が呼ぶ（コールバック）
	// ════════════════════════════════════════
	LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
		// 時刻を取得して、時刻を名前に入れたファイルを Dumps ディレクトリ以下に作成
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

		// 他に関連づけられている SEH 例外ハンドラがあれば実行。通常はプロセスを終了する
		return EXCEPTION_EXECUTE_HANDLER;
	}

}   // namespace

// ════════════════════════════════════════════════════════════════════
// クラッシュハンドラの登録
// ════════════════════════════════════════════════════════════════════
void RegisterCrashHandler() {
	// 誰も捕捉しなかった場合に(Unhandled)、捕捉する関数を登録する
	SetUnhandledExceptionFilter(ExportDump);
}

// ════════════════════════════════════════════════════════════════════
// リソースリークの検出
//   デストラクタなので、WinMain を抜けるときに自動で走る
// ════════════════════════════════════════════════════════════════════
D3DResourceLeakChecker::~D3DResourceLeakChecker() {
	// 解放し忘れたオブジェクトが残っていないか、DXGI に問い合わせる
	Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
	}
}
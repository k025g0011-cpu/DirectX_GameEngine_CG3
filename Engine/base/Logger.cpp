#include "Logger.h"

#include <Windows.h>

// ════════════════════════════════════════════════════════════════════
// 文字コード変換
//   Windows API は wchar_t（UTF-16）、C++ の標準は char（UTF-8）で
//   世界が分かれているので、境界でこの2つを行き来する必要がある
// ════════════════════════════════════════════════════════════════════

// string -> wstring
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	// 第5引数を 0 にすると「変換後に必要な文字数」を返してくれる（1回目の呼び出し）
	auto sizeNeeded = MultiByteToWideChar(
		CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]),
		static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}

	// 必要なぶんだけ確保してから、本番の変換を行う（2回目の呼び出し）
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(
		CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]),
		static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

// wstring -> string
std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(
		CP_UTF8, 0, str.data(), static_cast<int>(str.size()),
		NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}

	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(
		CP_UTF8, 0, str.data(), static_cast<int>(str.size()),
		result.data(), sizeNeeded, NULL, NULL);
	return result;
}

// ════════════════════════════════════════════════════════════════════
// ログ出力
//   ファイル（os）と Visual Studio の出力ウィンドウの両方に出す
// ════════════════════════════════════════════════════════════════════

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA((message + "\n").c_str());
}

void Log(std::ostream& os, const std::wstring& message) {
	// wstring から string に変換して、string 版の Log を呼ぶ
	Log(os, ConvertString(message));
}
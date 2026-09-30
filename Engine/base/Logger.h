#pragma once

#include <ostream>
#include <string>

// ════════════════════════════════════════════════════════════════════
// ログ出力
//   ファイルとデバッグ出力の両方に書き込む
//   定義は main.cpp にある
// ════════════════════════════════════════════════════════════════════
void Log(std::ostream& os, const std::string& message);
void Log(std::ostream& os, const std::wstring& message);

// ════════════════════════════════════════════════════════════════════
// 文字コードの変換
//   Windows API は wstring、ログや OBJ ファイルは string を使うので橋渡しが要る
// ════════════════════════════════════════════════════════════════════
std::string  ConvertString(const std::wstring& str);
std::wstring ConvertString(const std::string& str);
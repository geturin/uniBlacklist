#pragma once
#include <string>

enum class UbLanguage { Chinese, English };

UbLanguage UbLoadLanguage();
void UbSaveLanguage(UbLanguage language);
// Display translation only. Keep the original exception text in diagnostics.
std::wstring UbTranslateError(const std::wstring& message, UbLanguage language);

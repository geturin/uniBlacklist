#pragma once
#include <stdint.h>
#include <string>
#include <vector>
struct UbEntry {uint64_t id;std::wstring alias;};
std::wstring UbSettingsPath();
std::vector<UbEntry> UbLoadSettings();
void UbSaveSettings(const std::vector<UbEntry>& entries);
uint64_t UbParseId(const std::wstring& text);
std::wstring UbWide(const std::string& s);
std::string UbUtf8(const std::wstring& s);

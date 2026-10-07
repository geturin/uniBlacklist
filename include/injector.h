#pragma once
#include <windows.h>
#include <stdexcept>
#include <string>
#include <vector>
struct UbProcess { DWORD pid; std::wstring path; };
std::vector<UbProcess> UbFindGames();
DWORD UbAttach(DWORD pid);
std::wstring UbErrorText(DWORD code);

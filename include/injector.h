#pragma once
#include <windows.h>
#include <stdexcept>
#include <string>
#include <vector>
struct UbProcess { DWORD pid; std::wstring path; };
std::vector<UbProcess> UbFindGames();
DWORD UbAttach(DWORD pid);
std::wstring UbErrorText(DWORD code);

struct UbModule {uint32_t base=0,bytes=0;std::wstring name;};
std::vector<UbModule> UbListModules(DWORD pid);

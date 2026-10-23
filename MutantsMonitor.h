#pragma once

#include <string>
#include <set>

bool InitializeNtFunctions();

std::set<std::wstring> EnumerateNamedMutexes();
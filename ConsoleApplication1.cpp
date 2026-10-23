#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <io.h>
#include <fcntl.h>
#include <set>
#include <string>

#include "MutantsMonitor.h"

int main() {
    _setmode(_fileno(stdout), _O_U16TEXT);

    if (!InitializeNtFunctions()) {
        std::wcerr << L"ntdll.dll не загрузилась" << std::endl;
        return 1;
    }

    int intervalMs = 2000;

    std::set<std::wstring> knownMutexes = EnumerateNamedMutexes();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        std::set<std::wstring> currentMutexes = EnumerateNamedMutexes();

        for (const auto& name : currentMutexes) {
            if (knownMutexes.find(name) == knownMutexes.end()) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                wprintf(L"[%02d:%02d:%02d] Именованный мьютекс: %s\n",
                    st.wHour, st.wMinute, st.wSecond, name.c_str());
            }
        }
        knownMutexes = std::move(currentMutexes);
    }

    return 0;
}
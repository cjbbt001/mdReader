#include <windows.h>
#include <psapi.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: bench <exe> [file]\n"); return 1; }
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "%s %s", argv[1], argc > 2 ? argv[2] : "");

    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    ULONGLONG t0 = GetTickCount64();
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("CreateProcess failed (%lu)\n", GetLastError());
        return 1;
    }

    ULONGLONG tvis = 0;
    for (int i = 0; i < 3000; i++) {
        HWND h = FindWindowW(L"MDReaderWindow", NULL);
        if (h && IsWindowVisible(h)) { tvis = GetTickCount64() - t0; break; }
        Sleep(1);
    }

    Sleep(500);
    HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pi.dwProcessId);
    PROCESS_MEMORY_COUNTERS pmc = {0};
    pmc.cb = sizeof pmc;
    if (hp) GetProcessMemoryInfo(hp, &pmc, sizeof pmc);

    printf("window visible: %llu ms   working set: %llu MB   private: %llu MB\n", tvis,
           pmc.WorkingSetSize / 1048576ULL, pmc.PagefileUsage / 1048576ULL);
    if (hp) CloseHandle(hp);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

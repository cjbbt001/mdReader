#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <wchar.h>

struct WindowSearch {
    DWORD pid;
    HWND window;
};

static BOOL CALLBACK findWindow(HWND window, LPARAM data) {
    struct WindowSearch* search = (struct WindowSearch*)data;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == search->pid && IsWindowVisible(window)) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        fputs("usage: bench <exe> <test Markdown file>\n", stderr);
        return 1;
    }
    wchar_t command[32768];
    if (swprintf(command, ARRAYSIZE(command), L"\"%ls\" \"%ls\"", argv[1], argv[2]) < 0)
        return 1;

    STARTUPINFOW startup = {sizeof startup};
    PROCESS_INFORMATION child;
    LARGE_INTEGER frequency, start, visible;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    if (!CreateProcessW(argv[1], command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child)) {
        fprintf(stderr, "CreateProcess failed (%lu)\n", GetLastError());
        return 1;
    }

    struct WindowSearch search = {child.dwProcessId, NULL};
    for (int i = 0; i < 500 && !search.window; ++i) {
        EnumWindows(findWindow, (LPARAM)&search);
        if (search.window || WaitForSingleObject(child.hProcess, 2) == WAIT_OBJECT_0) break;
    }
    QueryPerformanceCounter(&visible);
    Sleep(1000);
    PROCESS_MEMORY_COUNTERS_EX memory = {0};
    memory.cb = sizeof memory;
    int ok = search.window && GetProcessMemoryInfo(child.hProcess,
                            (PROCESS_MEMORY_COUNTERS*)&memory, sizeof memory);
    if (ok)
        printf("%.1f ms, %.1f MiB working set, %.1f MiB private\n",
               (visible.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart,
               memory.WorkingSetSize / 1048576.0, memory.PrivateUsage / 1048576.0);
    else
        fputs("Benchmark could not locate the new process window. Close any running MdReader instance first.\n", stderr);

    // Benchmark fixture only. Avoid saving its path into the user's session.
    if (search.window) {
        TerminateProcess(child.hProcess, 0);
        WaitForSingleObject(child.hProcess, 3000);
    }
    CloseHandle(child.hProcess);
    CloseHandle(child.hThread);
    return ok ? 0 : 1;
}

// Windows only: start a program with no terminal and no console window (the launcher's "run in the background").
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

int vtmux_spawn_detached(const char *line)
{
    char cmd[512];
    STARTUPINFOA si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi;

    snprintf(cmd, sizeof(cmd), "cmd.exe /c %s", line);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) return -1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

unsigned long long vtmux_tick_ms(void)
{
    return GetTickCount64();
}

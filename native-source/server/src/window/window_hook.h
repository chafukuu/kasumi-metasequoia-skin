#include <Windows.h>

inline HHOOK g_hHook = NULL;
inline HHOOK g_mouseHook = NULL;

void InitServerCapsLockState();
int GetServerCapsLockState();
// Adopts a Caps Lock state observed by a TSF client, which reads it on the
// foreground thread. Corrects drift the hook cannot see.
void SyncServerCapsLockState(bool enabled);

// Exits with the Watchdog's restart code so the Server comes back immediately.
[[noreturn]] void RestartServerProcess();

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD);
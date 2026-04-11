// Mora SKSE Runtime Plugin — placeholder
// Full implementation in subsequent tasks

#ifdef _WIN32
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
#else
// Non-Windows stub for compilation testing
namespace mora { void runtime_stub() {} }
#endif

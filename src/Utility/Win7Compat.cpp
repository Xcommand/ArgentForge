// -----------------------------------------------------------------------------
//
// Three Windows 8 calls are wired straight into the C++ runtime that links into
// us. The loader looks them up in kernel32 before any of our code runs, so on
// Windows 7 the app dies with "CopyFile2 not found in KERNEL32.dll" instead of
// opening. These stand-ins answer the call themselves: they pass the work to the
// real function wherever it exists, and to the older equivalent where it doesn't.
//
// Our own code never calls any of this, so the linker would drop the file: the
// __imp_ pointers are what the runtime actually imports, and defining them here
// keeps kernel32's own entries out of the exe.
//
// -----------------------------------------------------------------------------

#ifdef _WIN32
#include <windows.h>

namespace
{
// Looked up by name so the real call never lands in the import table. The
// initialiser here is a constant, so no thread guard is emitted: two threads
// can race through and both will find the same pointer.
template<typename T>
T kernel32(const char* name)
{
	static T real = reinterpret_cast<T>(-1);
	if (real == reinterpret_cast<T>(-1))
		real = reinterpret_cast<T>(::GetProcAddress(::GetModuleHandleA("kernel32.dll"), name));
	return real;
}

BOOL win7_fail(DWORD error)
{
	::SetLastError(error);
	return FALSE;
}

typedef BOOL(WINAPI* copy_file2_func)(LPCWSTR, LPCWSTR, const COPYFILE2_EXTENDED_PARAMETERS*);
typedef HANDLE(WINAPI* create_file2_func)(
	LPCWSTR, DWORD, DWORD, DWORD, DWORD, const CREATEFILE2_EXTENDED_PARAMETERS*);
typedef void(WINAPI* precise_time_func)(LPFILETIME);

// -----------------------------------------------------------------------------
// CopyFile2: the runtime only ever asks for fail-if-exists, which is what the
// boolean in CopyFileExW does. Copying a symlink as a link needs the reparse
// data, which Win7 has no way to ask for, so that one says so rather than
// quietly copying the file the link points at.
// -----------------------------------------------------------------------------
BOOL WINAPI slade_CopyFile2(LPCWSTR from, LPCWSTR to, const COPYFILE2_EXTENDED_PARAMETERS* params)
{
	if (auto real = kernel32<copy_file2_func>("CopyFile2"))
		return real(from, to, params);

	DWORD flags = params ? params->dwCopyFlags : 0;
	if (flags & COPY_FILE_COPY_SYMLINK)
		return win7_fail(ERROR_NOT_SUPPORTED);

	return ::CopyFileExW(from, to, nullptr, nullptr, nullptr, (flags & COPY_FILE_FAIL_IF_EXISTS) ? TRUE : FALSE);
}

// -----------------------------------------------------------------------------
// CreateFile2: same six arguments, minus the security QoS flags that only
// Windows 8 can act on.
// -----------------------------------------------------------------------------
HANDLE WINAPI slade_CreateFile2(
	LPCWSTR name, DWORD access, DWORD share, DWORD disposition, DWORD flags,
	const CREATEFILE2_EXTENDED_PARAMETERS* params)
{
	if (auto real = kernel32<create_file2_func>("CreateFile2"))
		return real(name, access, share, disposition, flags, params);

	LPSECURITY_ATTRIBUTES security      = nullptr;
	HANDLE                template_file = nullptr;
	if (params)
	{
		flags |= params->dwFileAttributes | params->dwFileFlags;
		security      = params->lpSecurityAttributes;
		template_file = params->hTemplateFile;
	}

	return ::CreateFileW(name, access, share, security, disposition, flags, template_file);
}

// -----------------------------------------------------------------------------
// GetSystemTimePreciseAsFileTime: the sub-microsecond clock. Win7's coarser one
// is the same value in the same units, just no finer than the timer tick.
// -----------------------------------------------------------------------------
extern "C" void WINAPI slade_GetSystemTimePreciseAsFileTime(LPFILETIME time)
{
	if (auto real = kernel32<precise_time_func>("GetSystemTimePreciseAsFileTime"))
		return real(time);

	::GetSystemTimeAsFileTime(time);
}

} // namespace

extern "C"
{
	void* __imp_CopyFile2                     = reinterpret_cast<void*>(&slade_CopyFile2);
	void* __imp_CreateFile2                   = reinterpret_cast<void*>(&slade_CreateFile2);
	void* __imp_GetSystemTimePreciseAsFileTime = reinterpret_cast<void*>(&slade_GetSystemTimePreciseAsFileTime);
}

#endif // _WIN32

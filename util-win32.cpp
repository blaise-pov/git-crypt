/*
 * Copyright 2014 Andrew Ayer
 *
 * This file is part of git-crypt.
 *
 * git-crypt is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * git-crypt is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with git-crypt.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Additional permission under GNU GPL version 3 section 7:
 *
 * If you modify the Program, or any covered work, by linking or
 * combining it with the OpenSSL project's OpenSSL library (or a
 * modified version of that library), containing parts covered by the
 * terms of the OpenSSL or SSLeay licenses, the licensors of the Program
 * grant you additional permission to convey the resulting work.
 * Corresponding Source for a non-source form of such a combination
 * shall include the source code for the parts of OpenSSL used as well
 * as that of the covered work.
 */

#include <io.h>
#include <stdio.h>
#include <fcntl.h>
#include <windows.h>
#include <vector>
#include <cstring>
#include <string>
#include <locale>
#include <codecvt>
#include <fstream>

// UTF-8 to UTF-16 conversion helper
std::wstring utf8_to_utf16 (const std::string& utf8)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (len <= 0) {
		return std::wstring();
	}
	std::wstring utf16(len - 1, L'\0'); // len includes null terminator
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &utf16[0], len);
	return utf16;
}

// UTF-16 to UTF-8 conversion helper
std::string utf16_to_utf8 (const std::wstring& utf16)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0) {
		return std::string();
	}
	std::string utf8(len - 1, '\0'); // len includes null terminator
	WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), -1, &utf8[0], len, nullptr, nullptr);
	return utf8;
}

std::string System_error::message () const
{
	std::string	mesg(action);
	if (!target.empty()) {
		mesg += ": ";
		mesg += target;
	}
	if (error) {
		LPWSTR	error_message = nullptr;
		FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPWSTR>(&error_message),
			0,
			nullptr);

		// Convert error message from UTF-16 to UTF-8
		int len = WideCharToMultiByte(CP_UTF8, 0, error_message, -1, nullptr, 0, nullptr, nullptr);
		if (len > 0) {
			std::vector<char> buffer(len);
			WideCharToMultiByte(CP_UTF8, 0, error_message, -1, &buffer[0], len, nullptr, nullptr);
			mesg += &buffer[0];
		}
		LocalFree(error_message);
	}
	return mesg;
}

void	temp_fstream::open (std::ios_base::openmode mode)
{
	close();

	wchar_t			tmpdir[MAX_PATH + 1];

	DWORD			ret = GetTempPathW(sizeof(tmpdir) / sizeof(wchar_t), tmpdir);
	if (ret == 0) {
		throw System_error("GetTempPathW", "", GetLastError());
	} else if (ret > sizeof(tmpdir) / sizeof(wchar_t) - 1) {
		throw System_error("GetTempPathW", "", ERROR_BUFFER_OVERFLOW);
	}

	wchar_t			tmpfilename[MAX_PATH + 1];
	if (GetTempFileNameW(tmpdir, L"git-crypt", 0, tmpfilename) == 0) {
		throw System_error("GetTempFileNameW", "", GetLastError());
	}

	filename = utf16_to_utf8(tmpfilename);

	std::fstream::open(tmpfilename, mode);
	if (!std::fstream::is_open()) {
		DeleteFileW(tmpfilename);
		throw System_error("std::fstream::open", filename, 0);
	}
}

void	temp_fstream::close ()
{
	if (std::fstream::is_open()) {
		std::fstream::close();
		std::wstring	wfilename = utf8_to_utf16(filename);
		DeleteFileW(wfilename.c_str());
	}
}

void	mkdir_parent (const std::string& path)
{
	std::wstring	wpath = utf8_to_utf16(path);
	std::wstring::size_type		slash = 1;
	while ((slash = wpath.find_first_of(L"/\\", slash)) != std::wstring::npos) {
		std::wstring		prefix(wpath.substr(0, slash));
		if (GetFileAttributesW(prefix.c_str()) == INVALID_FILE_ATTRIBUTES) {
			// prefix does not exist, so try to create it
			if (!CreateDirectoryW(prefix.c_str(), nullptr)) {
				throw System_error("CreateDirectoryW", utf16_to_utf8(prefix), GetLastError());
			}
		}

		++slash;
	}
}

std::string our_exe_path ()
{
	std::vector<wchar_t>	buffer(128);

	while (true) {
		DWORD len = GetModuleFileNameW(nullptr, &buffer[0], buffer.size());
		if (len == 0) {
			throw System_error("GetModuleFileNameW", "", GetLastError());
		}
		if (len < buffer.size()) {
			// Success - convert to UTF-8
			return utf16_to_utf8(std::wstring(buffer.begin(), buffer.begin() + len));
		}
		// buffer was too small, resize and try again
		buffer.resize(buffer.size() * 2);
	}
}

int exit_status (int status)
{
	return status;
}

void	touch_file (const std::string& filename)
{
	std::wstring	wfilename = utf8_to_utf16(filename);
	HANDLE	fh = CreateFileW(wfilename.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (fh == INVALID_HANDLE_VALUE) {
		DWORD	error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND) {
			return;
		} else {
			throw System_error("CreateFileW", filename, error);
		}
	}
	SYSTEMTIME	system_time;
	GetSystemTime(&system_time);
	FILETIME	file_time;
	SystemTimeToFileTime(&system_time, &file_time);

	if (!SetFileTime(fh, nullptr, nullptr, &file_time)) {
		DWORD	error = GetLastError();
		CloseHandle(fh);
		throw System_error("SetFileTime", filename, error);
	}
	CloseHandle(fh);
}

void	remove_file (const std::string& filename)
{
	std::wstring	wfilename = utf8_to_utf16(filename);
	if (!DeleteFileW(wfilename.c_str())) {
		DWORD	error = GetLastError();
		if (error == ERROR_FILE_NOT_FOUND) {
			return;
		} else {
			throw System_error("DeleteFileW", filename, error);
		}
	}
}

static void	init_std_streams_platform ()
{
	_set_fmode(_O_BINARY);
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
	_setmode(_fileno(stderr), _O_BINARY);
}

void create_protected_file (const char* path)
{
	// TODO: implement with proper security attributes
	std::wstring	wpath = utf8_to_utf16(path);
	HANDLE	h = CreateFileW(wpath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h != INVALID_HANDLE_VALUE) {
		CloseHandle(h);
	}
}

int util_rename (const char* from, const char* to)
{
	std::wstring	wfrom = utf8_to_utf16(from);
	std::wstring	wto = utf8_to_utf16(to);
	// On Windows OS, it is necessary to ensure target file doesn't exist
	_wunlink(wto.c_str());
	return _wrename(wfrom.c_str(), wto.c_str());
}

std::vector<std::string> get_directory_contents (const char* path)
{
	std::vector<std::string>	filenames;
	std::wstring			wpath = utf8_to_utf16(path);

	if (!wpath.empty() && wpath[wpath.size() - 1] != L'/' && wpath[wpath.size() - 1] != L'\\') {
		wpath.push_back(L'\\');
	}
	wpath.push_back(L'*');

	WIN32_FIND_DATAW		ffd;
	HANDLE				h = FindFirstFileW(wpath.c_str(), &ffd);
	if (h == INVALID_HANDLE_VALUE) {
		throw System_error("FindFirstFileW", utf16_to_utf8(wpath), GetLastError());
	}
	do {
		if (wcscmp(ffd.cFileName, L".") != 0 && wcscmp(ffd.cFileName, L"..") != 0) {
			filenames.push_back(utf16_to_utf8(ffd.cFileName));
		}
	} while (FindNextFileW(h, &ffd) != 0);

	DWORD				err = GetLastError();
	if (err != ERROR_NO_MORE_FILES) {
		throw System_error("FindNextFileW", utf16_to_utf8(wpath), err);
	}
	FindClose(h);
	return filenames;
}

// Windows-specific helper for opening files with Unicode paths
// Returns a file descriptor that can be used with ifstream/ofstream
int open_file_utf8 (const char* path, int flags, int pmode)
{
	std::wstring	wpath = utf8_to_utf16(path);
	return _wopen(wpath.c_str(), flags, pmode);
}

// Windows-specific helper for checking if a file exists using Unicode paths
int access_utf8 (const char* path, int mode)
{
	std::wstring	wpath = utf8_to_utf16(path);
	DWORD attrs = GetFileAttributesW(wpath.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		return -1;
	}

	// Check if it's a directory
	if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
		// For directories, check read access based on attributes
		return (mode == F_OK) ? 0 : -1;
	}

	// Simple check - in a full implementation we'd check ACLs
	// For now, just check if file exists and is readable
	if (mode == F_OK || mode == R_OK) {
		return 0;
	}

	// For write access, check read-only attribute
	if (mode == W_OK) {
		return (attrs & FILE_ATTRIBUTE_READONLY) ? -1 : 0;
	}

	return -1;
}

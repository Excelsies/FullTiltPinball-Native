#pragma once
#include <string>

// Table text read from the user's EXE at run time. Our own reader: no game text lives in the repo.
// Two sources, both read from the EXE next to the DAT:
//  - RT_STRING resources (ids 2019-2025 and 3001-3076): any PE32 file;
//  - two .data strings (quotes, credits) at fixed virtual addresses: only for the EXE builds recorded in
//    table_text.cpp, after a file size + SHA-256 identity check. A failed check logs an error and the
//    getter returns nullptr (no fallback text).
enum class DataText
{
	Quotes,
	Credits,
};

namespace table_text
{
	// Replaces any earlier load. False (all getters nullptr, error on stderr) when the file is missing or is
	// not a PE32 image with a readable string table. True does not imply the .data strings are available:
	// ask GetData.
	bool Load(const std::string& exePath);
	// UTF-8 text of string resource `id`, or nullptr (not loaded, id not wanted, or empty in the EXE).
	// The pointer stays valid until the next Load.
	const char* Get(int id);
	// The whole `~`-separated string, or nullptr when the EXE failed the identity or content checks.
	const char* GetData(DataText which);
}

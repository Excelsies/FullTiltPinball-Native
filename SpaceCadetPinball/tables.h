#pragma once
#include <string>
#include <vector>

// Table catalogue. The catalogue entry decides which data files are loaded, the high-score key prefix and
// whether the Full Tilt gate pb::IsFullTiltTable() is open (Dragon's Keep and Skulduggery only).
enum class TableId
{
	Cadet,
	Dragon,
	Pirates,
	Pinball3D,
	Demo,
};

struct TableInfo
{
	TableId Id;
	const char* Name;           // catalogue id: -table=<Name>, option "Table"
	const char* Title;          // menu text
	const char* DatFile;        // upper case file name; the lower case spelling is tried too
	const char* SubFolders[3];  // probed under every search path, in order; "" = the search path itself; nullptr ends
	const char* ScoreKeyPrefix; // prepended to the high-score keys of the ini; "" keeps upstream's keys
	bool FullTilt;              // pb::FullTiltMode
	bool DemoData;              // pb::FullTiltDemoMode
};

namespace tables
{
	// The table chosen by the last Select() (Space Cadet before any selection or when nothing was found).
	const TableInfo& Current();

	// Full path of the EXE that sits next to the loaded DAT (Full Tilt tables), or an empty string when there is none.
	std::string ExePath();

	// Catalogue.
	const std::vector<TableInfo>& Catalogue();
	const TableInfo* Find(const char* name); // nullptr when unknown
	std::string IdList();                    // "cadet, dragon, pirates, 3dpb, demo"

	// Command line: finds "-table=<id>" in the line. Returns an empty string when the flag is absent or valid (a valid
	// id is remembered for the next Select()), else the error text (names the bad id and lists the valid ones).
	std::string ParseCommandLine(const char* cmdLine);

	// Table choice for pb::SelectDatFile. Order: -table=, option "Table", then upstream's order
	// (CADET, PINBALL / "Prefer 3DPB Game Data", DEMO) over all search paths, then DRAGON, PIRATES.
	// On success sets pb::DatFileName, pb::BasePath, pb::FullTiltMode, pb::FullTiltDemoMode and Current();
	// on failure leaves pb::DatFileName empty (pb::init then reports the missing data). Never falls back to another
	// table when a table was asked for by name.
	bool Select(const std::vector<const char*>& searchPaths);

	// True if the table's DAT is found in the search paths of the last Select() (for the Table menu).
	bool IsAvailable(const TableInfo& table);

	// Table menu: remember the choice in option "Table" and drop a -table= override. The caller restarts.
	void Choose(const TableInfo& table);
}

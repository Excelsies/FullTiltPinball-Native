#include "pch.h"
#include "tables.h"

#include "options.h"
#include "pb.h"

// The gate is open for the Full Tilt tables that need the engine additions (Dragon's Keep, Skulduggery); it is not
// pb::FullTiltMode, which upstream also sets for Full Tilt's own CADET.DAT.
static const TableInfo* CurrentTable = nullptr;
static const TableInfo* RequestedTable = nullptr; // -table= override of this launch
static std::vector<std::string> SearchPaths;       // copy of the paths of the last Select()

bool pb::IsFullTiltTable()
{
	return CurrentTable && (CurrentTable->Id == TableId::Dragon || CurrentTable->Id == TableId::Pirates);
}

const std::vector<TableInfo>& tables::Catalogue()
{
	static const std::vector<TableInfo> catalogue
	{
		{TableId::Cadet, "cadet", "Space Cadet", "CADET.DAT", {"", "CADET/", nullptr}, "", true, false},
		{TableId::Dragon, "dragon", "Dragon's Keep", "DRAGON.DAT", {"", "DRAGON/", nullptr}, "DragonsKeep.", true, false},
		{TableId::Pirates, "pirates", "Skulduggery", "PIRATES.DAT", {"", "PIRATES/", "PIRATE/"}, "Skulduggery.", true, false},
		{TableId::Pinball3D, "3dpb", "3D Pinball", "PINBALL.DAT", {"", nullptr, nullptr}, "", false, false},
		{TableId::Demo, "demo", "Demo", "DEMO.DAT", {"", nullptr, nullptr}, "", true, true},
	};
	return catalogue;
}

static const TableInfo& ById(TableId id)
{
	return tables::Catalogue()[static_cast<size_t>(id)];
}

const TableInfo& tables::Current()
{
	return CurrentTable ? *CurrentTable : ById(TableId::Cadet);
}

const TableInfo* tables::Find(const char* name)
{
	if (!name)
		return nullptr;
	for (const auto& table : Catalogue())
		if (strcmp(table.Name, name) == 0)
			return &table;
	return nullptr;
}

std::string tables::IdList()
{
	std::string list;
	for (const auto& table : Catalogue())
	{
		if (!list.empty())
			list += ", ";
		list += table.Name;
	}
	return list;
}

std::string tables::ParseCommandLine(const char* cmdLine)
{
	RequestedTable = nullptr;
	if (!cmdLine)
		return std::string();
	static const char flag[] = "-table=";
	auto pos = strstr(cmdLine, flag);
	if (!pos)
		return std::string();
	pos += sizeof flag - 1;
	std::string id;
	while (*pos && !isspace(static_cast<unsigned char>(*pos)))
		id += *pos++;
	RequestedTable = Find(id.c_str());
	if (!RequestedTable)
		return "Unknown table \"" + id + "\" in -table=. Valid tables: " + IdList();
	return std::string();
}

// Probes name and its lower case spelling under path + sub folder. Returns the file name that exists or "".
static std::string Probe(const std::string& path, const char* subFolder, const char* datFile)
{
	std::string fileName = datFile;
	for (int i = 0; i < 2; i++)
	{
		if (i == 1)
			std::transform(fileName.begin(), fileName.end(), fileName.begin(),
			               [](unsigned char c) { return std::tolower(c); });
		auto file = fopenu((path + subFolder + fileName).c_str(), "r");
		if (file)
		{
			fclose(file);
			return fileName;
		}
	}
	return std::string();
}

static bool Take(const TableInfo& table, const std::string& path, const char* subFolder, const std::string& fileName)
{
	pb::DatFileName = fileName;
	pb::BasePath = path + subFolder;
	pb::FullTiltMode = table.FullTilt;
	pb::FullTiltDemoMode = table.DemoData;
	CurrentTable = &table;
	printf("Loading game from: %s\n", (pb::BasePath + fileName).c_str());
	printf("Table: %s\n", table.Name);
	return true;
}

static bool FindIn(const TableInfo& table, bool useSubFolders)
{
	for (const auto& path : SearchPaths)
	{
		for (int i = 0; i < 3 && table.SubFolders[i]; i++)
		{
			if (i > 0 && !useSubFolders)
				break;
			auto fileName = Probe(path, table.SubFolders[i], table.DatFile);
			if (!fileName.empty())
				return Take(table, path, table.SubFolders[i], fileName);
		}
	}
	return false;
}

bool tables::Select(const std::vector<const char*>& searchPaths)
{
	pb::DatFileName.clear();
	pb::FullTiltDemoMode = pb::FullTiltMode = false;
	CurrentTable = nullptr;
	SearchPaths.clear();
	for (auto path : searchPaths)
		if (path)
			SearchPaths.push_back(path);

	// 1. -table=, 2. option "Table": a named table is looked for in all its places and nowhere else.
	const TableInfo* named = RequestedTable;
	if (!named && !options::Options.Table.V.empty())
	{
		named = Find(options::Options.Table.V.c_str());
		if (!named)
			printf("Ignoring unknown table \"%s\" in option Table.\n", options::Options.Table.V.c_str());
	}
	if (named)
		return FindIn(*named, true);

	// 3. Historic order over all search paths (flat layout only, as before), then the Full Tilt tables.
	const TableInfo* order[3] = {&ById(TableId::Cadet), &ById(TableId::Pinball3D), &ById(TableId::Demo)};
	if (options::Options.Prefer3DPBGameData)
		std::swap(order[0], order[1]);
	for (const auto& path : SearchPaths)
	{
		for (auto table : order)
		{
			auto fileName = Probe(path, "", table->DatFile);
			if (!fileName.empty())
				return Take(*table, path, "", fileName);
		}
	}
	return FindIn(ById(TableId::Dragon), true) || FindIn(ById(TableId::Pirates), true);
}

bool tables::IsAvailable(const TableInfo& table)
{
	for (const auto& path : SearchPaths)
		for (int i = 0; i < 3 && table.SubFolders[i]; i++)
			if (!Probe(path, table.SubFolders[i], table.DatFile).empty())
				return true;
	return false;
}

void tables::Choose(const TableInfo& table)
{
	RequestedTable = nullptr;
	options::Options.Table.V = table.Name;
}

std::string tables::ExePath()
{
	if (!CurrentTable || pb::DatFileName.empty())
		return std::string();
	auto stem = pb::DatFileName.substr(0, pb::DatFileName.find_last_of('.'));
	auto upperStem = stem;
	std::transform(upperStem.begin(), upperStem.end(), upperStem.begin(),
	               [](unsigned char c) { return std::toupper(c); });
	for (const auto& name : {stem, upperStem})
	{
		for (auto ext : {".EXE", ".exe"})
		{
			auto path = pb::BasePath + name + ext;
			auto file = fopenu(path.c_str(), "rb");
			if (file)
			{
				fclose(file);
				return path;
			}
		}
	}
	return std::string();
}

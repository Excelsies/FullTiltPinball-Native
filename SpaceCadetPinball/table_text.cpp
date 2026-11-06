#include "pch.h"
#include "table_text.h"
#include "ft_test_hook.h"

namespace
{
	// ---- recorded EXE builds ----
	struct DataString
	{
		uint32_t Va;
		size_t Length, Pieces, Longest;
	};
	struct KnownExe
	{
		const char* Name;
		uint32_t Size;
		const char* Sha256;
		DataString Quotes, Credits;
	};
	const KnownExe KnownExes[] =
	{
		{
			"DRAGON.EXE", 505344, "b2670835c1353f16f1d9b948d6fe36f8bb2a0c2062eee3e7d55007758b6dbe46",
			{0x43baa0, 182, 8, 39}, {0x43b6e8, 942, 36, 46}
		},
		{
			"PIRATES.EXE", 513536, "4534a20b0723b81259a63ce71b1c02e865292b5c0d439d5063d654c5ad069664",
			{0x43cfe8, 211, 10, 32}, {0x43cc30, 942, 36, 46}
		},
	};
	const size_t MaxPieceBytes = 199; // the original copies a piece into a 200-byte buffer
	const size_t MaxFileBytes = 64u << 20;

	// ---- loaded state ----
	std::map<int, std::string> Strings;
	std::string Quotes, Credits;
	bool QuotesOk = false, CreditsOk = false;

	bool WantedId(int id) { return (id >= 2001 && id <= 2039) || (id >= 2019 && id <= 2025) || (id >= 3001 && id <= 3076); }
	// RT_STRING blocks holding the wanted ids: (2019-1)/16+1 = 127 .. (3076-1)/16+1 = 193.
	const uint32_t FirstWantedBlock = 126, LastWantedBlock = 193;

	// ---- SHA-256 (FIPS 180-4) ----
	std::string Sha256Hex(const std::vector<uint8_t>& msg)
	{
		static const uint32_t K[64] =
		{
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};
		uint32_t h[8] = {
			0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
		};
		std::vector<uint8_t> m(msg);
		uint64_t bits = static_cast<uint64_t>(msg.size()) * 8;
		m.push_back(0x80);
		while (m.size() % 64 != 56)
			m.push_back(0);
		for (int i = 7; i >= 0; i--)
			m.push_back(static_cast<uint8_t>(bits >> (i * 8)));
		auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
		for (size_t off = 0; off < m.size(); off += 64)
		{
			uint32_t w[64];
			for (int i = 0; i < 16; i++)
				w[i] = (uint32_t(m[off + 4 * i]) << 24) | (uint32_t(m[off + 4 * i + 1]) << 16) |
					(uint32_t(m[off + 4 * i + 2]) << 8) | uint32_t(m[off + 4 * i + 3]);
			for (int i = 16; i < 64; i++)
			{
				uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
				uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
			for (int i = 0; i < 64; i++)
			{
				uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
				uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
				hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
			}
			h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
		}
		char out[65];
		for (int i = 0; i < 8; i++)
			snprintf(out + i * 8, 9, "%08x", h[i]);
		return std::string(out);
	}

	// ---- PE32 image view (every read bounds-checked) ----
	struct Section
	{
		char Name[9];
		uint32_t VirtualAddress, RawSize, RawOffset;
	};
	struct Image
	{
		std::vector<uint8_t> Bytes;
		uint32_t ImageBase = 0, ResourceRva = 0, ResourceSize = 0;
		std::vector<Section> Sections;
		std::string Error;

		bool Fail(const char* what)
		{
			if (Error.empty())
				Error = what;
			return false;
		}
		bool Has(size_t off, size_t len) const { return off <= Bytes.size() && len <= Bytes.size() - off; }
		uint16_t U16(size_t off) const { return uint16_t(Bytes[off] | (Bytes[off + 1] << 8)); }
		uint32_t U32(size_t off) const
		{
			return uint32_t(Bytes[off]) | (uint32_t(Bytes[off + 1]) << 8) | (uint32_t(Bytes[off + 2]) << 16) |
				(uint32_t(Bytes[off + 3]) << 24);
		}

		bool Parse()
		{
			if (!Has(0, 0x40) || Bytes[0] != 'M' || Bytes[1] != 'Z')
				return Fail("not a PE file (no MZ header)");
			size_t pe = U32(0x3c);
			if (!Has(pe, 24 + 96 + 16 * 8) || memcmp(&Bytes[pe], "PE\0\0", 4) != 0)
				return Fail("not a PE file (no PE signature or truncated headers)");
			size_t coff = pe + 4, opt = coff + 20;
			unsigned sectionCount = U16(coff + 2);
			size_t optSize = U16(coff + 16);
			if (U16(opt) != 0x10b)
				return Fail("not a PE32 image");
			if (sectionCount == 0 || sectionCount > 96 || !Has(opt, optSize) || optSize < 96 + 16 * 8)
				return Fail("malformed PE headers");
			ImageBase = U32(opt + 28);
			ResourceRva = U32(opt + 96 + 2 * 8);
			ResourceSize = U32(opt + 96 + 2 * 8 + 4);
			size_t sec = opt + optSize;
			if (!Has(sec, size_t(sectionCount) * 40))
				return Fail("truncated section table");
			for (unsigned i = 0; i < sectionCount; i++)
			{
				size_t p = sec + i * 40;
				Section s;
				memcpy(s.Name, &Bytes[p], 8);
				s.Name[8] = 0;
				s.VirtualAddress = U32(p + 12);
				s.RawSize = U32(p + 16);
				s.RawOffset = U32(p + 20);
				if (!Has(s.RawOffset, s.RawSize))
					return Fail("section raw data outside the file");
				Sections.push_back(s);
			}
			return true;
		}

		// File offset of [rva, rva+len) inside one section's raw data, or false.
		bool RvaToOffset(uint32_t rva, size_t len, size_t& off) const
		{
			for (const auto& s : Sections)
			{
				if (rva >= s.VirtualAddress && uint64_t(rva - s.VirtualAddress) + len <= s.RawSize)
				{
					off = s.RawOffset + (rva - s.VirtualAddress);
					return true;
				}
			}
			return false;
		}
	};

	// Appends the UTF-8 form of UTF-16 units; false on a lone surrogate.
	bool AppendUtf8(std::string& out, const uint16_t* u, size_t n)
	{
		for (size_t i = 0; i < n; i++)
		{
			uint32_t c = u[i];
			if (c >= 0xD800 && c <= 0xDBFF)
			{
				if (i + 1 >= n || u[i + 1] < 0xDC00 || u[i + 1] > 0xDFFF)
					return false;
				c = 0x10000 + ((c - 0xD800) << 10) + (u[++i] - 0xDC00);
			}
			else if (c >= 0xDC00 && c <= 0xDFFF)
				return false;
			if (c < 0x80)
				out += char(c);
			else if (c < 0x800)
			{
				out += char(0xC0 | (c >> 6));
				out += char(0x80 | (c & 0x3F));
			}
			else if (c < 0x10000)
			{
				out += char(0xE0 | (c >> 12));
				out += char(0x80 | ((c >> 6) & 0x3F));
				out += char(0x80 | (c & 0x3F));
			}
			else
			{
				out += char(0xF0 | (c >> 18));
				out += char(0x80 | ((c >> 12) & 0x3F));
				out += char(0x80 | ((c >> 6) & 0x3F));
				out += char(0x80 | (c & 0x3F));
			}
		}
		return true;
	}

	// One string-table block (16 counted UTF-16 strings); block b holds ids (b-1)*16 .. (b-1)*16+15.
	bool ReadStringBlock(Image& img, size_t off, size_t size, int block, size_t& stored)
	{
		size_t pos = off, end = off + size;
		for (int i = 0; i < 16; i++)
		{
			if (end - pos < 2)
				return img.Fail("RT_STRING block truncated");
			size_t count = img.U16(pos);
			pos += 2;
			if ((end - pos) / 2 < count)
				return img.Fail("RT_STRING string runs past its block");
			int id = (block - 1) * 16 + i;
			if (count && WantedId(id))
			{
				std::vector<uint16_t> units(count);
				for (size_t k = 0; k < count; k++)
					units[k] = img.U16(pos + 2 * k);
				std::string text;
				if (!AppendUtf8(text, units.data(), count))
					return img.Fail("RT_STRING contains an unpaired surrogate");
				Strings[id] = text;
				stored++;
			}
			pos += count * 2;
		}
		return true;
	}

	// Resource directory entry i of the table at `dir` (offset relative to the resource root).
	struct ResEntry
	{
		uint32_t Name, Offset;
	};
	bool ReadDir(Image& img, size_t rootOff, uint32_t dirOffset, std::vector<ResEntry>& idEntries)
	{
		if (dirOffset > img.ResourceSize || !img.Has(rootOff + dirOffset, 16))
			return img.Fail("resource directory outside the resource section");
		size_t at = rootOff + dirOffset;
		size_t named = img.U16(at + 12), ids = img.U16(at + 14);
		if (!img.Has(at + 16, (named + ids) * 8))
			return img.Fail("resource directory entries outside the file");
		for (size_t i = named; i < named + ids; i++) // id entries follow the named ones
			idEntries.push_back({img.U32(at + 16 + i * 8), img.U32(at + 16 + i * 8 + 4)});
		return true;
	}

	bool ReadStringTable(Image& img, size_t& stored)
	{
		size_t root;
		if (img.ResourceRva == 0 || img.ResourceSize == 0 || !img.RvaToOffset(img.ResourceRva, img.ResourceSize, root))
			return img.Fail("no resource directory");
		std::vector<ResEntry> types;
		if (!ReadDir(img, root, 0, types))
			return false;
		bool found = false;
		for (const auto& t : types)
		{
			if (t.Name != 6) // RT_STRING
				continue;
			found = true;
			if (!(t.Offset & 0x80000000u))
				return img.Fail("RT_STRING entry is not a directory");
			std::vector<ResEntry> blocks;
			if (!ReadDir(img, root, t.Offset & 0x7fffffffu, blocks))
				return false;
			for (const auto& b : blocks)
			{
				// Block b holds ids (b-1)*16..(b-1)*16+15; only blocks that can hold a wanted id are read, so the
				// id arithmetic in ReadStringBlock cannot overflow on a malformed block id.
				if (b.Name < FirstWantedBlock || b.Name > LastWantedBlock)
					continue;
				if (!(b.Offset & 0x80000000u))
					return img.Fail("string block entry is not a directory");
				std::vector<ResEntry> langs;
				if (!ReadDir(img, root, b.Offset & 0x7fffffffu, langs))
					return false;
				if (langs.empty())
					continue;
				uint32_t dataEntry = langs[0].Offset; // first language entry
				if ((dataEntry & 0x80000000u) || dataEntry > img.ResourceSize || !img.Has(root + dataEntry, 16))
					return img.Fail("resource data entry outside the resource section");
				uint32_t rva = img.U32(root + dataEntry), size = img.U32(root + dataEntry + 4);
				size_t off;
				if (!img.RvaToOffset(rva, size, off))
					return img.Fail("string block data outside its section");
				if (!ReadStringBlock(img, off, size, int(b.Name), stored))
					return false;
			}
		}
		return found || img.Fail("no RT_STRING resources");
	}

	// ---- .data strings ----
	const KnownExe* Identify(const std::vector<uint8_t>& bytes, const std::string& sha, std::string& why)
	{
		for (const auto& k : KnownExes)
		{
			if (bytes.size() == k.Size && sha == k.Sha256)
				return &k;
		}
		why = "EXE is not a recorded build (size/SHA-256 mismatch)";
		return nullptr;
	}

	bool ReadDataString(const Image& img, const DataString& rec, std::string& out, std::string& why)
	{
		const Section* data = nullptr;
		for (const auto& s : img.Sections)
			if (!strcmp(s.Name, ".data"))
				data = &s;
		if (!data)
		{
			why = "no .data section";
			return false;
		}
		uint32_t rva = rec.Va - img.ImageBase;
		if (rec.Va < img.ImageBase || rva < data->VirtualAddress ||
			uint64_t(rva - data->VirtualAddress) + rec.Length + 1 > data->RawSize)
		{
			why = "virtual address outside .data raw data";
			return false;
		}
		const uint8_t* p = &img.Bytes[data->RawOffset + (rva - data->VirtualAddress)];
		if (p[rec.Length] != 0)
		{
			why = "no NUL at the recorded length";
			return false;
		}
		size_t pieces = 1, cur = 0, longest = 0;
		for (size_t i = 0; i < rec.Length; i++)
		{
			uint8_t c = p[i];
			if (!((c >= 0x20 && c <= 0x7e) || c == '\r' || c == '\n'))
			{
				why = "non-printable byte";
				return false;
			}
			if (c == '~')
			{
				pieces++;
				cur = 0;
			}
			else if (++cur > longest)
				longest = cur;
			if (cur > MaxPieceBytes)
			{
				why = "piece longer than 199 bytes";
				return false;
			}
		}
		if (pieces != rec.Pieces || longest != rec.Longest)
		{
			why = "piece count / longest piece differ from the recorded values";
			return false;
		}
		out.assign(reinterpret_cast<const char*>(p), rec.Length);
		return true;
	}

	void LoadDataStrings(const Image& img, const std::string& sha, const std::string& name)
	{
		std::string why;
		auto known = Identify(img.Bytes, sha, why);
		if (!known)
		{
			fprintf(stderr, "table_text: %s: .data strings refused: %s\n", name.c_str(), why.c_str());
			return;
		}
		fprintf(stderr, "table_text: %s: size %zu, sha256 matches recorded %s\n", name.c_str(), img.Bytes.size(),
		        known->Name);
		struct Job
		{
			const char* Label;
			const DataString* Rec;
			std::string* Out;
			bool* Ok;
		};
		for (const Job& j : {Job{"quotes", &known->Quotes, &Quotes, &QuotesOk},
		                     Job{"credits", &known->Credits, &Credits, &CreditsOk}})
		{
			why.clear();
			*j.Ok = ReadDataString(img, *j.Rec, *j.Out, why);
			if (*j.Ok)
				fprintf(stderr, "table_text: %s: %s va=0x%x length=%zu pieces=%zu result=ok\n", name.c_str(), j.Label,
				        j.Rec->Va, j.Rec->Length, j.Rec->Pieces);
			else
				fprintf(stderr, "table_text: %s: %s va=0x%x refused: %s\n", name.c_str(), j.Label, j.Rec->Va,
				        why.c_str());
		}
	}

	void Clear()
	{
		Strings.clear();
		Quotes.clear();
		Credits.clear();
		QuotesOk = CreditsOk = false;
	}

	bool ReadFile(const std::string& path, std::vector<uint8_t>& out, std::string& err)
	{
		auto file = fopenu(path.c_str(), "rb");
		if (!file)
		{
			err = "cannot open the file";
			return false;
		}
		uint8_t buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, file)) > 0)
		{
			out.insert(out.end(), buf, buf + n);
			if (out.size() > MaxFileBytes)
			{
				fclose(file);
				err = "file larger than the 64 MB limit";
				return false;
			}
		}
		bool bad = ferror(file) != 0;
		fclose(file);
		if (bad)
			err = "read error";
		return !bad;
	}
}

bool table_text::Load(const std::string& exePath)
{
	Clear();
	auto slash = exePath.find_last_of("/\\");
	std::string name = slash == std::string::npos ? exePath : exePath.substr(slash + 1);
	Image img;
	std::string err;
	if (!ReadFile(exePath, img.Bytes, err))
	{
		fprintf(stderr, "table_text: %s: load failed: %s\n", name.c_str(), err.c_str());
		return false;
	}
	size_t stored = 0;
	if (!img.Parse() || !ReadStringTable(img, stored))
	{
		Clear();
		fprintf(stderr, "table_text: %s: load failed: %s\n", name.c_str(), img.Error.c_str());
		return false;
	}
	fprintf(stderr, "table_text: %s: RT_STRING ok, %zu wanted strings (ids 2001-2039, 3001-3076)\n", name.c_str(),
	        stored);
	LoadDataStrings(img, Sha256Hex(img.Bytes), name);
	return true;
}

const char* table_text::Get(int id)
{
	ft_test_hook::TextFetched(id);
	auto it = Strings.find(id);
	return it == Strings.end() ? nullptr : it->second.c_str();
}

const char* table_text::GetData(DataText which)
{
	switch (which)
	{
	case DataText::Quotes:
		return QuotesOk ? Quotes.c_str() : nullptr;
	case DataText::Credits:
		return CreditsOk ? Credits.c_str() : nullptr;
	}
	return nullptr;
}

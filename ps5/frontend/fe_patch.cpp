// fceuxs5 frontend: ROM hacks -- IPS, UPS and BPS soft-patching (fe_patch.h).
//
// The three formats, as their authors documented them:
//   IPS  "PATCH", then records (3-byte offset, 2-byte size, data; size 0 = run: 2-byte count, 1 byte), "EOF",
//        optionally a 3-byte size to truncate to.
//   UPS  "UPS1", varint source size, varint target size, then hunks (varint skip, bytes XORed until a 0), and the
//        CRC32s of source, target and patch (byu's format, 2008).
//   BPS  "BPS1", varint source size, target size, metadata size + metadata, then actions (SourceRead, TargetRead,
//        SourceCopy, TargetCopy with signed relative offsets), and the three CRC32s (byu's format, 2010).
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fe_patch.h"

#include "OrbisPaths.h"
#include "fe_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <map>
#include <mutex>
#include <zlib.h>

namespace fe
{
namespace
{
std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string ExtOf(const std::string& p)
{
	const size_t dot = p.find_last_of('.');
	const size_t slash = p.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(p.substr(dot));
}

std::string DirOf(const std::string& p)
{
	const size_t slash = p.find_last_of('/');
	return slash == std::string::npos ? "." : p.substr(0, slash);
}

std::string BaseOf(const std::string& p) // file name without folder and extension
{
	const size_t slash = p.find_last_of('/');
	std::string f = slash == std::string::npos ? p : p.substr(slash + 1);
	const size_t dot = f.find_last_of('.');
	return dot == std::string::npos ? f : f.substr(0, dot);
}

bool ReadAll(const std::string& path, std::vector<uint8_t>& out, size_t max_size)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.clear();
	uint8_t buf[65536];
	size_t n;
	bool ok = true;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
	{
		if (out.size() + n > max_size)
		{
			ok = false;
			break;
		}
		out.insert(out.end(), buf, buf + n);
	}
	if (ferror(f))
		ok = false;
	fclose(f);
	return ok;
}

uint32_t Crc(const uint8_t* p, size_t n)
{
	uLong c = crc32(0L, Z_NULL, 0);
	while (n > 0)
	{
		const uInt chunk = uInt(std::min<size_t>(n, 1u << 30));
		c = crc32(c, p, chunk);
		p += chunk;
		n -= chunk;
	}
	return uint32_t(c);
}

uint32_t Le32(const uint8_t* p)
{
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// byu's variable-length number (UPS and BPS)
bool ReadVarint(const std::vector<uint8_t>& d, size_t& pos, size_t end, uint64_t& out)
{
	uint64_t data = 0, shift = 1;
	for (int i = 0; i < 10; i++)
	{
		if (pos >= end)
			return false;
		const uint8_t x = d[pos++];
		data += (x & 0x7f) * shift;
		if (x & 0x80)
		{
			out = data;
			return true;
		}
		shift <<= 7;
		data += shift;
	}
	return false;
}

constexpr size_t kMaxRom = 64u << 20;

bool ApplyIps(const std::vector<uint8_t>& p, std::vector<uint8_t>& rom, std::string* error)
{
	if (p.size() < 8 || memcmp(p.data(), "PATCH", 5) != 0)
	{
		*error = "This is not an IPS patch.";
		return false;
	}
	size_t pos = 5;
	std::vector<uint8_t> out = rom;
	for (;;)
	{
		if (pos + 3 > p.size())
		{
			*error = "The IPS patch is cut short.";
			return false;
		}
		if (memcmp(p.data() + pos, "EOF", 3) == 0)
		{
			pos += 3;
			if (pos + 3 <= p.size()) // Lunar IPS's truncation extension
			{
				const size_t trunc = size_t(p[pos]) << 16 | size_t(p[pos + 1]) << 8 | p[pos + 2];
				if (trunc > 0 && trunc < out.size())
					out.resize(trunc);
			}
			break;
		}
		const size_t off = size_t(p[pos]) << 16 | size_t(p[pos + 1]) << 8 | p[pos + 2];
		pos += 3;
		if (pos + 2 > p.size())
		{
			*error = "The IPS patch is cut short.";
			return false;
		}
		size_t size = size_t(p[pos]) << 8 | p[pos + 1];
		pos += 2;
		if (size == 0)
		{
			if (pos + 3 > p.size())
			{
				*error = "The IPS patch is cut short.";
				return false;
			}
			const size_t run = size_t(p[pos]) << 8 | p[pos + 1];
			const uint8_t value = p[pos + 2];
			pos += 3;
			if (off + run > kMaxRom)
			{
				*error = "The IPS patch writes past 64 MB.";
				return false;
			}
			if (out.size() < off + run)
				out.resize(off + run, 0);
			std::fill(out.begin() + off, out.begin() + off + run, value);
		}
		else
		{
			if (pos + size > p.size())
			{
				*error = "The IPS patch is cut short.";
				return false;
			}
			if (off + size > kMaxRom)
			{
				*error = "The IPS patch writes past 64 MB.";
				return false;
			}
			if (out.size() < off + size)
				out.resize(off + size, 0);
			memcpy(out.data() + off, p.data() + pos, size);
			pos += size;
		}
	}
	rom.swap(out);
	return true;
}

// UPS on one source buffer; false with *error on a malformed patch. *src_ok: the source CRC matched.
bool UpsOn(const std::vector<uint8_t>& p, const std::vector<uint8_t>& src, std::vector<uint8_t>& out, bool* src_ok,
	std::string* error)
{
	if (p.size() < 18 || memcmp(p.data(), "UPS1", 4) != 0)
	{
		*error = "This is not a UPS patch.";
		return false;
	}
	const size_t end = p.size() - 12;
	size_t pos = 4;
	uint64_t in_size = 0, out_size = 0;
	if (!ReadVarint(p, pos, end, in_size) || !ReadVarint(p, pos, end, out_size) || out_size > kMaxRom)
	{
		*error = "The UPS patch is damaged.";
		return false;
	}
	const uint32_t want_src = Le32(p.data() + end);
	*src_ok = Crc(src.data(), src.size()) == want_src;
	// UPS works both ways: a source the size of the patch's target means it is applied in reverse
	uint64_t target = out_size;
	if (src.size() == out_size && src.size() != in_size)
		target = in_size;
	out.assign(size_t(target), 0);
	memcpy(out.data(), src.data(), std::min<size_t>(src.size(), out.size()));
	size_t at = 0;
	while (pos < end)
	{
		uint64_t skip = 0;
		if (!ReadVarint(p, pos, end, skip))
		{
			*error = "The UPS patch is damaged.";
			return false;
		}
		at += size_t(skip);
		while (pos < end)
		{
			const uint8_t x = p[pos++];
			if (at < out.size())
				out[at] ^= x;
			at++;
			if (x == 0)
				break;
		}
	}
	return true;
}

bool BpsOn(const std::vector<uint8_t>& p, const std::vector<uint8_t>& src, std::vector<uint8_t>& out, bool* src_ok,
	std::string* error)
{
	if (p.size() < 19 || memcmp(p.data(), "BPS1", 4) != 0)
	{
		*error = "This is not a BPS patch.";
		return false;
	}
	const size_t end = p.size() - 12;
	size_t pos = 4;
	uint64_t src_size = 0, dst_size = 0, meta = 0;
	if (!ReadVarint(p, pos, end, src_size) || !ReadVarint(p, pos, end, dst_size) || !ReadVarint(p, pos, end, meta) ||
		dst_size > kMaxRom || meta > end - pos)
	{
		*error = "The BPS patch is damaged.";
		return false;
	}
	pos += size_t(meta);
	*src_ok = Crc(src.data(), src.size()) == Le32(p.data() + end);
	out.assign(size_t(dst_size), 0);
	size_t out_pos = 0;
	int64_t src_rel = 0, dst_rel = 0;
	while (pos < end)
	{
		uint64_t data = 0;
		if (!ReadVarint(p, pos, end, data))
		{
			*error = "The BPS patch is damaged.";
			return false;
		}
		const unsigned cmd = unsigned(data & 3);
		const uint64_t len = (data >> 2) + 1;
		if (out_pos + len > out.size())
		{
			*error = "The BPS patch writes past the game's size.";
			return false;
		}
		switch (cmd)
		{
			case 0: // SourceRead
				for (uint64_t i = 0; i < len; i++, out_pos++)
					out[out_pos] = out_pos < src.size() ? src[out_pos] : 0;
				break;
			case 1: // TargetRead
				if (pos + len > end)
				{
					*error = "The BPS patch is cut short.";
					return false;
				}
				memcpy(out.data() + out_pos, p.data() + pos, size_t(len));
				pos += size_t(len);
				out_pos += size_t(len);
				break;
			case 2: // SourceCopy
			case 3: // TargetCopy
			{
				uint64_t raw = 0;
				if (!ReadVarint(p, pos, end, raw))
				{
					*error = "The BPS patch is damaged.";
					return false;
				}
				const int64_t delta = (raw & 1) ? -int64_t(raw >> 1) : int64_t(raw >> 1);
				int64_t& rel = cmd == 2 ? src_rel : dst_rel;
				rel += delta;
				for (uint64_t i = 0; i < len; i++, out_pos++, rel++)
				{
					if (rel < 0)
					{
						*error = "The BPS patch is damaged.";
						return false;
					}
					if (cmd == 2)
						out[out_pos] = size_t(rel) < src.size() ? src[size_t(rel)] : 0;
					else
					{
						if (size_t(rel) >= out_pos)
						{
							*error = "The BPS patch is damaged.";
							return false;
						}
						out[out_pos] = out[size_t(rel)];
					}
				}
				break;
			}
		}
	}
	return true;
}

// UPS / BPS: on the whole file; if its source CRC doesn't match and the file has an iNES header, on the body with
// the header put back.
bool ApplyChecked(PatchFormat fmt, const std::vector<uint8_t>& p, std::vector<uint8_t>& rom, std::string* note,
	std::string* error)
{
	auto run = [&](const std::vector<uint8_t>& src, std::vector<uint8_t>& out, bool* ok) {
		return fmt == PatchFormat::Ups ? UpsOn(p, src, out, ok, error) : BpsOn(p, src, out, ok, error);
	};
	const char* name = fmt == PatchFormat::Ups ? "UPS" : "BPS";
	std::vector<uint8_t> out;
	bool ok = false;
	if (!run(rom, out, &ok))
		return false;
	if (ok)
	{
		rom.swap(out);
		return true;
	}
	const bool headered = rom.size() > 16 && memcmp(rom.data(), "NES\x1a", 4) == 0;
	if (headered)
	{
		std::vector<uint8_t> body(rom.begin() + 16, rom.end()), out2;
		bool ok2 = false;
		std::string err2;
		std::string* keep = error;
		error = &err2;
		const bool ran = run(body, out2, &ok2);
		error = keep;
		if (ran && ok2)
		{
			std::vector<uint8_t> fixed(rom.begin(), rom.begin() + 16);
			// a patch that brings its own header (the target starts with one) keeps it
			if (out2.size() > 16 && memcmp(out2.data(), "NES\x1a", 4) == 0)
				fixed.clear();
			fixed.insert(fixed.end(), out2.begin(), out2.end());
			rom.swap(fixed);
			if (note)
				*note = std::string(name) + ": made for the ROM without its header; applied to the body.";
			return true;
		}
	}
	rom.swap(out);
	if (note)
		*note = std::string(name) + ": the ROM isn't the one the patch was made for; applied anyway.";
	return true;
}

// ---- remembered choices: patches.txt, "rom path<TAB>patch path" ("" = the original game) ---------------------------
std::mutex s_choice_lock;
std::map<std::string, std::string> s_choices;
bool s_choices_loaded = false;

std::string ChoicesPath()
{
	return OrbisRoot() + "/patches.txt";
}

void LoadChoices()
{
	if (s_choices_loaded)
		return;
	s_choices_loaded = true;
	FILE* f = fopen(ChoicesPath().c_str(), "r");
	if (!f)
		return;
	char line[2048];
	while (fgets(line, sizeof line, f))
	{
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		const size_t tab = s.find('\t');
		if (tab == std::string::npos)
			continue;
		s_choices[s.substr(0, tab)] = s.substr(tab + 1);
	}
	fclose(f);
}

void SaveChoices()
{
	const std::string tmp = ChoicesPath() + ".tmp";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f)
		return;
	for (const auto& kv : s_choices)
		fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
	const bool ok = fflush(f) == 0;
	fclose(f);
	if (ok)
		rename(tmp.c_str(), ChoicesPath().c_str());
}

void ListPatchesIn(const std::string& dir, const std::string& prefix_lower, bool any_name, std::vector<std::string>& out)
{
	DIR* d = opendir(dir.c_str());
	if (!d)
		return;
	std::vector<std::string> found;
	while (dirent* e = readdir(d))
	{
		const std::string name = e->d_name;
		if (name.empty() || name[0] == '.')
			continue;
		if (!IsPatchExt(ExtOf(name)))
			continue;
		if (!any_name && Lower(name).compare(0, prefix_lower.size(), prefix_lower) != 0)
			continue;
		found.push_back(dir + "/" + name);
	}
	closedir(d);
	std::sort(found.begin(), found.end());
	for (const std::string& f : found)
		if (std::find(out.begin(), out.end(), f) == out.end())
			out.push_back(f);
}
} // namespace

PatchFormat PatchFormatOf(const std::string& path)
{
	const std::string e = ExtOf(path);
	if (e == ".ips")
		return PatchFormat::Ips;
	if (e == ".ups")
		return PatchFormat::Ups;
	if (e == ".bps")
		return PatchFormat::Bps;
	return PatchFormat::Unknown;
}

bool IsPatchExt(const std::string& e)
{
	return e == ".ips" || e == ".ups" || e == ".bps";
}

bool ApplyPatchData(PatchFormat fmt, const std::vector<uint8_t>& patch, std::vector<uint8_t>& rom, std::string* note,
	std::string* error)
{
	std::string dummy;
	if (!error)
		error = &dummy;
	switch (fmt)
	{
		case PatchFormat::Ips: return ApplyIps(patch, rom, error);
		case PatchFormat::Ups:
		case PatchFormat::Bps: return ApplyChecked(fmt, patch, rom, note, error);
		default: *error = "Not a patch fceuxs5 knows (IPS, UPS or BPS)."; return false;
	}
}

bool ApplyPatch(const std::string& patch_path, std::vector<uint8_t>& rom, std::string* note, std::string* error)
{
	std::vector<uint8_t> patch;
	if (!ReadAll(patch_path, patch, 64u << 20))
	{
		if (error)
			*error = "The patch file can't be read.";
		return false;
	}
	return ApplyPatchData(PatchFormatOf(patch_path), patch, rom, note, error);
}

std::vector<std::string> FindPatches(const std::string& rom_path)
{
	std::vector<std::string> out;
	const std::string dir = DirOf(rom_path);
	const std::string base = BaseOf(rom_path);
	for (const char* e : {".ips", ".bps", ".ups", ".IPS", ".BPS", ".UPS"})
		if (OrbisIsFile(dir + "/" + base + e))
			out.push_back(dir + "/" + base + e);
	const std::string lower = Lower(base);
	ListPatchesIn(dir, lower, false, out);
	const std::string pdir = OrbisDir("patches");
	ListPatchesIn(pdir, lower, false, out);
	ListPatchesIn(pdir + "/" + base, "", true, out);
	return out;
}

std::string PatchForGame(const std::string& rom_path)
{
	{
		std::lock_guard<std::mutex> lock(s_choice_lock);
		LoadChoices();
		auto it = s_choices.find(rom_path);
		if (it != s_choices.end())
		{
			if (it->second.empty() || OrbisIsFile(it->second))
				return it->second;
		}
	}
	if (!Config().auto_patch)
		return "";
	const std::string dir = DirOf(rom_path);
	const std::string base = BaseOf(rom_path);
	for (const char* e : {".bps", ".ups", ".ips", ".BPS", ".UPS", ".IPS"})
		if (OrbisIsFile(dir + "/" + base + e))
			return dir + "/" + base + e;
	const std::string pdir = OrbisDir("patches");
	for (const char* e : {".bps", ".ups", ".ips"})
		if (OrbisIsFile(pdir + "/" + base + e))
			return pdir + "/" + base + e;
	return "";
}

bool GetPatchChoice(const std::string& rom_path, std::string* patch)
{
	std::lock_guard<std::mutex> lock(s_choice_lock);
	LoadChoices();
	auto it = s_choices.find(rom_path);
	if (it == s_choices.end())
		return false;
	*patch = it->second;
	return true;
}

void SetPatchForGame(const std::string& rom_path, const std::string& patch_path)
{
	std::lock_guard<std::mutex> lock(s_choice_lock);
	LoadChoices();
	if (patch_path == "auto")
		s_choices.erase(rom_path);
	else
		s_choices[rom_path] = patch_path;
	SaveChoices();
}

std::string PatchLabel(const std::string& patch_path)
{
	return BaseOf(patch_path);
}
} // namespace fe

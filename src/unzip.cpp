// ============================================================
//  WimSmith - unzip.cpp
//  Zip reading (see unzip.h). The inflater follows RFC 1951 the way
//  zlib's "puff" does it: canonical Huffman codes decoded bit by bit.
// ============================================================
#include "unzip.h"
#include <cstring>

namespace wimsmith
{
    namespace unzip
    {
        static HelperError Bad(const wstring& what) { return HelperError{ L"Zip file: " + what }; }

        // ---------- inflate (RFC 1951) ----------

        struct Bits
        {
            const uint8_t* p;
            size_t n, pos = 0;
            uint32_t buf = 0;
            int count = 0;
            Bits(const uint8_t* data, size_t size) : p(data), n(size) {}
            uint32_t Get(int k)
            {
                while (count < k)
                {
                    if (pos >= n) throw Bad(L"the compressed data ends too early.");
                    buf |= (uint32_t)p[pos++] << count;
                    count += 8;
                }
                uint32_t v = buf & ((1u << k) - 1);
                buf >>= k;
                count -= k;
                return v;
            }
        };

        struct Huffman
        {
            uint16_t counts[16];      // codes of each length
            uint16_t symbols[288];    // the symbols ordered by their code
        };

        static void Build(Huffman& h, const uint8_t* lengths, int n)
        {
            memset(h.counts, 0, sizeof h.counts);
            for (int i = 0; i < n; i++) h.counts[lengths[i]]++;
            h.counts[0] = 0;
            uint16_t offs[16] = {};
            for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h.counts[len]);
            for (int i = 0; i < n; i++)
                if (lengths[i]) h.symbols[offs[lengths[i]]++] = (uint16_t)i;
        }

        static int Decode(Bits& b, const Huffman& h)
        {
            int code = 0, first = 0, index = 0;
            for (int len = 1; len < 16; len++)
            {
                code |= (int)b.Get(1);
                int count = h.counts[len];
                if (code - count < first) return h.symbols[index + (code - first)];
                index += count;
                first = (first + count) << 1;
                code <<= 1;
            }
            throw Bad(L"a Huffman code is not valid.");
        }

        static const uint16_t LenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
        static const uint8_t LenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
        static const uint16_t DistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
        static const uint8_t DistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

        static void Codes(Bits& b, std::string& out, const Huffman& lit, const Huffman& dist)
        {
            while (true)
            {
                int sym = Decode(b, lit);
                if (sym < 256) { out.push_back((char)sym); continue; }
                if (sym == 256) return;
                sym -= 257;
                if (sym >= 29) throw Bad(L"a length code is not valid.");
                size_t len = LenBase[sym] + b.Get(LenExtra[sym]);
                int ds = Decode(b, dist);
                if (ds >= 30) throw Bad(L"a distance code is not valid.");
                size_t d = DistBase[ds] + b.Get(DistExtra[ds]);
                if (d > out.size()) throw Bad(L"a distance points before the start.");
                size_t from = out.size() - d;
                for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
            }
        }

        static std::string Inflate(const uint8_t* data, size_t size, size_t expected)
        {
            std::string out;
            out.reserve(expected);
            Bits b(data, size);
            bool last;
            do
            {
                last = b.Get(1) != 0;
                uint32_t type = b.Get(2);
                if (type == 0)
                {
                    // stored: from the next byte
                    b.buf = 0;
                    b.count = 0;
                    if (b.pos + 4 > b.n) throw Bad(L"the compressed data ends too early.");
                    uint32_t len = b.p[b.pos] | (b.p[b.pos + 1] << 8), nlen = b.p[b.pos + 2] | (b.p[b.pos + 3] << 8);
                    b.pos += 4;
                    if (len != (~nlen & 0xFFFF)) throw Bad(L"a stored block is not valid.");
                    if (b.pos + len > b.n) throw Bad(L"the compressed data ends too early.");
                    out.append((const char*)b.p + b.pos, len);
                    b.pos += len;
                }
                else if (type == 1)
                {
                    static Huffman fixedLit, fixedDist;
                    static bool built;
                    if (!built)
                    {
                        uint8_t l[288];
                        int i = 0;
                        for (; i < 144; i++) l[i] = 8;
                        for (; i < 256; i++) l[i] = 9;
                        for (; i < 280; i++) l[i] = 7;
                        for (; i < 288; i++) l[i] = 8;
                        Build(fixedLit, l, 288);
                        for (i = 0; i < 30; i++) l[i] = 5;
                        Build(fixedDist, l, 30);
                        built = true;
                    }
                    Codes(b, out, fixedLit, fixedDist);
                }
                else if (type == 2)
                {
                    static const uint8_t Order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                    int nlit = (int)b.Get(5) + 257, ndist = (int)b.Get(5) + 1, ncode = (int)b.Get(4) + 4;
                    if (nlit > 286 || ndist > 30) throw Bad(L"a block header is not valid.");
                    uint8_t lengths[320] = {};
                    for (int i = 0; i < ncode; i++) lengths[Order[i]] = (uint8_t)b.Get(3);
                    Huffman lencode;
                    Build(lencode, lengths, 19);
                    memset(lengths, 0, sizeof lengths);
                    int i = 0;
                    while (i < nlit + ndist)
                    {
                        int sym = Decode(b, lencode);
                        if (sym < 16) { lengths[i++] = (uint8_t)sym; continue; }
                        uint8_t value = 0;
                        int repeat;
                        if (sym == 16)
                        {
                            if (i == 0) throw Bad(L"a block header is not valid.");
                            value = lengths[i - 1];
                            repeat = 3 + (int)b.Get(2);
                        }
                        else if (sym == 17) repeat = 3 + (int)b.Get(3);
                        else repeat = 11 + (int)b.Get(7);
                        if (i + repeat > nlit + ndist) throw Bad(L"a block header is not valid.");
                        while (repeat--) lengths[i++] = value;
                    }
                    if (lengths[256] == 0) throw Bad(L"a block has no end code.");
                    Huffman lit, dist;
                    Build(lit, lengths, nlit);
                    Build(dist, lengths + nlit, ndist);
                    Codes(b, out, lit, dist);
                }
                else throw Bad(L"a block type is not valid.");
            } while (!last);
            return out;
        }

        // ---------- CRC-32 ----------

        static uint32_t Crc32(const std::string& data)
        {
            static uint32_t table[256];
            static bool built;
            if (!built)
            {
                for (uint32_t i = 0; i < 256; i++)
                {
                    uint32_t c = i;
                    for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                    table[i] = c;
                }
                built = true;
            }
            uint32_t c = 0xFFFFFFFFu;
            for (unsigned char ch : data) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }

        // ---------- the zip file ----------

        static uint32_t U16(const std::string& s, size_t at)
        {
            if (at + 2 > s.size()) throw Bad(L"a header is cut off.");
            return (uint8_t)s[at] | ((uint8_t)s[at + 1] << 8);
        }

        static uint32_t U32(const std::string& s, size_t at)
        {
            if (at + 4 > s.size()) throw Bad(L"a header is cut off.");
            return (uint32_t)(uint8_t)s[at] | ((uint32_t)(uint8_t)s[at + 1] << 8) | ((uint32_t)(uint8_t)s[at + 2] << 16) | ((uint32_t)(uint8_t)s[at + 3] << 24);
        }

        int ExtractTop(const wstring& zip, const wstring& outDir, const std::function<bool(const wstring& name)>& want)
        {
            std::string z;
            if (!ReadBytes(zip, z)) throw HelperError{ zip + L": " + ErrorMessage(GetLastError()) };
            // the end of central directory record: in the last 64 KB + 22 bytes
            if (z.size() < 22) throw Bad(L"too short.");
            size_t end = std::string::npos;
            size_t lowest = z.size() > 65557 ? z.size() - 65557 : 0;
            for (size_t i = z.size() - 22 + 1; i-- > lowest;)
                if (U32(z, i) == 0x06054b50u) { end = i; break; }
            if (end == std::string::npos) throw Bad(L"no central directory.");
            uint32_t entries = U16(z, end + 10), cdOffset = U32(z, end + 16);
            if (entries == 0xFFFF || cdOffset == 0xFFFFFFFFu) throw Bad(L"ZIP64 is not supported.");
            int written = 0;
            size_t at = cdOffset;
            for (uint32_t e = 0; e < entries; e++)
            {
                if (U32(z, at) != 0x02014b50u) throw Bad(L"the central directory is not valid.");
                uint32_t flags = U16(z, at + 8), method = U16(z, at + 10), crc = U32(z, at + 16);
                uint32_t packed = U32(z, at + 20), size = U32(z, at + 24);
                uint32_t nameLen = U16(z, at + 28), extraLen = U16(z, at + 30), commentLen = U16(z, at + 32);
                uint32_t local = U32(z, at + 42);
                if (at + 46 + nameLen > z.size()) throw Bad(L"the central directory is cut off.");
                std::string raw = z.substr(at + 46, nameLen);
                at += 46 + nameLen + extraLen + commentLen;
                // the top folder only (no '/' in the name), files only
                if (raw.empty() || raw.find('/') != std::string::npos || raw.find('\\') != std::string::npos) continue;
                wstring name = (flags & 0x800) ? FromUtf8(raw) : FromCodePage(raw, 437);
                if (name.find(L':') != wstring::npos || name == L"." || name == L".." || !want(name)) continue;
                if (flags & 1) throw Bad(name + L" is encrypted.");
                if (U32(z, local) != 0x04034b50u) throw Bad(L"the local header of " + name + L" is not valid.");
                size_t data = local + 30 + U16(z, local + 26) + U16(z, local + 28);
                if (data + packed > z.size()) throw Bad(name + L" is cut off.");
                std::string out;
                if (method == 0) out = z.substr(data, packed);
                else if (method == 8) out = Inflate((const uint8_t*)z.data() + data, packed, size);
                else throw Bad(name + L": compression method " + Num((long long)method) + L" is not supported.");
                if (out.size() != size || Crc32(out) != crc) throw Bad(name + L" is damaged (size or CRC-32).");
                wstring target = Combine(outDir, name);
                if (!WriteBytes(target, out)) throw HelperError{ target + L": " + ErrorMessage(GetLastError()) };
                written++;
            }
            return written;
        }
    }
}

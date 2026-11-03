#!/usr/bin/env python3
"""Subset and compact an OFL-licensed TrueType font for stb_truetype.
1. Keep only glyphs reachable from target_codepoints (+ composite components).
2. Remap glyph IDs densely to 0..K-1 (updating cmap, hmtx, loca, maxp, and composite glyph references).
3. Strip TrueType bytecode instructions from simple glyphs (stb_truetype ignores instructions).
"""
import struct

def parse_tables(data: bytes) -> dict[bytes, bytes]:
    num_tables = struct.unpack_from(">H", data, 4)[0]
    tables = {}
    for i in range(num_tables):
        off = 12 + i * 16
        tag = data[off:off+4]
        t_off, t_len = struct.unpack_from(">II", data, off + 8)
        tables[tag] = data[t_off:t_off + t_len]
    return tables

def parse_cmap_format4(sub: bytes) -> dict[int, int]:
    seg_count_x2 = struct.unpack_from(">H", sub, 6)[0]
    seg_count = seg_count_x2 // 2
    end_codes = [struct.unpack_from(">H", sub, 14 + 2 * i)[0] for i in range(seg_count)]
    start_codes = [struct.unpack_from(">H", sub, 16 + seg_count_x2 + 2 * i)[0] for i in range(seg_count)]
    id_deltas = [struct.unpack_from(">h", sub, 16 + 2 * seg_count_x2 + 2 * i)[0] for i in range(seg_count)]
    id_range_off_base = 16 + 3 * seg_count_x2
    id_range_offsets = [struct.unpack_from(">H", sub, id_range_off_base + 2 * i)[0] for i in range(seg_count)]
    mapping = {}
    for i in range(seg_count):
        sc, ec, delta, ro = start_codes[i], end_codes[i], id_deltas[i], id_range_offsets[i]
        if sc == 0xFFFF and ec == 0xFFFF:
            break
        for cp in range(sc, ec + 1):
            if ro == 0:
                gid = (cp + delta) & 0xFFFF
            else:
                idx_off = id_range_off_base + 2 * i + ro + 2 * (cp - sc)
                if idx_off + 2 > len(sub):
                    continue
                gid = struct.unpack_from(">H", sub, idx_off)[0]
                if gid != 0:
                    gid = (gid + delta) & 0xFFFF
            if gid != 0:
                mapping[cp] = gid
    return mapping

def parse_cmap_format12(sub: bytes) -> dict[int, int]:
    num_groups = struct.unpack_from(">I", sub, 12)[0]
    mapping = {}
    for i in range(num_groups):
        sc, ec, sg = struct.unpack_from(">III", sub, 16 + i * 12)
        if ec - sc > 0x20000:
            continue
        for cp in range(sc, min(ec + 1, 0x10000)):
            mapping[cp] = sg + (cp - sc)
    return mapping

def get_cmap_mapping(cmap_data: bytes) -> dict[int, int]:
    num_tables = struct.unpack_from(">H", cmap_data, 2)[0]
    mapping = {}
    for i in range(num_tables):
        pid, eid, sub_off = struct.unpack_from(">HHI", cmap_data, 4 + i * 8)
        if pid in (0, 3):
            fmt = struct.unpack_from(">H", cmap_data, sub_off)[0]
            if fmt == 4:
                elen = struct.unpack_from(">H", cmap_data, sub_off + 2)[0]
                mapping.update(parse_cmap_format4(cmap_data[sub_off:sub_off + elen]))
            elif fmt == 12:
                elen = struct.unpack_from(">I", cmap_data, sub_off + 4)[0]
                mapping.update(parse_cmap_format12(cmap_data[sub_off:sub_off + elen]))
    return mapping

def build_compact_cmap(cp_to_gid: dict[int, int]) -> bytes:
    items = sorted((cp, gid) for cp, gid in cp_to_gid.items() if 0 < cp < 0xFFFF and 0 < gid < 0xFFFF)
    segments = []
    for cp, gid in items:
        if segments and segments[-1][1] + 1 == cp and (segments[-1][2] + 1 == gid):
            segments[-1] = (segments[-1][0], cp, gid, segments[-1][3])
        else:
            segments.append((cp, cp, gid, gid))
    segments.append((0xFFFF, 0xFFFF, 0, 0))
    seg_count = len(segments)
    seg_count_x2 = seg_count * 2
    max_pow2 = 1
    entry_sel = 0
    while max_pow2 * 2 <= seg_count:
        max_pow2 *= 2
        entry_sel += 1
    search_range = max_pow2 * 2
    range_shift = seg_count_x2 - search_range

    end_codes = b"".join(struct.pack(">H", s[1]) for s in segments)
    start_codes = b"".join(struct.pack(">H", s[0]) for s in segments)
    id_deltas = b"".join(
        struct.pack(">h", ((s[3] - s[0]) & 0xFFFF) if s[0] != 0xFFFF else 1)
        if ((s[3] - s[0]) & 0xFFFF) < 0x8000 else struct.pack(">h", ((s[3] - s[0]) & 0xFFFF) - 0x10000)
        for s in segments
    )
    id_range_offsets = b"\x00\x00" * seg_count

    sub_len = 16 + 4 * seg_count_x2
    sub = struct.pack(">HHHHHHH", 4, sub_len, 0, seg_count_x2, search_range, entry_sel, range_shift)
    sub += end_codes + b"\x00\x00" + start_codes + id_deltas + id_range_offsets

    cmap_hdr = struct.pack(">HH", 0, 1) + struct.pack(">HHI", 3, 1, 12)
    return cmap_hdr + sub

def get_composite_components(glyph_bytes: bytes) -> list[int]:
    if len(glyph_bytes) < 10:
        return []
    num_contours = struct.unpack_from(">h", glyph_bytes, 0)[0]
    if num_contours >= 0:
        return []
    off = 10
    comps = []
    ARG_1_AND_2_ARE_WORDS = 0x0001
    WE_HAVE_A_SCALE = 0x0008
    MORE_COMPONENTS = 0x0020
    WE_HAVE_AN_X_AND_Y_SCALE = 0x0040
    WE_HAVE_A_TWO_BY_TWO = 0x0080
    while off + 4 <= len(glyph_bytes):
        flags, comp_gid = struct.unpack_from(">HH", glyph_bytes, off)
        comps.append(comp_gid)
        off += 4
        if flags & ARG_1_AND_2_ARE_WORDS:
            off += 4
        else:
            off += 2
        if flags & WE_HAVE_A_SCALE:
            off += 2
        elif flags & WE_HAVE_AN_X_AND_Y_SCALE:
            off += 4
        elif flags & WE_HAVE_A_TWO_BY_TWO:
            off += 8
        if not (flags & MORE_COMPONENTS):
            break
    return comps

def rewrite_glyph(glyph_bytes: bytes, gid_map: dict[int, int]) -> bytes:
    if len(glyph_bytes) < 10:
        return glyph_bytes
    num_contours = struct.unpack_from(">h", glyph_bytes, 0)[0]
    if num_contours > 0:
        end_pts_end = 10 + 2 * num_contours
        if end_pts_end + 2 > len(glyph_bytes):
            return glyph_bytes
        instr_len = struct.unpack_from(">H", glyph_bytes, end_pts_end)[0]
        if end_pts_end + 2 + instr_len > len(glyph_bytes):
            return glyph_bytes
        return glyph_bytes[:end_pts_end] + b"\x00\x00" + glyph_bytes[end_pts_end + 2 + instr_len:]
    elif num_contours < 0:
        out = bytearray(glyph_bytes)
        off = 10
        ARG_1_AND_2_ARE_WORDS = 0x0001
        WE_HAVE_A_SCALE = 0x0008
        MORE_COMPONENTS = 0x0020
        WE_HAVE_AN_X_AND_Y_SCALE = 0x0040
        WE_HAVE_A_TWO_BY_TWO = 0x0080
        WE_HAVE_INSTRUCTIONS = 0x0100
        has_instr = False
        while off + 4 <= len(out):
            flags, comp_gid = struct.unpack_from(">HH", out, off)
            if flags & WE_HAVE_INSTRUCTIONS:
                has_instr = True
                flags &= ~WE_HAVE_INSTRUCTIONS
                struct.pack_into(">H", out, off, flags)
            struct.pack_into(">H", out, off + 2, gid_map.get(comp_gid, 0))
            off += 4
            if flags & ARG_1_AND_2_ARE_WORDS:
                off += 4
            else:
                off += 2
            if flags & WE_HAVE_A_SCALE:
                off += 2
            elif flags & WE_HAVE_AN_X_AND_Y_SCALE:
                off += 4
            elif flags & WE_HAVE_A_TWO_BY_TWO:
                off += 8
            if not (flags & MORE_COMPONENTS):
                break
        if has_instr:
            return bytes(out[:off])
        return bytes(out)
    return glyph_bytes

def get_hmetric(hmtx: bytes, num_hmetrics: int, gid: int) -> bytes:
    if gid < num_hmetrics:
        return hmtx[gid * 4 : gid * 4 + 4]
    adv = hmtx[(num_hmetrics - 1) * 4 : (num_hmetrics - 1) * 4 + 2]
    lsb_off = num_hmetrics * 4 + (gid - num_hmetrics) * 2
    lsb = hmtx[lsb_off : lsb_off + 2]
    return adv + lsb

def subset_ttf(data: bytes, target_codepoints: set[int]) -> bytes:
    tables = parse_tables(data)
    head = bytearray(tables[b"head"])
    hhea = bytearray(tables[b"hhea"])
    maxp = bytearray(tables[b"maxp"])
    cmap = tables[b"cmap"]
    hmtx = tables[b"hmtx"]
    loca = tables[b"loca"]
    glyf = tables[b"glyf"]

    num_glyphs = struct.unpack_from(">H", maxp, 4)[0]
    num_hmetrics = struct.unpack_from(">H", hhea, 34)[0]
    index_to_loc_format = struct.unpack_from(">h", head, 50)[0]

    if index_to_loc_format == 0:
        offsets = [struct.unpack_from(">H", loca, 2 * i)[0] * 2 for i in range(num_glyphs + 1)]
    else:
        offsets = [struct.unpack_from(">I", loca, 4 * i)[0] for i in range(num_glyphs + 1)]

    cmap_map = get_cmap_mapping(cmap)
    keep_gids = {0}
    for cp in target_codepoints:
        gid = cmap_map.get(cp, 0)
        if 0 < gid < num_glyphs:
            keep_gids.add(gid)

    queue = list(keep_gids)
    while queue:
        gid = queue.pop()
        if gid >= num_glyphs:
            continue
        g_bytes = glyf[offsets[gid]:offsets[gid + 1]]
        for comp in get_composite_components(g_bytes):
            if comp not in keep_gids and 0 <= comp < num_glyphs:
                keep_gids.add(comp)
                queue.append(comp)

    sorted_old_gids = sorted(keep_gids)
    gid_map = {old_gid: new_gid for new_gid, old_gid in enumerate(sorted_old_gids)}
    new_num_glyphs = len(sorted_old_gids)

    filtered_cmap = {}
    for cp in target_codepoints:
        old_gid = cmap_map.get(cp, 0)
        if old_gid in gid_map and gid_map[old_gid] > 0:
            filtered_cmap[cp] = gid_map[old_gid]

    struct.pack_into(">H", maxp, 4, new_num_glyphs)
    struct.pack_into(">H", hhea, 34, new_num_glyphs)

    new_hmtx = bytearray()
    new_glyf = bytearray()
    new_offsets = [0]

    for old_gid in sorted_old_gids:
        new_hmtx.extend(get_hmetric(hmtx, num_hmetrics, old_gid))
        g_bytes = rewrite_glyph(glyf[offsets[old_gid]:offsets[old_gid + 1]], gid_map)
        new_glyf.extend(g_bytes)
        while len(new_glyf) % 4 != 0:
            new_glyf.append(0)
        new_offsets.append(len(new_glyf))

    if new_offsets[-1] // 2 <= 0xFFFF:
        struct.pack_into(">h", head, 50, 0)
        new_loca = b"".join(struct.pack(">H", off // 2) for off in new_offsets)
    else:
        struct.pack_into(">h", head, 50, 1)
        new_loca = b"".join(struct.pack(">I", off) for off in new_offsets)

    out_tables = {
        b"cmap": build_compact_cmap(filtered_cmap),
        b"glyf": bytes(new_glyf),
        b"head": bytes(head),
        b"hhea": bytes(hhea),
        b"hmtx": bytes(new_hmtx),
        b"loca": new_loca,
        b"maxp": bytes(maxp),
    }

    tags = sorted(out_tables.keys())
    num_t = len(tags)
    max_pow2 = 1
    entry_sel = 0
    while max_pow2 * 2 <= num_t:
        max_pow2 *= 2
        entry_sel += 1
    search_range = max_pow2 * 16
    range_shift = num_t * 16 - search_range

    out = bytearray(struct.pack(">IHHHH", 0x00010000, num_t, search_range, entry_sel, range_shift))
    dir_pos = len(out)
    out.extend(b"\x00" * (16 * num_t))

    for idx, tag in enumerate(tags):
        tdata = out_tables[tag]
        t_off = len(out)
        t_len = len(tdata)
        padded = tdata + (b"\x00" * ((4 - (t_len % 4)) % 4))
        csum = sum(struct.unpack(f">{len(padded)//4}I", padded)) & 0xFFFFFFFF
        struct.pack_into(">4sIII", out, dir_pos + idx * 16, tag, csum, t_off, t_len)
        out.extend(padded)

    return bytes(out)

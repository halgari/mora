#include "mora_skyrim_compile/kid_parser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fmt/format.h>
#include <fstream>

namespace mora_skyrim_compile {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

std::string to_lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

// Map KID item-type token (case-insensitive, space-tolerant) to the
// canonical snake_case form matching `form/<type>` predicates. Unknown
// types return empty — caller emits a diag.
std::string normalize_item_type(std::string_view raw) {
    std::string t = to_lower(trim(raw));
    // Strip spaces so "Magic Effect" == "magiceffect".
    t.erase(std::remove_if(t.begin(), t.end(),
                           [](unsigned char c) { return std::isspace(c); }),
            t.end());
    static const std::pair<std::string_view, std::string_view> k_map[] = {
        {"weapon",           "weapon"},
        {"armor",            "armor"},
        {"ammo",             "ammo"},
        {"magiceffect",      "magic_effect"},
        {"potion",           "potion"},
        {"scroll",           "scroll"},
        {"location",         "location"},
        {"ingredient",       "ingredient"},
        {"book",             "book"},
        {"miscitem",         "misc_item"},
        {"key",              "key"},
        {"soulgem",          "soul_gem"},
        {"spell",            "spell"},
        {"activator",        "activator"},
        {"flora",            "flora"},
        {"furniture",        "furniture"},
        {"race",             "race"},
        {"talkingactivator", "talking_activator"},
        {"enchantment",      "enchantment"},
    };
    for (auto& [src, dst] : k_map) {
        if (t == src) return std::string(dst);
    }
    return {};
}

// Parse one identifier token ("MyKeyword" or "0xFFF~Mod.esp").
bool parse_ref(std::string_view tok, KidRef& out, std::string& err) {
    tok = trim(tok);
    if (tok.empty()) { err = "empty reference"; return false; }

    auto tilde = tok.find('~');
    if (tilde == std::string_view::npos) {
        // EditorID (possibly a glob). KID allows a broad char set in
        // EditorIDs, so we don't validate beyond non-emptiness. If the
        // token contains '*' or '?', flag it for the resolver to
        // expand against the EditorID map.
        out.editor_id = std::string(tok);
        out.wildcard  = tok.find_first_of("*?") != std::string_view::npos;
        return true;
    }

    auto hex  = tok.substr(0, tilde);
    auto plug = tok.substr(tilde + 1);
    hex  = trim(hex);
    plug = trim(plug);
    if (hex.empty() || plug.empty()) {
        err = fmt::format("malformed FormID reference: \"{}\"", tok);
        return false;
    }
    // Strip optional "0x" / "0X" prefix.
    if (hex.size() > 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex.remove_prefix(2);
    }
    uint32_t value = 0;
    auto res = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
    if (res.ec != std::errc{} || res.ptr != hex.data() + hex.size()) {
        err = fmt::format("not a hex number: \"{}\"", hex);
        return false;
    }
    out.formid   = value;
    out.mod_file = std::string(plug);
    return true;
}

} // namespace

bool parse_kid_line(std::string_view raw, int line_num,
                    KidLine& out, std::vector<KidDiag>& diags) {
    out = {};
    out.raw = std::string(raw);
    out.source_line = line_num;

    // Strip inline comment starting at the first unquoted ';'. KID .ini
    // files have no quoting rules, so a naive strip is fine.
    auto semi = raw.find(';');
    if (semi != std::string_view::npos) raw = raw.substr(0, semi);
    raw = trim(raw);
    if (raw.empty()) return false;                           // blank / full-line comment
    if (raw.front() == '[' && raw.back() == ']') return false; // [Keywords] section header

    // Optional "Keyword = " (or any "LHS = ") prefix, as seen in some
    // real-world KID files. We ignore the LHS: KID only distributes
    // keywords, so there's no other distribution type to dispatch on.
    auto eq = raw.find('=');
    if (eq != std::string_view::npos) {
        raw = trim(raw.substr(eq + 1));
    }

    auto fields = split(raw, '|');
    if (fields.size() < 2) {
        diags.push_back({line_num, fmt::format(
            "expected at least 2 pipe-separated fields (target|type), got {}: \"{}\"",
            fields.size(), std::string(raw))});
        return false;
    }

    // Field 0: target keyword.
    std::string err;
    if (!parse_ref(fields[0], out.target, err)) {
        diags.push_back({line_num, fmt::format("target keyword: {}", err)});
        return false;
    }

    // Field 1: item type.
    out.item_type = normalize_item_type(fields[1]);
    if (out.item_type.empty()) {
        diags.push_back({line_num, fmt::format(
            "unknown KID item type \"{}\"", std::string(trim(fields[1])))});
        return false;
    }

    // Field 2: filter strings. Optional / may be "NONE" / blank.
    if (fields.size() > 2) {
        auto fstr = trim(fields[2]);
        if (!fstr.empty() && to_lower(fstr) != "none") {
            // Top-level commas separate OR-groups; '+' inside a group
            // ANDs the alternatives. Wildcards ('*') aren't supported
            // yet — we pass them through as literal editor IDs and
            // resolution will likely fail, producing a warning.
            for (auto or_tok : split(fstr, ',')) {
                or_tok = trim(or_tok);
                if (or_tok.empty()) continue;
                KidFilterGroup group;
                for (auto and_tok : split(or_tok, '+')) {
                    and_tok = trim(and_tok);
                    if (and_tok.empty()) continue;
                    KidRef ref;
                    std::string rerr;
                    if (!parse_ref(and_tok, ref, rerr)) {
                        diags.push_back({line_num, fmt::format("filter: {}", rerr)});
                        continue;
                    }
                    group.values.push_back(std::move(ref));
                }
                if (!group.values.empty()) out.filter.push_back(std::move(group));
            }
        }
    }

    // Field 3: traits. Comma or '+' separated, raw tokens kept as-is.
    if (fields.size() > 3) {
        auto tstr = trim(fields[3]);
        if (!tstr.empty() && to_lower(tstr) != "none") {
            // Split on both ',' and '+' — the grammar permits either.
            std::string buf(tstr);
            for (auto& c : buf) if (c == '+') c = ',';
            for (auto tok : split(buf, ',')) {
                tok = trim(tok);
                if (!tok.empty()) out.traits.emplace_back(tok);
            }
        }
    }

    // Field 4: chance. Default 100 if missing / "NONE" / blank.
    if (fields.size() > 4) {
        auto cstr = trim(fields[4]);
        if (!cstr.empty() && to_lower(cstr) != "none") {
            double v = 0.0;
            auto res = std::from_chars(cstr.data(), cstr.data() + cstr.size(), v);
            if (res.ec != std::errc{}) {
                diags.push_back({line_num, fmt::format(
                    "chance: not a number \"{}\"", std::string(cstr))});
            } else {
                out.chance = v;
            }
        }
    }

    return true;
}

KidFile parse_kid_file(const std::filesystem::path& path) {
    KidFile file;
    file.path = path;

    std::ifstream f(path);
    if (!f.is_open()) {
        file.diags.push_back({0, fmt::format("could not open \"{}\"", path.string())});
        return file;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(f, line)) {
        ++line_num;
        // Strip trailing \r for CRLF files.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        KidLine out;
        if (parse_kid_line(line, line_num, out, file.diags)) {
            file.lines.push_back(std::move(out));
        }
    }
    return file;
}

} // namespace mora_skyrim_compile

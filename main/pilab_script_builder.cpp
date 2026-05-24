#include "pilab_script_builder.hpp"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char* TAG = "PILAB_BUILDER";

#ifndef PILAB_SCRIPT_BUILDER_DEBUG
#define PILAB_SCRIPT_BUILDER_DEBUG 1
#endif

std::string PiLabScriptBuilder::trim(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string PiLabScriptBuilder::compactSpaces(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    bool in_ws = false;
    for (char c : s) {
        if (isspace((unsigned char)c)) {
            if (!in_ws) out.push_back(' ');
            in_ws = true;
        } else {
            out.push_back(c);
            in_ws = false;
        }
    }
    return trim(out);
}

bool PiLabScriptBuilder::startsWithWord(const std::string& s, const char* word)
{
    const size_t n = strlen(word);
    if (s.size() < n || strncmp(s.c_str(), word, n) != 0) return false;
    if (s.size() == n) return true;
    const char c = s[n];
    return !(isalnum((unsigned char)c) || c == '_');
}

static bool is_ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static std::string local_trim_for_builder(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::string strip_default_assignment(const std::string& s)
{
    int paren = 0;
    int angle = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(') ++paren;
        else if (c == ')' && paren > 0) --paren;
        else if (c == '<') ++angle;
        else if (c == '>' && angle > 0) --angle;
        else if (c == '=' && paren == 0 && angle == 0) return local_trim_for_builder(s.substr(0, i));
    }
    return local_trim_for_builder(s);
}

void PiLabScriptBuilder::parseDeclaration(PiLabMetadataDecl& out)
{
    const std::string decl = compactSpaces(out.declaration);
    out.kind = "unknown";
    out.typeName.clear();
    out.symbolName.clear();

    if (decl.empty()) return;

    if (startsWithWord(decl, "class")) {
        out.kind = "class";
        size_t p = 5;
        while (p < decl.size() && isspace((unsigned char)decl[p])) ++p;
        size_t e = p;
        while (e < decl.size() && is_ident_char(decl[e])) ++e;
        out.typeName = decl.substr(p, e - p);
        out.symbolName = out.typeName;
        return;
    }

    if (startsWithWord(decl, "interface")) {
        out.kind = "interface";
        size_t p = 9;
        while (p < decl.size() && isspace((unsigned char)decl[p])) ++p;
        size_t e = p;
        while (e < decl.size() && is_ident_char(decl[e])) ++e;
        out.typeName = decl.substr(p, e - p);
        out.symbolName = out.typeName;
        return;
    }

    // Function declaration/definition: find an opening paren, then identify the word before it.
    size_t paren = decl.find('(');
    size_t semi = decl.find(';');
    size_t brace = decl.find('{');
    if (paren != std::string::npos && (semi == std::string::npos || paren < semi) && (brace == std::string::npos || paren < brace)) {
        size_t end = paren;
        while (end > 0 && isspace((unsigned char)decl[end - 1])) --end;
        size_t start = end;
        while (start > 0 && is_ident_char(decl[start - 1])) --start;
        if (start < end) {
            // Constructor-style global declaration, e.g. TON T1(1000), also matches here.
            // Treat it as a global if there is only one token before the symbol name.
            std::string before = trim(decl.substr(0, start));
            size_t last_space = before.find_last_of(" \t");
            if (last_space == std::string::npos && before != "void" && before != "bool" && before != "int" && before != "uint" && before != "float" && before != "double" && before != "string") {
                out.kind = "global";
                out.typeName = before;
                out.symbolName = decl.substr(start, end - start);
            } else {
                out.kind = "function";
                out.symbolName = decl.substr(start, end - start);
                out.typeName = before;
            }
            return;
        }
    }

    // Global variable declaration, e.g. bool Fault; or MyType Obj;
    std::string no_assign = strip_default_assignment(decl);
    if (!no_assign.empty() && no_assign.back() == ';') no_assign.pop_back();
    no_assign = trim(no_assign);
    size_t end = no_assign.size();
    while (end > 0 && isspace((unsigned char)no_assign[end - 1])) --end;
    size_t start = end;
    while (start > 0 && is_ident_char(no_assign[start - 1])) --start;
    if (start < end) {
        out.kind = "global";
        out.symbolName = no_assign.substr(start, end - start);
        out.typeName = trim(no_assign.substr(0, start));
    }
}

std::string PiLabScriptBuilder::findFollowingDeclaration(const char* source, size_t length, size_t start)
{
    size_t i = start;
    while (i < length && isspace((unsigned char)source[i])) ++i;

    const size_t decl_start = i;
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    bool in_char = false;
    bool escape = false;
    int paren = 0;
    int brace = 0;
    int angle = 0;

    for (; i < length; ++i) {
        char c = source[i];
        char n = (i + 1 < length) ? source[i + 1] : '\0';

        if (in_line_comment) {
            if (c == '\n') in_line_comment = false;
            continue;
        }
        if (in_block_comment) {
            if (c == '*' && n == '/') { in_block_comment = false; ++i; }
            continue;
        }
        if (in_string) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (in_char) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '\'') in_char = false;
            continue;
        }

        if (c == '/' && n == '/') { in_line_comment = true; ++i; continue; }
        if (c == '/' && n == '*') { in_block_comment = true; ++i; continue; }
        if (c == '"') { in_string = true; continue; }
        if (c == '\'') { in_char = true; continue; }

        if (c == '(') ++paren;
        else if (c == ')' && paren > 0) --paren;
        else if (c == '<') ++angle;
        else if (c == '>' && angle > 0) --angle;
        else if (c == '{' && paren == 0) {
            ++brace;
            if (brace == 1) return compactSpaces(std::string(source + decl_start, i - decl_start));
        } else if (c == ';' && paren == 0 && angle == 0 && brace == 0) {
            return compactSpaces(std::string(source + decl_start, i - decl_start + 1));
        }
    }

    return compactSpaces(std::string(source + decl_start, length - decl_start));
}

bool PiLabScriptBuilder::preprocess(const char* source, size_t length, std::string& error)
{
    clean_script_.clear();
    final_script_.clear();
    metadata_.clear();
    monitors_.clear();
    params_.clear();
    error.clear();
    if (!source) {
        error = "No script source";
        return false;
    }

    clean_script_.reserve(length + 128);

    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    bool in_char = false;
    bool escape = false;
    int line = 1;
    int col = 1;
    bool at_statement_start = true;

    for (size_t i = 0; i < length; ++i) {
        char c = source[i];
        char n = (i + 1 < length) ? source[i + 1] : '\0';

        auto append_char = [&](char ch) {
            clean_script_.push_back(ch);
            if (ch == '\n') { ++line; col = 1; at_statement_start = true; }
            else { ++col; if (!isspace((unsigned char)ch)) at_statement_start = false; }
        };

        if (in_line_comment) {
            append_char(c);
            if (c == '\n') in_line_comment = false;
            continue;
        }
        if (in_block_comment) {
            append_char(c);
            if (c == '*' && n == '/') { append_char(n); ++i; in_block_comment = false; }
            continue;
        }
        if (in_string) {
            append_char(c);
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (in_char) {
            append_char(c);
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '\'') in_char = false;
            continue;
        }

        if (c == '/' && n == '/') { append_char(c); append_char(n); ++i; in_line_comment = true; continue; }
        if (c == '/' && n == '*') { append_char(c); append_char(n); ++i; in_block_comment = true; continue; }
        if (c == '"') { append_char(c); in_string = true; continue; }
        if (c == '\'') { append_char(c); in_char = true; continue; }

        if (isspace((unsigned char)c)) {
            append_char(c);
            continue;
        }

        if (at_statement_start && c == '[') {
            const int meta_line = line;
            const int meta_col = col;
            size_t j = i + 1;
            bool found = false;
            while (j < length) {
                if (source[j] == ']') { found = true; break; }
                ++j;
            }
            if (!found) {
                error = "Unterminated PiLab/AngelScript metadata block";
                return false;
            }

            std::string meta(source + i + 1, j - i - 1);
            PiLabMetadataDecl decl;
            decl.metadata = trim(meta);
            decl.line = meta_line;
            decl.column = meta_col;
            decl.declaration = findFollowingDeclaration(source, length, j + 1);
            parseDeclaration(decl);
            metadata_.push_back(decl);

            // Strip metadata from the script, but preserve line/column shape as much as practical.
            for (size_t k = i; k <= j; ++k) {
                char ch = source[k];
                clean_script_.push_back(ch == '\n' ? '\n' : ' ');
                if (ch == '\n') { ++line; col = 1; at_statement_start = true; }
                else { ++col; }
            }
            i = j;
            at_statement_start = true;
            continue;
        }

        append_char(c);
        if (c == ';' || c == '}' || c == '{') at_statement_start = true;
    }

    for (const auto& md : metadata_) {
        PiLabParamDecl param;
        std::string param_error;
        if (parseParamMetadata(md, param, param_error)) {
            params_.push_back(param);
        } else if (!param_error.empty()) {
            error = param_error;
            return false;
        }

        PiLabMonitorObject mon;
        std::string mon_error;
        if (parseMonitorMetadata(md, mon, mon_error)) {
            monitors_.push_back(mon);
        } else if (!mon_error.empty()) {
            error = mon_error;
            return false;
        }
    }

    return true;
}

bool PiLabScriptBuilder::buildFinalScript(std::string& error)
{
    return rewriteScanWrapper(clean_script_, monitors_, final_script_, error);
}

void PiLabScriptBuilder::debugPrintMetadata() const
{
#if PILAB_SCRIPT_BUILDER_DEBUG
    ESP_LOGI(TAG, "PiLabScriptBuilder metadata count=%u", (unsigned)metadata_.size());
    for (size_t i = 0; i < metadata_.size(); ++i) {
        const PiLabMetadataDecl& m = metadata_[i];
        ESP_LOGI(TAG,
                 "meta[%u] line=%d col=%d kind=%s type=%s symbol=%s metadata=[%s] declaration=[%s]",
                 (unsigned)i,
                 m.line,
                 m.column,
                 m.kind.c_str(),
                 m.typeName.c_str(),
                 m.symbolName.c_str(),
                 m.metadata.c_str(),
                 m.declaration.c_str());
    }
#endif
}

static bool metadata_has_word(const std::string& metadata, const char* word)
{
    if (!word) return false;
    const size_t n = strlen(word);
    for (size_t p = metadata.find(word); p != std::string::npos; p = metadata.find(word, p + 1)) {
        const bool left_ok = (p == 0) || !(isalnum((unsigned char)metadata[p - 1]) || metadata[p - 1] == '_');
        const size_t e = p + n;
        const bool right_ok = (e >= metadata.size()) || !(isalnum((unsigned char)metadata[e]) || metadata[e] == '_');
        if (left_ok && right_ok) return true;
    }
    return false;
}

static bool extract_quoted_attr(const std::string& metadata, const char* key, std::string& out)
{
    out.clear();
    if (!key) return false;
    std::string pat = std::string(key) + "=\"";
    size_t p = metadata.find(pat);
    if (p == std::string::npos) return false;
    p += pat.size();
    bool escape = false;
    for (size_t i = p; i < metadata.size(); ++i) {
        char c = metadata[i];
        if (escape) { out.push_back(c); escape = false; continue; }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') return true;
        out.push_back(c);
    }
    out.clear();
    return false;
}

static bool is_valid_as_ident(const std::string& s)
{
    if (s.empty()) return false;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || c == '_')) return false;
    return true;
}

static std::string tag_type_for_script_type(const std::string& script_type)
{
    if (script_type == "bool") return "bool";
    if (script_type == "float" || script_type == "double") return "float";
    return "int"; // uint/int/int32/etc. are stored in PiLab's signed int tag type for now.
}

static std::string assignment_cast_for_tag_type(const std::string& tag_type, const std::string& expr)
{
    if (tag_type == "bool") return expr;
    if (tag_type == "float") return std::string("float(") + expr + ")";
    return std::string("int(") + expr + ")";
}

static float parse_float_attr_or_default(const std::string& metadata, const char* key, float fallback)
{
    std::string txt;
    if (!extract_quoted_attr(metadata, key, txt) || local_trim_for_builder(txt).empty()) return fallback;
    return strtof(txt.c_str(), nullptr);
}

bool PiLabScriptBuilder::parseParamMetadata(const PiLabMetadataDecl& decl, PiLabParamDecl& out, std::string& error)
{
    out = PiLabParamDecl{};
    error.clear();
    if (!metadata_has_word(decl.metadata, "PiLabParam")) return false;

    out.metadata = decl.metadata;
    out.line = decl.line;

    if (!extract_quoted_attr(decl.metadata, "name", out.name) || trim(out.name).empty()) {
        error = "PiLabParam is missing name=\"...\"";
        return false;
    }
    out.name = trim(out.name);
    if (!is_valid_as_ident(out.name)) {
        error = "Invalid PiLabParam name: " + out.name;
        return false;
    }

    if (!extract_quoted_attr(decl.metadata, "tag", out.tagName) || trim(out.tagName).empty()) {
        // Fall back to the symbol name when metadata is attached directly to a global declaration.
        out.tagName = decl.symbolName;
    }
    out.tagName = trim(out.tagName);
    if (!is_valid_as_ident(out.tagName)) {
        error = "Invalid PiLabParam tag name: " + out.tagName;
        return false;
    }
    if (out.tagName.size() >= 32) {
        error = "PiLabParam tag name too long: " + out.tagName;
        return false;
    }

    if (!extract_quoted_attr(decl.metadata, "type", out.scriptType) || trim(out.scriptType).empty()) {
        out.scriptType = "int";
    }
    out.scriptType = trim(out.scriptType);
    out.tagType = tag_type_for_script_type(out.scriptType);

    if (!extract_quoted_attr(decl.metadata, "default", out.defaultValue)) {
        if (out.tagType == "bool") out.defaultValue = "false";
        else out.defaultValue = "0";
    }
    out.defaultValue = trim(out.defaultValue);

    extract_quoted_attr(decl.metadata, "units", out.units);
    out.units = trim(out.units);
    out.minValue = parse_float_attr_or_default(decl.metadata, "min", 0.0f);
    out.maxValue = parse_float_attr_or_default(decl.metadata, "max", 100.0f);
    return true;
}

bool PiLabScriptBuilder::parseMonitorMetadata(const PiLabMetadataDecl& decl, PiLabMonitorObject& out, std::string& error)
{
    out = PiLabMonitorObject{};
    error.clear();
    if (!metadata_has_word(decl.metadata, "PiLabMonitor")) return false;
    if (decl.kind != "global") {
        // Class-level PiLabMonitor metadata is useful documentation for now, but
        // only global object instances can create monitor globals.
        return false;
    }

    out.objectName = decl.symbolName;
    out.objectType = decl.typeName;
    out.metadata = decl.metadata;
    out.line = decl.line;

    std::string name_attr;
    if (extract_quoted_attr(decl.metadata, "name", name_attr) && !name_attr.empty()) {
        out.objectName = name_attr;
    }
    std::string type_attr;
    if (extract_quoted_attr(decl.metadata, "type", type_attr) && !type_attr.empty()) {
        out.objectType = type_attr;
    }

    if (!is_valid_as_ident(out.objectName)) {
        error = "Invalid PiLabMonitor object name: " + out.objectName;
        return false;
    }

    std::string fields;
    if (!extract_quoted_attr(decl.metadata, "fields", fields) || trim(fields).empty()) {
        error = "PiLabMonitor for " + out.objectName + " is missing fields=\"...\"";
        return false;
    }

    size_t start = 0;
    while (start < fields.size()) {
        size_t comma = fields.find(',', start);
        std::string part = trim(fields.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!part.empty()) {
            size_t colon = part.find(':');
            if (colon == std::string::npos) {
                error = "Bad PiLabMonitor field entry '" + part + "' for " + out.objectName + "; expected name:type";
                return false;
            }
            PiLabMonitorField f;
            f.name = trim(part.substr(0, colon));
            f.scriptType = trim(part.substr(colon + 1));
            if (!is_valid_as_ident(f.name)) {
                error = "Invalid PiLabMonitor field name: " + f.name;
                return false;
            }
            f.tagType = tag_type_for_script_type(f.scriptType);
            f.tagName = "__obj_" + out.objectName + "_" + f.name;
            if (f.tagName.size() >= 32) {
                error = "Generated monitor tag name too long: " + f.tagName;
                return false;
            }
            f.expression = out.objectName + "." + f.name + "()";
            out.fields.push_back(f);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    if (out.fields.empty()) {
        error = "PiLabMonitor for " + out.objectName + " did not define any fields";
        return false;
    }
    return true;
}

static bool match_word_at(const std::string& s, size_t pos, const char* word)
{
    const size_t n = strlen(word);
    if (pos + n > s.size() || s.compare(pos, n, word) != 0) return false;
    if (pos > 0 && (isalnum((unsigned char)s[pos - 1]) || s[pos - 1] == '_')) return false;
    if (pos + n < s.size() && (isalnum((unsigned char)s[pos + n]) || s[pos + n] == '_')) return false;
    return true;
}

static bool find_matching_brace(const std::string& s, size_t open_brace, size_t& close_brace)
{
    bool in_line_comment = false, in_block_comment = false, in_string = false, in_char = false, escape = false;
    int depth = 0;
    for (size_t i = open_brace; i < s.size(); ++i) {
        char c = s[i];
        char n = (i + 1 < s.size()) ? s[i + 1] : '\0';
        if (in_line_comment) { if (c == '\n') in_line_comment = false; continue; }
        if (in_block_comment) { if (c == '*' && n == '/') { in_block_comment = false; ++i; } continue; }
        if (in_string) { if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '"') in_string = false; continue; }
        if (in_char) { if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '\'') in_char = false; continue; }
        if (c == '/' && n == '/') { in_line_comment = true; ++i; continue; }
        if (c == '/' && n == '*') { in_block_comment = true; ++i; continue; }
        if (c == '"') { in_string = true; continue; }
        if (c == '\'') { in_char = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) { close_brace = i; return true; }
        }
    }
    return false;
}

static bool find_scan_function(const std::string& s, size_t& start, size_t& body_open, size_t& body_close, bool& takes_float)
{
    bool in_line_comment = false, in_block_comment = false, in_string = false, in_char = false, escape = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        char n = (i + 1 < s.size()) ? s[i + 1] : '\0';
        if (in_line_comment) { if (c == '\n') in_line_comment = false; continue; }
        if (in_block_comment) { if (c == '*' && n == '/') { in_block_comment = false; ++i; } continue; }
        if (in_string) { if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '"') in_string = false; continue; }
        if (in_char) { if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '\'') in_char = false; continue; }
        if (c == '/' && n == '/') { in_line_comment = true; ++i; continue; }
        if (c == '/' && n == '*') { in_block_comment = true; ++i; continue; }
        if (c == '"') { in_string = true; continue; }
        if (c == '\'') { in_char = true; continue; }

        if (!match_word_at(s, i, "void")) continue;
        size_t p = i + 4;
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (!(match_word_at(s, p, "scan") || match_word_at(s, p, "Scan"))) continue;
        p += 4;
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (p >= s.size() || s[p] != '(') continue;
        size_t paren_open = p;
        int paren_depth = 0;
        for (; p < s.size(); ++p) {
            if (s[p] == '(') ++paren_depth;
            else if (s[p] == ')') { --paren_depth; if (paren_depth == 0) break; }
        }
        if (p >= s.size()) return false;
        std::string args = local_trim_for_builder(s.substr(paren_open + 1, p - paren_open - 1));
        takes_float = !args.empty();
        ++p;
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
        if (p >= s.size() || s[p] != '{') continue;
        start = i;
        body_open = p;
        return find_matching_brace(s, p, body_close);
    }
    return false;
}

bool PiLabScriptBuilder::rewriteScanWrapper(const std::string& in, const std::vector<PiLabMonitorObject>& monitors, std::string& out, std::string& error)
{
    out.clear(); error.clear();
    if (monitors.empty()) { out = in; return true; }

    size_t fn_start = 0, body_open = 0, body_close = 0;
    bool takes_float = false;
    if (!find_scan_function(in, fn_start, body_open, body_close, takes_float)) {
        error = "PiLabMonitor metadata requires a user scan function: void scan() or void Scan(float)";
        return false;
    }

    out.reserve(in.size() + 512);
    out.append(in.substr(0, fn_start));

    // Preserve the user's scan signature exactly, including the argument name.
    // Earlier versions rewrote Scan(float dt) as __pilab_user_scan(float
    // __pilab_dt_seconds) but copied the original body unchanged; if user code
    // referenced dt, the generated script no longer compiled.
    std::string user_signature = in.substr(fn_start, body_open - fn_start);
    size_t name_pos = user_signature.find("scan");
    if (name_pos == std::string::npos) name_pos = user_signature.find("Scan");
    if (name_pos == std::string::npos) {
        error = "Internal error while rewriting scan function";
        return false;
    }
    user_signature.replace(name_pos, 4, "__pilab_user_scan");
    out.append(user_signature);
    out.append(in.substr(body_open, body_close - body_open + 1));
    out.append(in.substr(body_close + 1));

    out.append("\n\n// PiLab generated monitor wrapper\n");
    if (takes_float) {
        out.append("void Scan(float __pilab_dt_seconds)\n{\n");
        out.append("    __pilab_user_scan(__pilab_dt_seconds);\n");
    } else {
        out.append("void scan()\n{\n");
        out.append("    __pilab_user_scan();\n");
    }
    for (const auto& obj : monitors) {
        out.append("\n    // Monitor object: "); out.append(obj.objectName); out.append("\n");
        for (const auto& f : obj.fields) {
            out.append("    "); out.append(f.tagName); out.append(" = ");
            out.append(assignment_cast_for_tag_type(f.tagType, f.expression));
            out.append(";\n");
        }
    }
    out.append("}\n");
    return true;
}

void PiLabScriptBuilder::debugPrintMonitors() const
{
#if PILAB_SCRIPT_BUILDER_DEBUG
    ESP_LOGI(TAG, "PiLabScriptBuilder monitor object count=%u", (unsigned)monitors_.size());
    for (size_t i = 0; i < monitors_.size(); ++i) {
        const PiLabMonitorObject& obj = monitors_[i];
        ESP_LOGI(TAG, "monitor[%u] object=%s type=%s fields=%u metadata=[%s]",
                 (unsigned)i, obj.objectName.c_str(), obj.objectType.c_str(),
                 (unsigned)obj.fields.size(), obj.metadata.c_str());
        for (size_t f = 0; f < obj.fields.size(); ++f) {
            const PiLabMonitorField& mf = obj.fields[f];
            ESP_LOGI(TAG, "  field[%u] %s scriptType=%s tag=%s tagType=%s expr=%s",
                     (unsigned)f, mf.name.c_str(), mf.scriptType.c_str(),
                     mf.tagName.c_str(), mf.tagType.c_str(), mf.expression.c_str());
        }
    }
#endif
}


void PiLabScriptBuilder::debugPrintParams() const
{
#if PILAB_SCRIPT_BUILDER_DEBUG
    ESP_LOGI(TAG, "PiLabScriptBuilder param count=%u", (unsigned)params_.size());
    for (size_t i = 0; i < params_.size(); ++i) {
        const PiLabParamDecl& p = params_[i];
        ESP_LOGI(TAG,
                 "param[%u] name=%s tag=%s scriptType=%s tagType=%s default=%s min=%.3f max=%.3f units=%s metadata=[%s]",
                 (unsigned)i,
                 p.name.c_str(),
                 p.tagName.c_str(),
                 p.scriptType.c_str(),
                 p.tagType.c_str(),
                 p.defaultValue.c_str(),
                 (double)p.minValue,
                 (double)p.maxValue,
                 p.units.c_str(),
                 p.metadata.c_str());
    }
#endif
}

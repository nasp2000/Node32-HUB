// structures.cpp -- ISQ20/Amada formatting helpers and config helpers extracted from structures.h.
// These functions were formerly static inline in structures.h.
// Compiled once; definitions are shared across all translation units.

#include "structures.h"

// 📦 Fill the "structures" section in the /api/configs configs object
void fillStructuresInConfigs(JsonObject &configs, ConfigManager &configManager) {
    // Root node for all structures config
    JsonObject structures = configs.createNestedObject("structures");
    // ISQ20 sub-objects
    JsonObject amada_mtf1 = structures.createNestedObject("amada_isq20_mtf1");
    amada_mtf1["enabled"] = configManager.getAmadaISQ20MTF1Enabled();
    JsonObject amada_awse = structures.createNestedObject("amada_isq20_awse");
    amada_awse["enabled"] = configManager.getAmadaISQ20AWSEEnabled();
    structures["auto_detect"] = configManager.getStructuresAutoDetectEnabled();
    // These two were missing — the page JS reads them from saved_configs.structures
    // and without them it always fell back to defaults, breaking change detection
    structures["amada_filter_headers"] = configManager.getAmadaFilterHeaders();
    structures["amada_header_every"]   = configManager.getAmadaHeaderEvery();
}

// 🚀 Stub for future runtime application of structures (RS232 parameters, protocols, etc.)
void applyStructuresRuntime(ConfigManager &configManager) {
    // 🧠 Here we will apply RS232/flow parameters per active modes (MTF1/AWSE)
    // 📝 Currently just a placeholder with no behavior change
}

// 🧠 Canonical labels for ISQ20 MTF1 (replace with official manual names when available)
static const char* ISQ20_MTF1_FIELDS[10] = {
    "Program number",                 // 🧠 Nº do programa (Prg)
    "Peak current pulse 1 [kA]",      // 🧠 Is1_kA
    "Peak voltage pulse 1 [V]",       // 🧠 Us1_V
    "Peak power pulse 1 [kW]",        // 🧠 Ps1_kW
    "Welding time pulse 1 [ms]",      // 🧠 Sz1_ms
    "Peak current pulse 2 [kA]",      // 🧠 Is2_kA
    "Peak voltage pulse 2 [V]",       // 🧠 Us2_V
    "Peak power pulse 2 [kW]",        // 🧠 Ps2_kW
    "Welding time pulse 2 [ms]",      // 🧠 Sz2_ms
    "Global welding counter"          // 🧠 Count
};

// Short names for tabular header (compact for CSV/table display)
const char* ISQ20_MTF1_SHORT_NAMES[12] = {
    "Prg",       // Program number
    "Is1_kA",    // Peak current pulse 1
    "Us1_V",     // Peak voltage pulse 1
    "Ps1_kW",    // Peak power pulse 1
    "tw1_ms",    // Welding time pulse 1
    "Is2_kA",    // Peak current pulse 2
    "Us2_V",     // Peak voltage pulse 2
    "Ps2_kW",    // Peak power pulse 2
    "tw2_ms",    // Welding time pulse 2
    "Count",     // Global welding counter
    "G+",        // Upper limit exceeding
    "G-"         // Lower limit exceeding
};

// ♻️ Removed the PT-PT description array (ISQ20_MTF1_DESCR_PT) as obsolete; the table now uses ISQ20_MTF1_FIELDS (EN)

// 🧠 Helper: check whether a token represents a valid number (optional sign, integers or decimals with '.' or ',')
bool isNumericToken(const String &s) {
    // ✅ Do not accept empty
    if (s.length() == 0) return false; // ⚠️ Empty is not a number
    int i = 0; bool hasDigit = false; bool hasDec = false; // 🧮 Estado de parsing
    // 🔖 Optional sign at start
    if (s.charAt(0) == '+' || s.charAt(0) == '-') { if (s.length() == 1) return false; i = 1; }
    for (; i < s.length(); ++i) { // Iterate characters
        char c = s.charAt(i);
        if (c >= '0' && c <= '9') { hasDigit = true; continue; } // ✅ Digit
        if ((c == '.' || c == ',') && !hasDec) { hasDec = true; continue; } // ✅ Single decimal separator
        return false; // ❌ Any other character invalidates
    }
    return hasDigit; // ✅ Has at least one digit
}

// 🧠 Helper: check whether a token is an integer (digits only; no sign for program/axis cases)
bool isIntegerToken(const String &s) {
    if (s.length() == 0) return false; // ⚠️ Vazio
    for (int i = 0; i < s.length(); ++i) { char c = s.charAt(i); if (c < '0' || c > '9') return false; } // ✅ All digits
    return true; // ✅ Inteiro válido
}

// 🧠 Split a string by spaces/tabs into up to maxTokens elements
void splitByWhitespace(const String &line, String tokens[], size_t maxTokens, size_t &count) {
    count = 0; // 🧠 Start token count
    int i = 0; // 🧠 Índice de varrimento
    const int n = line.length(); // 🧠 Comprimento da linha
    while (i < n && count < maxTokens) { // 🧠 Main loop with token limit
        // Skip whitespace (space, tab, CR, LF)
        if (i >= n) break; // 🧠 Nothing more to extract
        int start = i; // 🧠 Token start
        while (i < n) { char c = line[i]; if (c==' ' || c=='\t' || c=='\r' || c=='\n') break; i++; } // 🧠 Advance to separator
        tokens[count++] = line.substring(start, i); // 🧠 Extrai token
    }
}

// 🧠 Helper: append left-aligned text, cutting/padding to the exact size
void appendFixedLeft(String &out, const String &text, size_t width) {
    // 🧠 Compute effective length (in bytes) and cut if needed to avoid breaking width
    size_t len = text.length(); // 🧠 Tamanho original
    if (len > width) { // ⚠️ Text too long: cut
        out += text.substring(0, width); // 🧠 Keep only the part that fits
        return; // ✅ Feito (já ocupa exatamente a largura)
    }
    out += text; // 🧠 Append content
    for (size_t i = len; i < width; ++i) out += ' '; // 🧠 Pad with spaces up to width
}

// 🧠 Helper: acrescenta texto centrado numa largura fixa, cortando se exceder
void appendFixedCenter(String &out, const String &text, size_t width) {
    // 🧠 If the text is wider than the width, cut to keep the table stable
    size_t len = text.length(); // 🧠 Comprimento
    if (len >= width) { // ⚠️ Excede ou igual: cortar
        out += text.substring(0, width); // 🧠 Corta ao tamanho exato
        return; // ✅ Exit (already fixed width)
    }
    size_t totalPad = width - len; // 🧮 Spaces to distribute
    size_t leftPad = totalPad / 2; // 🧮 Half on the left (round down)
    size_t rightPad = totalPad - leftPad; // 🧮 Remainder on the right
    for (size_t i = 0; i < leftPad; ++i) out += ' '; // 🧠 Spaces on the left
    out += text; // 🧠 Text
    for (size_t i = 0; i < rightPad; ++i) out += ' '; // 🧠 Spaces on the right
}

// 🧠 Check whether the line matches the ISQ20 MTF1 pattern (10 strict numeric values, and only '*' after)
bool looksLikeISQ20_MTF1(const String &line) {
    if (line.length() < 10) return false; // ⚠️ Linha demasiado curta
    String toks[32]; size_t c = 0; splitByWhitespace(line, toks, 32, c); // 🧠 Tokenize by spaces
    
    // 🧠 Count main values (numeric or *)
    int valCount = 0;
    for (size_t i = 0; i < c && valCount < 12; ++i) { // ✅ Up to 12 to include G+/G-
        const String &t = toks[i];
        if (isNumericToken(t) || t == "*") { // ✅ Accept number OR asterisk
            valCount++;
        } else {
            return false; // ❌ Token inválido
        }
    }
    
    // ✅ Valid MTF1: 10 values (numeric/asterisks) + optional 2 G+/G- asterisks
    return (valCount >= 10 && valCount <= 12);
}

// 🧠 Build an aligned text table from an MTF1 line
bool buildISQ20_MTF1_Table(const String &rawLine, String &outText) {
    String toks[48]; size_t c = 0; splitByWhitespace(rawLine, toks, 48, c); // 🧠 Divide linha completa em tokens
    String vals[10]; size_t vi = 0; int idx10 = -1; // 📦 Array for 10 useful values and index of the 10th
    for (size_t i = 0; i < c && vi < 10; ++i) { // Gather 10 numeric values, ignoring '*'
        if (toks[i] == "*") continue; // ♻️ Ignora marcadores
        vals[vi++] = toks[i]; // 🧠 Guarda valor
        if (vi == 10) idx10 = (int)i; // 🧭 Mark position of the 10th value
    }
    if (vi != 10 || idx10 < 0) return false; // ⚠️ Unexpected pattern
    bool gplus = false, gminus = false; int seenStars = 0; // 🌟 G+/G- flags
    for (size_t j = idx10 + 1; j < c; ++j) { // 🔎 Examine suffix after the 10th value
        if (toks[j] == "*") { if (seenStars == 0) gplus = true; else if (seenStars == 1) gminus = true; seenStars++; } // 🧠 Order G+, G-
    }
    // 🧮 FIXED widths to keep the table always identical (Position | Value | Description)
    const size_t POS_W = 7; // 🧠 Fixed "Position" column (ASCII)
    const size_t VAL_W = 12; // 🧠 "Value" column now left-aligned (fixed width)
    const size_t DESC_W = 52; // 🧠 "Description" column widened to avoid cutting text
    // 🧠 Descriptions of extra lines 11/12 (G+/G-) cut at DESC_W
    const char* d11 = "Upper limit exceeding max. (Out of Limits +)"; const char* d12 = "Lower limit exceeding min. (Out of Limits -)"; // 🏷️ G+/G- labels with suffix per manual
    // 🧱 Borders based on fixed widths
    String border = "+-"; for (size_t i = 0; i < POS_W; ++i) border += '-'; border += "-+-"; for (size_t i = 0; i < VAL_W; ++i) border += '-'; border += "-+-"; for (size_t i = 0; i < DESC_W; ++i) border += '-'; border += "-+\n"; // 🧱 Horizontal line
    // 🧾 Header and lines with constant size
    outText.reserve(1200); // 🧰 Approximate reserve for 12 lines + borders
    outText += border; // 🧱 Top
    outText += "| "; appendFixedLeft(outText, String(F("Pos")), POS_W); outText += " | "; appendFixedLeft(outText, String(F("Value")), VAL_W); outText += " | "; appendFixedLeft(outText, String(F("Description")), DESC_W); outText += " |\n"; // 🧠 English header
    outText += border; // 🧱 Separator
    for (size_t i = 0; i < 10; ++i) { // 🧾 10 base fields with fixed width
        String pos = String(i+1); const String &val = vals[i]; const char* desc = ISQ20_MTF1_FIELDS[i]; // 📦 Use official English labels
        outText += "| "; appendFixedLeft(outText, pos, POS_W); outText += " | "; appendFixedLeft(outText, val, VAL_W); outText += " | "; appendFixedLeft(outText, String(desc), DESC_W); outText += " |\n"; // ✅ Line with values on the left (EN)
    }
    // 🌟 Lines 11 and 12: G+ and G- with fixed width
    String v11 = gplus ? "*" : "-"; String v12 = gminus ? "*" : "-"; // 🌟 Indicators
    { String pos = String(11); const char* desc = d11; const String &val = v11; outText += "| "; appendFixedLeft(outText, pos, POS_W); outText += " | "; appendFixedLeft(outText, val, VAL_W); outText += " | "; appendFixedLeft(outText, String(desc), DESC_W); outText += " |\n"; } // Line 11 on the left
    { String pos = String(12); const char* desc = d12; const String &val = v12; outText += "| "; appendFixedLeft(outText, pos, POS_W); outText += " | "; appendFixedLeft(outText, val, VAL_W); outText += " | "; appendFixedLeft(outText, String(desc), DESC_W); outText += " |\n"; } // Line 12 on the left
    outText += border; // 🧱 Footer
    return true; // ✅ Success
}
// 📊 NEW: Generate horizontal header (comma-separated) for compact tabular CSV format
// Column widths for MTF1 CSV alignment (maintain visual columns with comma separators)
static const size_t MTF1_COL_WIDTHS[13] = {
    9,  // Timestamp (HH:MM:SS)
    4,  // Prg
    7,  // Is1_kA
    7,  // Us1_V
    7,  // Ps1_kW
    7,  // tw1_ms
    7,  // Is2_kA
    7,  // Us2_V
    7,  // Ps2_kW
    7,  // tw2_ms
    6,  // Count
    2,  // G+
    2   // G-
};

String buildISQ20_MTF1_TabularHeader() {
    String header;
    header.reserve(170); // 🧰 Pre-allocate space (enlarged to include Timestamp)
    appendFixedLeft(header, "Timestamp", MTF1_COL_WIDTHS[0]); // ⏰ Primeira coluna: timestamp
    for (int i = 0; i < 12; ++i) {
        header += ","; // 🔗 Comma separator (CSV)
        appendFixedLeft(header, ISQ20_MTF1_SHORT_NAMES[i], MTF1_COL_WIDTHS[i + 1]);
    }
    return header;
}

// 📊 NOVA: Converte uma linha MTF1 em formato tabular (tab-separated values)
bool buildISQ20_MTF1_TabularRow(const String &rawLine, String &outRow, const String &timestamp) {
    String toks[48]; size_t c = 0; splitByWhitespace(rawLine, toks, 48, c); // 🧠 Divide linha
    String vals[10]; size_t vi = 0; int idx10 = -1; // 📦 10 valores principais
    for (size_t i = 0; i < c && vi < 10; ++i) {
        if (toks[i] == "*") continue; // ♻️ Ignora marcadores
        vals[vi++] = toks[i];
        if (vi == 10) idx10 = (int)i;
    }
    if (vi != 10 || idx10 < 0) return false; // ⚠️ Invalid pattern
    
    // 🌟 Detect G+ and G- (asterisks after the 10th value)
    bool gplus = false, gminus = false; int seenStars = 0;
    for (size_t j = idx10 + 1; j < c; ++j) {
        if (toks[j] == "*") {
            if (seenStars == 0) gplus = true;
            else if (seenStars == 1) gminus = true;
            seenStars++;
        }
    }
    
    // 🧾 Montar linha tabular CSV (Timestamp + 10 valores + G+ + G-)
    outRow.reserve(170);
    // ⏰ ALWAYS add timestamp (empty or filled) to keep CSV alignment
    appendFixedLeft(outRow, timestamp, MTF1_COL_WIDTHS[0]); // ⏰ Timestamp (may be empty)
    outRow += ","; // 🔸 Separator between timestamp and first data column
    appendFixedLeft(outRow, vals[0], MTF1_COL_WIDTHS[1]); // 🔸 Primeiro valor (Prg)
    for (int i = 1; i < 10; ++i) { // 🔸 Continua do 2º valor
        outRow += ",";
        appendFixedLeft(outRow, vals[i], MTF1_COL_WIDTHS[i + 1]);
    }
    outRow += ","; 
    String gplusStr = gplus ? "*" : " ";
    appendFixedLeft(outRow, gplusStr, MTF1_COL_WIDTHS[11]); // G+
    outRow += ","; 
    String gminusStr = gminus ? "*" : " ";
    appendFixedLeft(outRow, gminusStr, MTF1_COL_WIDTHS[12]); // G-
    
    return true; // ✅ Success
}

// 📊 NEW: Convert an MTF1 line to comma-separated CSV format (for recipients/email)
// Formato: Time, 12:34:56, Prg, 123, Is1_kA, 5.23, Us1_V, 3.45, Ps1_kW, 16.45, ...
bool buildISQ20_MTF1_CSVRow(const String &rawLine, String &outRow, const String &timestamp) {
    String toks[48]; size_t c = 0; splitByWhitespace(rawLine, toks, 48, c); // 🧠 Divide linha
    String vals[10]; size_t vi = 0; int idx10 = -1; // 📦 10 valores principais
    for (size_t i = 0; i < c && vi < 10; ++i) {
        if (toks[i] == "*") continue; // ♻️ Ignora marcadores
        vals[vi++] = toks[i];
        if (vi == 10) idx10 = (int)i;
    }
    if (vi != 10 || idx10 < 0) return false; // ⚠️ Invalid pattern
    
    // 🌟 Detect G+ and G- (asterisks after the 10th value)
    bool gplus = false, gminus = false; int seenStars = 0;
    for (size_t j = idx10 + 1; j < c; ++j) {
        if (toks[j] == "*") {
            if (seenStars == 0) gplus = true;
            else if (seenStars == 1) gminus = true;
            seenStars++;
        }
    }
    
    // 🧾 Montar linha CSV (key, value, key, value, ...)
    outRow.reserve(220);
    bool first = true;
    auto addPair = [&](const char* key, const String &val) {
        if (!first) outRow += ", ";
        first = false;
        outRow += key;
        outRow += ", ";
        outRow += val;
    };
    
    // ⏰ Add Timestamp if provided (as first Time, HH:MM:SS pair)
    if (timestamp.length() > 0) {
        addPair("Time", timestamp);
    }
    
    // 10 campos principais + G+/G-
    for (int i = 0; i < 10; ++i) {
        addPair(ISQ20_MTF1_SHORT_NAMES[i], vals[i]);
    }
    addPair("G+", gplus ? "*" : " ");
    addPair("G-", gminus ? "*" : " ");
    
    return true; // ✅ Success
}

// 🧠 Try formatting a line as an MTF1 table
bool tryFormatISQ20_MTF1(const String &inputLine, String &outText) {
    if (!looksLikeISQ20_MTF1(inputLine)) return false; // ⚠️ No match
    return buildISQ20_MTF1_Table(inputLine, outText); // ✅ Format table
}

// 🧠 Placeholder: tentativa de formatar AWSE (estrutura por definir)
// 🧠 Official labels for ISQ20 AWSE (AWS3) — per manual (p. 22)
static const char* ISQ20_AWSE_FIELDS[21] = {
    "Program number",                 // 🧠 Program number (Prg)
    "Axis 1 or axis 2",              // 🧠 Text per manual (AX)
    "Peak current pulse 1 [kA]",     // 🧠 Is1_kA
    "RMS current pulse 1 [kA]",      // 🧠 Ie1_kA
    "Peak voltage pulse 1 [V]",      // 🧠 Us1_V
    "RMS voltage pulse 1 [V]",       // 🧠 Ue1_V
    "Peak power pulse 1 [kW]",       // 🧠 P1_kW
    "Energy pulse 1 [Ws]",           // 🧠 W1_Ws
    "Resistance 1 [µOhm]",           // 🧠 R1_uOhm
    "Welding time pulse 1 [ms]",     // 🧠 tw1_ms
    "Peak current pulse 2 [kA]",     // 🧠 Is2_kA
    "RMS current pulse 2 [kA]",      // 🧠 Ie2_kA
    "Peak voltage pulse 2 [V]",      // 🧠 Us2_V
    "RMS voltage pulse 2 [V]",       // 🧠 Ue2_V
    "Peak power pulse 2 [kW]",       // 🧠 P2_kW
    "Energy pulse 2 [Ws]",           // 🧠 W2_Ws
    "Resistance 2 [µOhm]",           // 🧠 R2_uOhm
    "Welding time pulse 2 [ms]",     // 🧠 tw2_ms
    "Global welding counter",        // 🧠 Count
    "Upper limit exceeding max. (ISO-AWS3)",    // 🧠 G+
    "Lower limit exceeding min. (ISO-AWS3)"     // 🧠 G-
};

// Short names for tabular header AWSE (compact for CSV/table display)
const char* ISQ20_AWSE_SHORT_NAMES[21] = {
    "Prg",       // Program number
    "Axis",      // Axis 1 or 2
    "Is1_kA",    // Peak current pulse 1
    "Ie1_kA",    // RMS current pulse 1
    "Us1_V",     // Peak voltage pulse 1
    "Ue1_V",     // RMS voltage pulse 1
    "P1_kW",     // Peak power pulse 1
    "W1_Ws",     // Energy pulse 1
    "R1_uOhm",   // Resistance 1
    "tw1_ms",    // Welding time pulse 1
    "Is2_kA",    // Peak current pulse 2
    "Ie2_kA",    // RMS current pulse 2
    "Us2_V",     // Peak voltage pulse 2
    "Ue2_V",     // RMS voltage pulse 2
    "P2_kW",     // Peak power pulse 2
    "W2_Ws",     // Energy pulse 2
    "R2_uOhm",   // Resistance 2
    "tw2_ms",    // Welding time pulse 2
    "Count",     // Global welding counter
    "G+",        // Upper limit exceeding
    "G-"         // Lower limit exceeding
};

// 🧠 Check whether the line matches the ISQ20 AWSE pattern (19 valid numbers + up to 2 trailing asterisks)
bool looksLikeISQ20_AWSE(const String &line) {
    if (line.length() < 30) return false; // ⚠️ Line too short to hold 19 fields
    String toks[64]; size_t c = 0; splitByWhitespace(line, toks, 64, c); // 🧠 Tokenize by spaces
    if (c < 19) return false; // ⚠️ Too few tokens
    
    // 🧠 Count main values (numeric or *), capturing the first 2 for validation
    int valCount = 0;
    String firstNum, secondNum; // 📦 Cache of the first 2 values
    for (size_t i = 0; i < c && valCount < 21; ++i) { // ✅ Up to 21 to include G+/G-
        const String &t = toks[i];
        if (isNumericToken(t) || t == "*") { // ✅ Accept number OR asterisk
            valCount++;
            if (valCount == 1 && isNumericToken(t)) firstNum = t;  // 🧠 Program
            if (valCount == 2 && isNumericToken(t)) secondNum = t; // 🧠 Axis
        } else {
            return false; // ❌ Token inválido
        }
    }
    
    // ✅ Valid AWSE: 19 values (numeric/asterisks) + optional 2 G+/G- asterisks
    if (valCount < 19 || valCount > 21) return false;
    
    // 🧪 Validate Program/Axis as short integers (if they were not asterisks)
    if (firstNum.length() > 0 && (!isIntegerToken(firstNum) || firstNum.length() > 3)) return false;
    if (secondNum.length() > 0 && (!isIntegerToken(secondNum) || secondNum.length() > 2)) return false;
    
    return true; // ✅ Valid AWSE
}

// 🧠 Build an AWSE table from a valid line
bool buildISQ20_AWSE_Table(const String &rawLine, String &outText) {
    String toks[48]; size_t c = 0; splitByWhitespace(rawLine, toks, 48, c); // 🧠 Split by spaces
    String vals[19]; size_t vi = 0; int idx19 = -1; // 📦 Expected numeric values
    for (size_t i = 0; i < c && vi < 19; ++i) { // 🔁 Collect 19 values (ignore asterisks)
        if (toks[i] == "*") continue; // ♻️ Ignore
        vals[vi++] = toks[i]; // 🧠 Store value
        if (vi == 19) idx19 = (int)i; // 🧭 Mark position of the 19th
    }
    if (vi != 19 || idx19 < 0) return false; // ⚠️ Unexpected pattern
    bool gplus = false, gminus = false; int seenStars = 0; // 🌟 G+/G- flags
    for (size_t j = idx19 + 1; j < c; ++j) { // 🔎 Examine suffix
        if (toks[j] == "*") { if (seenStars == 0) gplus = true; else if (seenStars == 1) gminus = true; seenStars++; } // 🧠 Order G+, G-
        else return false; // ⚠️ No other tokens expected
    }
    // 🧮 FIXED widths to keep the table stable (Position | Value | Description)
    const size_t POS_W = 7; // 🧠 Fixed "Position" column (ASCII)
    const size_t VAL_W = 12; // 🧠 "Value" column left-aligned
    const size_t DESC_W = 52; // 🧠 Widened "Description" column
    // 🧱 Borders based on fixed widths
    String border = "+-"; for (size_t i = 0; i < POS_W; ++i) border += '-'; border += "-+-"; for (size_t i = 0; i < VAL_W; ++i) border += '-'; border += "-+-"; for (size_t i = 0; i < DESC_W; ++i) border += '-'; border += "-+\n"; // 🧱 Horizontal line
    outText.reserve(1800); // 🧰 Approximate reserve for 21 lines + borders
    outText += border; // 🧱 Top
    outText += "| "; appendFixedLeft(outText, String(F("Pos")), POS_W); outText += " | "; appendFixedLeft(outText, String(F("Value")), VAL_W); outText += " | "; appendFixedLeft(outText, String(F("Description")), DESC_W); outText += " |\n"; // 🧠 English header
    outText += border; // 🧱 Separator
    for (size_t i = 0; i < 19; ++i) { String pos = String(i+1); const String &val = vals[i]; const char* desc = ISQ20_AWSE_FIELDS[i]; outText += "| "; appendFixedLeft(outText, pos, POS_W); outText += " | "; appendFixedLeft(outText, val, VAL_W); outText += " | "; appendFixedLeft(outText, String(desc), DESC_W); outText += " |\n"; } // Lines 1..19 on the left
    outText += border; // 🧱 Middle separator
    { String pos = String(20); String val = gplus ? "*" : "-"; const char* desc = ISQ20_AWSE_FIELDS[19]; outText += "| "; appendFixedLeft(outText, pos, POS_W); outText += " | "; appendFixedLeft(outText, val, VAL_W); outText += " | "; appendFixedLeft(outText, String(desc), DESC_W); outText += " |\n"; } // 🌟 G+
    { String pos = String(21); String val = gminus ? "*" : "-"; const char* desc = ISQ20_AWSE_FIELDS[20]; outText += "| "; appendFixedLeft(outText, pos, POS_W); outText += " | "; appendFixedLeft(outText, val, VAL_W); outText += " | "; appendFixedLeft(outText, String(desc), DESC_W); outText += " |\n"; } // 🌟 G-
    outText += border; // 🧱 Footer
    return true; // ✅ Success
}

// Column widths for AWSE CSV alignment (maintain visual columns with comma separators)
static const size_t AWSE_COL_WIDTHS[22] = {
    9,  // Timestamp (HH:MM:SS)
    4,  // Prg
    5,  // Axis
    7,  // Is1_kA
    7,  // Ie1_kA
    7,  // Us1_V
    7,  // Ue1_V
    7,  // P1_kW
    7,  // W1_Ws
    8,  // R1_uOhm
    7,  // tw1_ms
    7,  // Is2_kA
    7,  // Ie2_kA
    7,  // Us2_V
    7,  // Ue2_V
    7,  // P2_kW
    7,  // W2_Ws
    8,  // R2_uOhm
    7,  // tw2_ms
    6,  // Count
    2,  // G+
    2   // G-
};

// 📊 NEW: Generate horizontal header (tab-separated) for AWSE tabular format
String buildISQ20_AWSE_TabularHeader() {
    String header;
    header.reserve(300); // 🧰 Pre-allocate space (enlarged to include Timestamp)
    appendFixedLeft(header, "Timestamp", AWSE_COL_WIDTHS[0]); // ⏰ First column: timestamp
    for (int i = 0; i < 21; ++i) {
        header += ","; // 🔗 Comma separator (CSV)
        appendFixedLeft(header, ISQ20_AWSE_SHORT_NAMES[i], AWSE_COL_WIDTHS[i + 1]);
    }
    return header;
}

// 📊 NOVA: Converte uma linha AWSE em formato tabular (tab-separated values)
bool buildISQ20_AWSE_TabularRow(const String &rawLine, String &outRow, const String &timestamp) {
    String toks[48]; size_t c = 0; splitByWhitespace(rawLine, toks, 48, c); // 🧠 Divide linha
    String vals[19]; size_t vi = 0; int idx19 = -1; // 📦 19 valores principais
    for (size_t i = 0; i < c && vi < 19; ++i) {
        if (toks[i] == "*") continue; // ♻️ Ignora marcadores
        vals[vi++] = toks[i];
        if (vi == 19) idx19 = (int)i;
    }
    if (vi != 19 || idx19 < 0) return false; // ⚠️ Invalid pattern
    
    // 🌟 Detect G+ and G- (asterisks after the 19th value)
    bool gplus = false, gminus = false; int seenStars = 0;
    for (size_t j = idx19 + 1; j < c; ++j) {
        if (toks[j] == "*") {
            if (seenStars == 0) gplus = true;
            else if (seenStars == 1) gminus = true;
            seenStars++;
        }
    }
    
    // 🧾 Montar linha tabular CSV (Timestamp + 19 valores + G+ + G-)
    outRow.reserve(270);
    // ⏰ ALWAYS add timestamp (empty or filled) to keep CSV alignment
    appendFixedLeft(outRow, timestamp, AWSE_COL_WIDTHS[0]); // ⏰ Timestamp (may be empty)
    outRow += ","; // 🔸 Separator between timestamp and first data column
    appendFixedLeft(outRow, vals[0], AWSE_COL_WIDTHS[1]); // 🔸 First value (Prg)
    for (int i = 1; i < 19; ++i) { // 🔸 Continue from the 2nd value
        outRow += ",";
        appendFixedLeft(outRow, vals[i], AWSE_COL_WIDTHS[i + 1]);
    }
    outRow += ","; 
    String gplusStr = gplus ? "*" : " ";
    appendFixedLeft(outRow, gplusStr, AWSE_COL_WIDTHS[20]); // G+
    outRow += ","; 
    String gminusStr = gminus ? "*" : " ";
    appendFixedLeft(outRow, gminusStr, AWSE_COL_WIDTHS[21]); // G-
    
    return true; // ✅ Success
}

// 📊 NEW: Convert an AWSE line to comma-separated CSV format (for recipients/email)
// Format: Time, 12:34:56, Prg, 123, Axis, 1, Is1_kA, 5.23, Ie1_kA, 4.85, ...
bool buildISQ20_AWSE_CSVRow(const String &rawLine, String &outRow, const String &timestamp) {
    String toks[48]; size_t c = 0; splitByWhitespace(rawLine, toks, 48, c); // 🧠 Divide linha
    String vals[19]; size_t vi = 0; int idx19 = -1; // 📦 19 valores principais
    for (size_t i = 0; i < c && vi < 19; ++i) {
        if (toks[i] == "*") continue; // ♻️ Ignora marcadores
        vals[vi++] = toks[i];
        if (vi == 19) idx19 = (int)i;
    }
    if (vi != 19 || idx19 < 0) return false; // ⚠️ Invalid pattern
    
    // 🌟 Detect G+ and G- (asterisks after the 19th value)
    bool gplus = false, gminus = false; int seenStars = 0;
    for (size_t j = idx19 + 1; j < c; ++j) {
        if (toks[j] == "*") {
            if (seenStars == 0) gplus = true;
            else if (seenStars == 1) gminus = true;
            seenStars++;
        }
    }
    
    // 🧾 Montar linha CSV (key, value, key, value, ...)
    outRow.reserve(350);
    bool first = true;
    auto addPair = [&](const char* key, const String &val) {
        if (!first) outRow += ", ";
        first = false;
        outRow += key;
        outRow += ", ";
        outRow += val;
    };
    
    // ⏰ Adicionar Timestamp se fornecido
    if (timestamp.length() > 0) {
        addPair("Time", timestamp);
    }
    
    // 19 campos principais + G+/G-
    for (int i = 0; i < 19; ++i) {
        addPair(ISQ20_AWSE_SHORT_NAMES[i], vals[i]);
    }
    addPair("G+", gplus ? "*" : " ");
    addPair("G-", gminus ? "*" : " ");
    
    return true; // ✅ Success
}

// 🧠 Try formatting a line as an AWSE table (AWS3)
bool tryFormatISQ20_AWSE(const String &inputLine, String &outText) {
    if (!looksLikeISQ20_AWSE(inputLine)) return false; // ⚠️ No match
    return buildISQ20_AWSE_Table(inputLine, outText); // ✅ Format table
}

// 🧠 Helper: anexa estruturas ISQ20 ao corpo do e-mail, se flags estiverem ativas
void appendISQ20StructuresToEmail(String &emailBody, const String &candidateLine, ConfigManager &configManager) {
    if (configManager.getAmadaISQ20MTF1Enabled()) { // 🧠 Se MTF1 ativo
        String tabela; // 📦 Formatted output
        if (tryFormatISQ20_MTF1(candidateLine, tabela)) { // 🧠 Find and format
            emailBody += F("\n\n"); // 🧠 Space between sections
            emailBody += tabela; // ✅ Append MTF1 table
        }
    }
    if (configManager.getAmadaISQ20AWSEEnabled()) { // 🧠 If AWSE active
        String tabela; // 📦 Formatted AWSE output
        if (tryFormatISQ20_AWSE(candidateLine, tabela)) { // 🧠 When available
            emailBody += F("\n\n"); // 🧠 Space between sections
            emailBody += tabela; // ✅ Append AWSE table
        }
    }
}

// 🧠 NEW: process the full body and insert tables immediately after each recognized ISQ20 line
void processEmailBodyInsertISQ20Tables(String &body, ConfigManager &configManager, int maxTables) {
    // 🧠 Determine strategy: auto-detect ignores toggles; manual mode follows flags
    bool autoDetect = configManager.getStructuresAutoDetectEnabled(); // 🧠 Read global state
    bool allowMTF1 = autoDetect ? true : configManager.getAmadaISQ20MTF1Enabled(); // 🧠 Allow MTF1?
    bool allowAWSE = autoDetect ? true : configManager.getAmadaISQ20AWSEEnabled(); // 🧠 Allow AWSE?
    if (!allowMTF1 && !allowAWSE) return; // ♻️ Nothing to do
    if (maxTables <= 0) return; // 🧱 No budget for tables
    String out; out.reserve(body.length() + 1024); // 🧰 Reserve approximate space
    int n = body.length(); int i = 0; // 🧠 Indexes
    int tablesInserted = 0; // 🧮 Global counter of inserted tables
    while (i < n) { // 🔁 Walk the original body line by line
        int start = i; // 🧭 Line start
        // 🔎 Advance to end of line (keep CR/LF)
        while (i < n && body.charAt(i)!='\n' && body.charAt(i)!='\r') i++; // 🧠 Until separator
        String line = body.substring(start, i); // 📦 Line without CR/LF
        String eol = ""; // 📦 Original terminator
        // ♻️ Aggregate a present CR and/or LF
        if (i < n && (body.charAt(i)=='\r' || body.charAt(i)=='\n')) { char c1 = body.charAt(i++); eol += c1; if (i < n) { char c2 = body.charAt(i); if ((c1=='\r' && c2=='\n') || (c1=='\n' && c2=='\r')) { eol += c2; i++; } } }
        // ✍️ Copy the original line to the output first
        out += line; out += eol;
        // 🧼 Clean candidate for detection (trim)
        String cand = line; cand.trim();
        if (cand.length() > 0 && tablesInserted < maxTables) {
            // 🧪 Smart detection: if at least one mode is active, try both (AWSE priority for being more specific)
            bool inserted = false; String tabela;
            // 🔁 Try AWSE first (19 values) — avoids false positives with MTF1
            if (allowAWSE && tryFormatISQ20_AWSE(cand, tabela)) { out += '\n'; /* 🧱 blank line */ out += F("ISQ20 AWS3 Table\n"); /* 🏷️ title in English */ out += tabela; out += '\n'; out += '\n'; inserted = true; tablesInserted++; } // ✅ AWS3 inserted
            // 🔁 If AWSE did not match, try MTF1 (10 values)
            if (!inserted && tablesInserted < maxTables && allowMTF1 && tryFormatISQ20_MTF1(cand, tabela)) { out += '\n'; /* 🧱 blank line */ out += F("ISQ20 MTF1 Table\n"); /* 🏷️ title in English */ out += tabela; out += '\n'; out += '\n'; inserted = true; tablesInserted++; } // ✅ MTF1 inserted
            // 🔕 If not recognized, continue without inserting
        }
    }
    body = out; // ♻️ Replace the body with the transformed one
}

// 🧠 NEW: compute and build a chunk ONLY with ISQ20 tables fitting the line budget (66 or 90)
// 🧩 Contract:
// - Inputs: raw (raw lines), configManager (auto/manual flags), maxLines (total budget)
// - Outputs: outTables (tables + titles/spacers only), rawCutIndex (index into raw up to consumed), linesUsed (emitted lines)
// - Rule: include only complete tables; if the next table exceeds the budget, neither include it nor consume the raw line
void buildISQ20TablesChunkByLinesBudget(const String &raw, ConfigManager &configManager, int maxLines, String &outTables, int &rawCutIndex, int &linesUsed) {
    // 🧠 Goal: build horizontal tables per type (AWSE and/or MTF1) in the SAME email, each with 1 header + N lines, honoring the total line limit (e.g., 66).
    // 🧠 Policy: sweep the RAW and, whenever an ISQ line segment of the same type appears, emit header (if not yet emitted for that type) and accumulate lines until budget runs out. Different types may coexist in the same email.
    bool autoDetect = configManager.getStructuresAutoDetectEnabled(); // 🧠 Global auto-detect state
    bool allowMTF1 = autoDetect ? true : configManager.getAmadaISQ20MTF1Enabled(); // 🧩 May use MTF1?
    bool allowAWSE = autoDetect ? true : configManager.getAmadaISQ20AWSEEnabled(); // 🧩 May use AWSE?
    outTables = String(); // ♻️ Clear output
    rawCutIndex = 0; // 🧭 Nothing consumed initially
    linesUsed = 0; // 🔢 Lines used in output
    if (maxLines <= 1) return; // ⚠️ Need at least 1 line for header and 1 for data

    // ✅ DESCRIPTIVE header definitions (PT-PT) and column widths for ASCII grid
    // ✅ AWSE (21 columns): Prg, AX, 17 metrics, Counter, G+, G-
    static const char* AWSE_HDR[21] = {
        "Programa","Eixo","Is1 (kA)","Ie1 (kA)","Us1 (V)","Ue1 (V)","P1 (kW)","W1 (Ws)","R1 (µOhm)","tw1 (ms)",
        "Is2 (kA)","Ie2 (kA)","Us2 (V)","Ue2 (V)","P2 (kW)","W2 (Ws)","R2 (µOhm)","tw2 (ms)","Contador","G+","G-"
    };
    static const uint8_t AWSE_W[21] = {
        7,3,9,9,8,8,8,8,10,9, 9,9,8,8,8,8,10,9,9,2,2
    };
    // ✅ MTF1 (12 columns): Program, pulse1/pulse2 metrics, Counter, G+, G-
    static const char* MTF1_HDR[12] = {
        "Programa","Is1 (kA)","Us1 (V)","Ps1 (kW)","Sz1 (ms)","Is2 (kA)","Us2 (V)","Ps2 (kW)","Sz2 (ms)","Contador","G+","G-"
    };
    static const uint8_t MTF1_W[12] = {
        7,9,8,9,9,9,8,9,9,9,2,2
    };

    // Local helpers for light parsing (avoid repeated allocations)
    auto parseMTF1 = [](const String &line, String outVals[12]) -> bool {
        // ✅ Extract 10 values and G+/G- flags
        String toks[48]; size_t c = 0; splitByWhitespace(line, toks, 48, c);
        String vals[10]; size_t vi = 0; int idx10 = -1;
        for (size_t i = 0; i < c && vi < 10; ++i) { if (toks[i] == "*") continue; vals[vi++] = toks[i]; if (vi == 10) idx10 = (int)i; }
        if (vi != 10 || idx10 < 0) return false; // ⚠️ Falhou
        bool gplus=false, gminus=false; int stars=0; for (size_t j=idx10+1;j<c;++j){ if (toks[j]=="*") { if (stars==0) gplus=true; else if (stars==1) gminus=true; stars++; } else return false; }
        // 🧾 Fill output (12 columns): 10 values + G+ + G-
        for (int k=0;k<10;++k) outVals[k]=vals[k]; outVals[10]= gplus?"*":"-"; outVals[11]= gminus?"*":"-";
        return true;
    };
    auto parseAWSE = [](const String &line, String outVals[21]) -> bool {
        // ✅ Extrai 19 valores (inclui Prg e AX) + flags G+/G-
        String toks[64]; size_t c = 0; splitByWhitespace(line, toks, 64, c);
        String vals[19]; size_t vi = 0; int idx19 = -1; for (size_t i=0;i<c && vi<19;++i){ if (toks[i]=="*") continue; vals[vi++]=toks[i]; if (vi==19) idx19=(int)i; }
        if (vi!=19 || idx19<0) return false; bool gplus=false,gminus=false; int stars=0; for (size_t j=idx19+1;j<c;++j){ if (toks[j]=="*") { if (stars==0) gplus=true; else if (stars==1) gminus=true; stars++; } else return false; }
        // 🧾 21 colunas: 19 valores + G+ + G-
        for (int k=0;k<19;++k) outVals[k]=vals[k]; outVals[19]= gplus?"*":"-"; outVals[20]= gminus?"*":"-";
        return true;
    };

    // 🔁 Single pass (streaming): emit multiple tables (AWSE and/or MTF1) within the total budget
    int n = raw.length(); int i = 0; // 🧭 Sweep indexes
    bool awseHeader = false, mtf1Header = false; // 🏷️ Headers already emitted
    bool awseOpen = false, mtf1Open = false;     // 🔓 Table open (to close with footer)
    int budget = maxLines; // 🧮 Total line budget for this email
    int cutIndexMax = 0; // 🧭 Largest raw index consumed by ISQ lines
    outTables.reserve(2048); // 🧰 Modest initial reserve
    linesUsed = 0; // 🔢 Global counter

    // ✅ Local helpers for ASCII grid (borders and header)
    auto appendBorderAWSE = [&](){ outTables += '+'; for (int c=0;c<21;++c){ for (int k=0;k<AWSE_W[c]+2;++k) outTables += '-'; outTables += '+'; } outTables += '\n'; };
    auto appendBorderMTF1 = [&](){ outTables += '+'; for (int c=0;c<12;++c){ for (int k=0;k<MTF1_W[c]+2;++k) outTables += '-'; outTables += '+'; } outTables += '\n'; };
    auto appendHeaderAWSE = [&](){ appendBorderAWSE(); outTables += '|'; for (int c=0;c<21;++c){ outTables += ' '; appendFixedLeft(outTables, String(AWSE_HDR[c]), AWSE_W[c]); outTables += ' '; outTables += '|'; } outTables += '\n'; appendBorderAWSE(); };
    auto appendHeaderMTF1 = [&](){ appendBorderMTF1(); outTables += '|'; for (int c=0;c<12;++c){ outTables += ' '; appendFixedLeft(outTables, String(MTF1_HDR[c]), MTF1_W[c]); outTables += ' '; outTables += '|'; } outTables += '\n'; appendBorderMTF1(); };

    // ✅ Emit header if needed, reserving budget space for at least 1 data line and the footer
    auto ensureHeader = [&](int type){
        if (type == 1 && !awseHeader) { // ✅ AWSE
            if (budget < 4) return; // ⚠️ 3 (top+header+sep) + 1 data line
            appendHeaderAWSE(); awseHeader = true; awseOpen = true; linesUsed += 3; budget -= 3;
        } else if (type == 2 && !mtf1Header) { // ✅ MTF1
            if (budget < 4) return; // ⚠️ Same
            appendHeaderMTF1(); mtf1Header = true; mtf1Open = true; linesUsed += 3; budget -= 3;
        }
    };
    bool awseHasRows = false, mtf1HasRows = false; // 🧮 Record whether any line was emitted

    while (i < n && budget > 0) {
        int start = i; while (i < n && raw.charAt(i)!='\n' && raw.charAt(i)!='\r') i++; int lineEndNoEol = i; // 🧭 Delimit line
        if (i < n && (raw.charAt(i)=='\r' || raw.charAt(i)=='\n')) { char c1=raw.charAt(i++); if (i<n){ char c2=raw.charAt(i); if ((c1=='\r' && c2=='\n') || (c1=='\n' && c2=='\r')) { i++; } } }
        String line = raw.substring(start, lineEndNoEol); String cand = line; cand.trim();

        // ✅ Specific filters for ISQ20 table assembly (do not affect raw mode)
        //    - Remove "FU" token at line start (if present)
        //    - Discard emitter header lines (e.g. "Prg Is1_kA ... Sz2_ms Count  G+G- +-")
        if (cand.length() > 0) {
            // 🔎 ISQ20 header (emitter) — discard and CONSUME from raw
            if (cand.startsWith("Prg") && cand.indexOf(F("Is1_kA")) >= 0 && cand.indexOf(F("Sz2_ms")) >= 0 && cand.indexOf(F("G+G-")) >= 0) {
                cutIndexMax = i; // ✅ Consume this line in raw
                continue;       // ♻️ Emit nothing to the table
            }
            // 🔎 Leading FU token — remove; if the line is just "FU", discard and CONSUME
            if (cand.startsWith("FU")) {
                if (cand.length() == 2) {
                    cutIndexMax = i; // ✅ Consume the empty "FU" line
                    continue;        // ♻️ Emit nothing
                }
                char after = cand.length() > 2 ? cand.charAt(2) : '\0';
                if (after == ' ' || after == '\t') {
                    cand = cand.substring(3); // remove "FU" + one separator
                    cand.trim();
                }
            }
        }

        bool matched = false;
        if (cand.length() > 0) {
            // 🧪 Try AWSE
            if (allowAWSE && budget > 1 && looksLikeISQ20_AWSE(cand)) { // ⚠️ Needs 1 line (footer at close)
                String vals[21]; if (parseAWSE(cand, vals)) {
                    // 🧮 Ensure 1 free line for data (header is emitted on demand)
                    ensureHeader(1); if (budget <= 1 && !awseHeader) { i = start; break; }
                    // ✅ Data line with grid
                    outTables += '|';
                    for (int c=0;c<21;++c){ outTables += ' '; appendFixedLeft(outTables, vals[c], AWSE_W[c]); outTables += ' '; outTables += '|'; }
                    outTables += '\n';
                    linesUsed++; budget--; awseHasRows = true; matched = true; cutIndexMax = i; // ✅ Consumed
                }
            }
            // 🧪 Try MTF1 (if not already consumed as AWSE)
            if (!matched && allowMTF1 && budget > 1 && looksLikeISQ20_MTF1(cand)) { // ⚠️ Needs 1 line
                String vals[12]; if (parseMTF1(cand, vals)) {
                    ensureHeader(2); if (budget <= 1 && !mtf1Header) { i = start; break; }
                    // ✅ Data line with grid
                    outTables += '|';
                    for (int c=0;c<12;++c){ outTables += ' '; appendFixedLeft(outTables, vals[c], MTF1_W[c]); outTables += ' '; outTables += '|'; }
                    outTables += '\n';
                    linesUsed++; budget--; mtf1HasRows = true; matched = true; cutIndexMax = i; // ✅ Consumed
                }
            }
        }

        // 🧭 If not a recognized ISQ line, we neither include it in the body (tables) nor consume it (it stays for the next email)
        if (!matched) {
            // Stop at the first non-ISQ line after we started emitting a table,
            // to avoid skipping textual content. If we have not emitted headers/lines yet, keep sweeping.
            if (awseHeader || mtf1Header) { i = start; break; }
        }
    }

    // ✅ Close open grids with the footer
    if (awseOpen && budget > 0) { appendBorderAWSE(); linesUsed++; budget--; awseOpen = false; }
    if (mtf1Open && budget > 0) { appendBorderMTF1(); linesUsed++; budget--; mtf1Open = false; }

    // 🧾 Final result
    if (!awseHeader && !mtf1Header) { outTables = String(); linesUsed = 0; rawCutIndex = 0; return; } // ⚠️ Nothing to emit
    rawCutIndex = cutIndexMax; // 🧭 Consume only what was processed ISQ20
}

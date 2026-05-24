#pragma once

#include <stddef.h>
#include <string>
#include <vector>

struct PiLabMetadataDecl {
    std::string metadata;      // Text inside [ ... ], e.g. PiLabMonitor fields="Q:bool,ET:uint"
    std::string declaration;   // Best-effort declaration immediately following the metadata
    std::string kind;          // class, function, global, unknown
    std::string typeName;      // Best-effort type/class/function return type
    std::string symbolName;    // Best-effort declared symbol name
    int line = 0;              // 1-based source line where metadata started
    int column = 0;            // 1-based source column where metadata started
};

struct PiLabMonitorField {
    std::string name;          // Q, ET, PT
    std::string scriptType;    // bool, int, uint, float
    std::string tagName;       // __obj_T1_Q
    std::string tagType;       // bool, int, float - PiLab tag type
    std::string expression;    // T1.Q(), T1.ET(), etc.
};

struct PiLabMonitorObject {
    std::string objectName;    // T1
    std::string objectType;    // TON
    std::string metadata;      // Original metadata text
    int line = 0;
    std::vector<PiLabMonitorField> fields;
};

struct PiLabParamDecl {
    std::string name;          // PT, PV, SpeedSetpoint, etc.
    std::string tagName;       // T1_PT, C1_PV, etc.
    std::string scriptType;    // bool, int, uint, float
    std::string tagType;       // bool, int, float - PiLab tag type
    std::string defaultValue;  // Metadata default literal as text
    std::string units;         // Optional display units
    float minValue = 0.0f;
    float maxValue = 100.0f;
    std::string metadata;      // Original metadata text
    int line = 0;
};

class PiLabScriptBuilder {
public:
    bool preprocess(const char* source, size_t length, std::string& error);

    const std::string& cleanScript() const { return clean_script_; }
    const std::string& finalScript() const { return final_script_.empty() ? clean_script_ : final_script_; }
    const std::vector<PiLabMetadataDecl>& metadata() const { return metadata_; }
    const std::vector<PiLabMonitorObject>& monitors() const { return monitors_; }
    const std::vector<PiLabParamDecl>& params() const { return params_; }

    // Builds the final script. If PiLabMonitor objects exist, this renames the
    // user's scan() to __pilab_user_scan() and emits a new scan() wrapper that
    // calls the user scan and mirrors object fields into monitor globals.
    bool buildFinalScript(std::string& error);

    void debugPrintMetadata() const;
    void debugPrintMonitors() const;
    void debugPrintParams() const;

private:
    std::string clean_script_;
    std::string final_script_;
    std::vector<PiLabMetadataDecl> metadata_;
    std::vector<PiLabMonitorObject> monitors_;
    std::vector<PiLabParamDecl> params_;

    static std::string trim(const std::string& s);
    static std::string compactSpaces(const std::string& s);
    static bool startsWithWord(const std::string& s, const char* word);
    static void parseDeclaration(PiLabMetadataDecl& out);
    static std::string findFollowingDeclaration(const char* source, size_t length, size_t start);

    static bool parseMonitorMetadata(const PiLabMetadataDecl& decl, PiLabMonitorObject& out, std::string& error);
    static bool parseParamMetadata(const PiLabMetadataDecl& decl, PiLabParamDecl& out, std::string& error);
    static bool rewriteScanWrapper(const std::string& in, const std::vector<PiLabMonitorObject>& monitors, std::string& out, std::string& error);
};

#include "pilab_script_builder.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace {
int g_failures = 0;

void fail(const char* file, int line, const std::string& message) {
    ++g_failures;
    std::cerr << file << ":" << line << ": FAIL: " << message << "\n";
}

#define CHECK_TRUE(expr) do { if (!(expr)) fail(__FILE__, __LINE__, std::string("Expected true: ") + #expr); } while (0)
#define CHECK_FALSE(expr) do { if ((expr)) fail(__FILE__, __LINE__, std::string("Expected false: ") + #expr); } while (0)
#define CHECK_EQ(a,b) do { auto _a=(a); auto _b=(b); if (!(_a == _b)) { std::ostringstream _os; _os << "Expected " #a " == " #b ", got [" << _a << "] and [" << _b << "]"; fail(__FILE__, __LINE__, _os.str()); } } while (0)
#define CHECK_CONTAINS(haystack, needle) do { const std::string _h=(haystack); const std::string _n=(needle); if (_h.find(_n) == std::string::npos) { fail(__FILE__, __LINE__, std::string("Expected output to contain: ") + _n); } } while (0)
#define CHECK_NOT_CONTAINS(haystack, needle) do { const std::string _h=(haystack); const std::string _n=(needle); if (_h.find(_n) != std::string::npos) { fail(__FILE__, __LINE__, std::string("Expected output not to contain: ") + _n); } } while (0)

bool preprocess(PiLabScriptBuilder& b, const std::string& src, std::string& err) {
    return b.preprocess(src.c_str(), src.size(), err);
}

void test_param_and_monitor_metadata_are_parsed_and_stripped() {
    const std::string src = R"AS(
class TON {
  uint preset_ms;
  TON(uint p) { preset_ms = p; }
  void SetPreset(uint p) { preset_ms = p; }
  void update(bool input, uint scan_ms) {}
  bool Q() const { return false; }
  uint ET() const { return 0; }
  uint PT() const { return preset_ms; }
};

[PiLabParam name="PT" tag="T1_PT" type="int" default="1000" min="0" max="600000" units="ms"]
[PiLabMonitor name="T1" type="TON" fields="Q:bool,ET:int,PT:int"]
TON T1(1000);

void scan()
{
  T1.SetPreset(uint(T1_PT));
  T1.update(HMI_I0, 5);
}
)AS";

    PiLabScriptBuilder b;
    std::string err;
    CHECK_TRUE(preprocess(b, src, err));
    CHECK_EQ(b.params().size(), size_t(1));
    CHECK_EQ(b.monitors().size(), size_t(1));

    const PiLabParamDecl& p = b.params()[0];
    CHECK_EQ(p.name, std::string("PT"));
    CHECK_EQ(p.tagName, std::string("T1_PT"));
    CHECK_EQ(p.scriptType, std::string("int"));
    CHECK_EQ(p.tagType, std::string("int"));
    CHECK_EQ(p.defaultValue, std::string("1000"));
    CHECK_EQ(p.units, std::string("ms"));
    CHECK_TRUE(std::fabs(p.minValue - 0.0f) < 0.001f);
    CHECK_TRUE(std::fabs(p.maxValue - 600000.0f) < 0.001f);

    const PiLabMonitorObject& m = b.monitors()[0];
    CHECK_EQ(m.objectName, std::string("T1"));
    CHECK_EQ(m.objectType, std::string("TON"));
    CHECK_EQ(m.fields.size(), size_t(3));
    CHECK_EQ(m.fields[0].tagName, std::string("__obj_T1_Q"));
    CHECK_EQ(m.fields[0].expression, std::string("T1.Q()"));
    CHECK_EQ(m.fields[1].tagName, std::string("__obj_T1_ET"));
    CHECK_EQ(m.fields[1].expression, std::string("T1.ET()"));
    CHECK_EQ(m.fields[2].tagName, std::string("__obj_T1_PT"));
    CHECK_EQ(m.fields[2].expression, std::string("T1.PT()"));

    CHECK_NOT_CONTAINS(b.cleanScript(), "PiLabParam");
    CHECK_NOT_CONTAINS(b.cleanScript(), "PiLabMonitor");

    CHECK_TRUE(b.buildFinalScript(err));
    const std::string out = b.finalScript();
    CHECK_CONTAINS(out, "void __pilab_user_scan()");
    CHECK_CONTAINS(out, "void scan()\n{");
    CHECK_CONTAINS(out, "__pilab_user_scan();");
    CHECK_CONTAINS(out, "__obj_T1_Q = T1.Q();");
    CHECK_CONTAINS(out, "__obj_T1_ET = int(T1.ET());");
    CHECK_CONTAINS(out, "__obj_T1_PT = int(T1.PT());");
}

void test_scan_float_argument_name_is_preserved() {
    const std::string src = R"AS(
class TON { bool Q() const { return false; } uint ET() const { return 0; } uint PT() const { return 0; } };
[PiLabMonitor name="T1" type="TON" fields="Q:bool,ET:int,PT:int"]
TON T1;
void Scan(float dt)
{
  LastDt = dt;
}
)AS";

    PiLabScriptBuilder b;
    std::string err;
    CHECK_TRUE(preprocess(b, src, err));
    CHECK_TRUE(b.buildFinalScript(err));
    const std::string out = b.finalScript();
    CHECK_CONTAINS(out, "void __pilab_user_scan(float dt)");
    CHECK_CONTAINS(out, "LastDt = dt;");
    CHECK_CONTAINS(out, "void Scan(float __pilab_dt_seconds)");
    CHECK_CONTAINS(out, "__pilab_user_scan(__pilab_dt_seconds);");
}

void test_class_level_monitor_metadata_is_documentation_only() {
    const std::string src = R"AS(
[PiLabMonitor fields="Q:bool,ET:int,PT:int"]
class TON { bool Q() const { return false; } uint ET() const { return 0; } uint PT() const { return 0; } };
void scan() {}
)AS";

    PiLabScriptBuilder b;
    std::string err;
    CHECK_TRUE(preprocess(b, src, err));
    CHECK_EQ(b.metadata().size(), size_t(1));
    CHECK_EQ(b.monitors().size(), size_t(0));
    CHECK_TRUE(b.buildFinalScript(err));
    CHECK_NOT_CONTAINS(b.finalScript(), "__obj_");
}

void test_invalid_metadata_is_rejected() {
    {
        PiLabScriptBuilder b;
        std::string err;
        const std::string src = R"AS(
[PiLabParam name="PT" tag="Bad Tag" type="int" default="1"]
int Dummy;
void scan() {}
)AS";
        CHECK_FALSE(preprocess(b, src, err));
        CHECK_CONTAINS(err, "Invalid PiLabParam tag name");
    }

    {
        PiLabScriptBuilder b;
        std::string err;
        const std::string src = R"AS(
[PiLabMonitor name="T1" type="TON" fields="Q"]
TON T1;
void scan() {}
)AS";
        CHECK_FALSE(preprocess(b, src, err));
        CHECK_CONTAINS(err, "expected name:type");
    }
}

void test_monitor_metadata_requires_scan_function() {
    const std::string src = R"AS(
class TON { bool Q() const { return false; } };
[PiLabMonitor name="T1" type="TON" fields="Q:bool"]
TON T1;
)AS";
    PiLabScriptBuilder b;
    std::string err;
    CHECK_TRUE(preprocess(b, src, err));
    CHECK_FALSE(b.buildFinalScript(err));
    CHECK_CONTAINS(err, "requires a user scan function");
}

void test_param_defaults_and_type_mapping() {
    const std::string src = R"AS(
[PiLabParam name="Enable" tag="Axis_Enable" type="bool"]
bool Axis_Enable;
[PiLabParam name="Speed" tag="Axis_Speed" type="float" default="12.5" min="0" max="100"]
float Axis_Speed;
void scan() {}
)AS";

    PiLabScriptBuilder b;
    std::string err;
    CHECK_TRUE(preprocess(b, src, err));
    CHECK_EQ(b.params().size(), size_t(2));
    CHECK_EQ(b.params()[0].tagType, std::string("bool"));
    CHECK_EQ(b.params()[0].defaultValue, std::string("false"));
    CHECK_EQ(b.params()[1].tagType, std::string("float"));
    CHECK_EQ(b.params()[1].defaultValue, std::string("12.5"));
}

} // namespace

int main() {
    test_param_and_monitor_metadata_are_parsed_and_stripped();
    test_scan_float_argument_name_is_preserved();
    test_class_level_monitor_metadata_is_documentation_only();
    test_invalid_metadata_is_rejected();
    test_monitor_metadata_requires_scan_function();
    test_param_defaults_and_type_mapping();

    if (g_failures) {
        std::cerr << "\nPiLab firmware metadata host tests FAILED: " << g_failures << " failure(s).\n";
        return 1;
    }
    std::cout << "PiLab firmware metadata host tests passed.\n";
    return 0;
}

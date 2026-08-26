// Configuration and JSON round-trip tests.
#include "../src/config/json.h"
#include "../src/config/schema.h"
#include "../src/config/manager.h"
#include "test_util.h"
#include <string>

using namespace stgr;
using namespace stgr::config;

void test_json_parse()
{
    auto v = json::parse(R"({"a":1,"b":[1,2,3],"c":"hello","d":true,"e":null})");
    CHECK(v["a"].as_number() == 1.0, "json number");
    CHECK(v["b"].size() == 3, "json array size");
    CHECK(v["c"].as_string() == "hello", "json string");
    CHECK(v["d"].as_bool() == true, "json bool");
    CHECK(v["e"].is_null(), "json null");
}

void test_json_serialize_roundtrip()
{
    const std::string input = R"({"a":1.5,"b":[1,2],"c":{"d":"test"}})";
    auto v = json::parse(input);
    std::string out = v.serialize(false);
    auto v2 = json::parse(out);
    CHECK(v2["a"].as_number() == 1.5, "rt number");
    CHECK(v2["b"].size() == 2, "rt array");
    CHECK(v2["c"]["d"].as_string() == "test", "rt object");
}

void test_stage_params_roundtrip()
{
    dsp::StageParams p;
    p.type = dsp::StageType::Compressor;
    p.enabled = true;
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 80.0f;

    auto j = stage_params_to_json(p);
    auto p2 = stage_params_from_json(j);
    CHECK(p2.type == dsp::StageType::Compressor, "stage type");
    CHECK_CLOSE(p2.thresholdDb, -20.0f, 0.01f, "threshold");
    CHECK_CLOSE(p2.ratio, 4.0f, 0.01f, "ratio");
}

void test_device_config_roundtrip()
{
    DeviceConfig cfg;
    cfg.endpointId = "{test-id}";
    cfg.endpointName = "Test Mic";
    cfg.enabled = true;

    dsp::StageParams hp;
    hp.type = dsp::StageType::HighPass;
    hp.enabled = true;
    hp.freq = 80.0f;
    hp.q = 0.707f;
    cfg.chain.push_back(hp);

    dsp::StageParams lim;
    lim.type = dsp::StageType::Limiter;
    lim.enabled = true;
    lim.ceilingDb = -1.0f;
    cfg.chain.push_back(lim);

    auto j = device_config_to_json(cfg);
    auto cfg2 = device_config_from_json(j);
    CHECK(cfg2.endpointId == cfg.endpointId, "dev id");
    CHECK(cfg2.enabled == cfg.enabled, "dev enabled");
    CHECK(cfg2.chain.size() == 2, "chain size");
    CHECK(cfg2.chain[0].type == dsp::StageType::HighPass, "hp type");
    CHECK_CLOSE(cfg2.chain[0].freq, 80.0f, 0.01f, "hp freq");
    CHECK(cfg2.chain[1].type == dsp::StageType::Limiter, "limiter type");
}

void test_presets()
{
    auto presets = default_presets();
    CHECK(presets.size() == 6, "default presets count");
    bool voice = false, streaming = false;
    for (const auto& p : presets) {
        if (p.name == "Voice") voice = true;
        if (p.name == "Streaming") streaming = true;
    }
    CHECK(voice, "voice preset");
    CHECK(streaming, "streaming preset");

    // Round-trip.
    auto j = preset_to_json(presets[0]);
    auto p2 = preset_from_json(j);
    CHECK(p2.name == presets[0].name, "preset rt name");
    CHECK(p2.chain.size() == presets[0].chain.size(), "preset rt chain");
}

int main()
{
    printf("=== Config tests ===\n");
    test_json_parse();
    test_json_serialize_roundtrip();
    test_stage_params_roundtrip();
    test_device_config_roundtrip();
    test_presets();
    return stgr::test::test_summary();
}
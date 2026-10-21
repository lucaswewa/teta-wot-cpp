// What teta-wot makes of Rust types' JSON Schemas (tests/td/schema_corpus.jsonl,
// written by conformance/reference-rs's `schema-corpus`), against the C++ port.

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "teta/wot/td/convert.hpp"

namespace {

using teta::wot::json::Value;
namespace td = teta::wot::td;

struct Entry {
    std::string type;
    Value json_schema;
    std::string data_schema;
};

std::vector<Entry> corpus() {
    std::ifstream file(TETA_WOT_SCHEMA_CORPUS, std::ios::binary);
    if (!file) {
        throw std::runtime_error("can't open " TETA_WOT_SCHEMA_CORPUS);
    }
    std::vector<Entry> entries;
    std::string line;
    while (std::getline(file, line)) {
        const auto value = Value::parse(line);
        entries.push_back({value["type"].get<std::string>(), value["json_schema"],
                           value["data_schema"].get<std::string>()});
    }
    return entries;
}

// Converting schemars' schema gives teta-wot's DataSchema, byte for byte.
// (`DataSchema::for_type` drops the root title schemars generates from the
// type's name; every title in the corpus is such a name.)
TEST(SchemaCorpus, ConvertingSchemarsOutput) {
    const auto entries = corpus();
    ASSERT_GT(entries.size(), 40u);
    for (const auto& entry : entries) {
        Value schema = entry.json_schema;
        schema.erase("title");
        const auto converted = td::from_json_schema(schema);
        EXPECT_EQ(teta::wot::json::dump(td::to_json(converted)), entry.data_schema) << entry.type;
    }
}

// Reading teta-wot's DataSchema and writing it back changes nothing.
TEST(SchemaCorpus, DataSchemaRoundTrip) {
    for (const auto& entry : corpus()) {
        const auto schema = td::data_schema_from_json(Value::parse(entry.data_schema));
        EXPECT_EQ(teta::wot::json::dump(td::to_json(schema)), entry.data_schema) << entry.type;
    }
}

}  // namespace

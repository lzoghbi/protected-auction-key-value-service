#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "emscripten/bind.h"
#include "nlohmann/json.hpp"

namespace {

constexpr std::array<const char*, 3> kPreferredLocations = {"California", "Arizona", "Florida"};

struct BatchMetrics {
  std::unordered_map<std::string, int> users_per_location;

  void incrementUsersInLocation(const std::string& location) {
    ++users_per_location[location];
  }

  void log_metrics() const {
    for (const auto& [loc, count] : users_per_location) {
      std::cout << "Location: " << loc << " - Total users: " << count << std::endl;
    }
  }
};

// Calls getValues for a list of keys and processes the response.
absl::StatusOr<emscripten::val> getKvPairs(const emscripten::val& get_values_cb,
                                           const emscripten::val& keys,
                                           BatchMetrics& metrics) {
  // Lookup values for the input keys
  const std::string str_values = get_values_cb(keys).as<std::string>();
  const nlohmann::json json_values = nlohmann::json::parse(str_values, nullptr, false, true);
  if (json_values.is_discarded() || !json_values.contains("kvPairs")) {
    return absl::InvalidArgumentError("No kvPairs returned");
  }

  emscripten::val kv_pairs = emscripten::val::object();
  for (auto& [k, v] : json_values["kvPairs"].items()) {
    if (v.contains("value")) {   
      emscripten::val value = emscripten::val::object();
      const std::string val = v["value"].get<std::string>();
      const nlohmann::json nested_value = nlohmann::json::parse(val, nullptr, false, true);
      if (!nested_value.is_object() || !nested_value.contains("location")) continue;
      
      // Filter requests based on user location
      const std::string user_location = nested_value["location"].get<std::string>();
      auto it = find(kPreferredLocations.begin(), kPreferredLocations.end(), user_location);
      if (it != kPreferredLocations.end()) {
        metrics.incrementUsersInLocation(user_location);
        value.set("value", val);
        kv_pairs.set(k, std::move(value));
      }
    }    
  }

  return kv_pairs;
}

emscripten::val getKeyGroupOutputs(const emscripten::val& get_values_cb,
                                   const emscripten::val& udf_arguments,
                                   BatchMetrics& metrics) {
  emscripten::val key_group_outputs = emscripten::val::array();
  // Convert a JS array to a std::vector so we can iterate through it.
  const std::vector<emscripten::val> key_groups =
      emscripten::vecFromJSArray<emscripten::val>(udf_arguments);

  for (auto key_group : key_groups) {
    emscripten::val key_group_output = emscripten::val::object();
    key_group_output.set("tags", key_group["tags"]);

    const emscripten::val data =
        key_group.hasOwnProperty("tags") ? key_group["data"] : key_group;
    absl::StatusOr<emscripten::val> kv_pairs = getKvPairs(get_values_cb, data, metrics);
    if (kv_pairs.ok()) {
      key_group_output.set("keyValues", *kv_pairs);
      key_group_outputs.call<void>("push", key_group_output);
    }
  }

  return key_group_outputs;
}

}

emscripten::val handleRequestCc(const emscripten::val& get_values_cb,
                                const emscripten::val& udf_arguments) {
  emscripten::val result = emscripten::val::object();
  BatchMetrics metrics;

  result.set("keyGroupOutputs",
             getKeyGroupOutputs(get_values_cb, std::move(udf_arguments), metrics));
  result.set("udfOutputApiVersion", emscripten::val(1));

  metrics.log_metrics();
  return result;
}

EMSCRIPTEN_BINDINGS(HandleRequestExample) {
  emscripten::function("handleRequestCc", &handleRequestCc);
}
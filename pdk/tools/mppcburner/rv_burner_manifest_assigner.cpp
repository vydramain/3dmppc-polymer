#include "rv_burner_manifest_assigner.hpp"

#include "rv_burner_manifest_failer.hpp"
#include "rv_burner_schema.hpp"
#include "rv_burner_text.hpp"

bool rv_pdktools::rv_burner_manifest_assigner::wrong_type(
	const std::string &key,
	const rv_burner_mvalue &v,
	rv_burner_value_kind want)
{
	return rv_burner_manifest_failer::fail(v.line,
		"key '" +
			key +
			"' in [" +
			section_ +
			"] takes " +
			std::string(rv_burner_kind_name(want)) +
			", not " +
			std::string(rv_burner_kind_name(v.kind)),
		error_);
}

bool rv_pdktools::rv_burner_manifest_assigner::assign(
	const std::string &key,
	const rv_burner_mvalue &value,
	int key_line)
{
	const section_spec *spec = rv_burner_sections_get(section_);
	if (spec == nullptr) {
		return rv_burner_manifest_failer::fail(key_line, "internal error: unknown current section", error_);
	}

	bool known = false;
	for (std::size_t i = 0; i < spec->key_count; ++i) {
		if (spec->keys[i] == key) {
			known = true;
		}
	}
	if (!known) {
		return rv_burner_manifest_failer::fail(key_line,
			"unknown key '" +
				key +
				"' in [" +
				section_ +
				"]" +
				rv_pdktools::suggest(key, spec->keys, spec->key_count),
			error_);
	}
	if (!seen_keys_.insert(section_ + "." + key).second) {
		return rv_burner_manifest_failer::fail(key_line,
			"key '" +
				key +
				"' in [" +
				section_ +
				"] is set twice",
			error_);
	}

	auto handler = handlers_.find(section_ + "_" + key);

	if (handlers_.end() == handler) {
		return rv_burner_manifest_failer::fail(key_line,
			"internal error: no handler for key '" +
				key +
				"' in [" +
				section_ +
				"]",
			error_);
	}

	return handler->second(value, *this);
}

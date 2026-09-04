#include "rv_manifest_text.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "rv_manifest_schema.hpp"

namespace rv_pdklib
{

// --- "did you mean" -----------------------------------------------------------

// Plain Levenshtein distance. Small alphabet, tiny strings, called only on the
// error path — there is no reason for anything cleverer.
std::size_t edit_distance(std::string_view a, std::string_view b)
{
	std::vector<std::size_t> prev(b.size() + 1);
	std::vector<std::size_t> cur(b.size() + 1);
	for (std::size_t j = 0; j <= b.size(); ++j) {
		prev[j] = j;
	}
	for (std::size_t i = 1; i <= a.size(); ++i) {
		cur[0] = i;
		for (std::size_t j = 1; j <= b.size(); ++j) {
			const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
			cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
		}
		prev = cur;
	}
	return prev[b.size()];
}

// Returns a ready-to-append " (did you mean 'x'?)" when some candidate is close
// enough to be a typo, and an empty string when the word is simply unknown —
// suggesting a wildly different key is worse than suggesting nothing.
std::string suggest(std::string_view word, const std::string_view *candidates, std::size_t count)
{
	std::string_view best;
	std::size_t best_distance = std::numeric_limits<std::size_t>::max();
	for (std::size_t i = 0; i < count; ++i) {
		const std::size_t d = edit_distance(word, candidates[i]);
		if (d < best_distance) {
			best_distance = d;
			best = candidates[i];
		}
	}
	const std::size_t limit = std::max<std::size_t>(1, word.size() / 3 + 1);
	if (best.empty() || best_distance > limit) {
		return std::string();
	}
	return " (did you mean '" + std::string(best) + "'?)";
}

std::string suggest_section(std::string_view word, const rv_manifest_section_spec *sections,
	std::size_t section_count)
{
	std::vector<std::string_view> names;
	names.reserve(section_count);
	for (std::size_t i = 0; i < section_count; ++i) {
		names.push_back(sections[i].name);
	}
	return suggest(word, names.data(), names.size());
}

std::string suggest_key(std::string_view word, const rv_manifest_key_spec *keys,
	std::size_t key_count)
{
	std::vector<std::string_view> names;
	names.reserve(key_count);
	for (std::size_t i = 0; i < key_count; ++i) {
		names.push_back(keys[i].name);
	}
	return suggest(word, names.data(), names.size());
}

} // namespace rv_pdklib

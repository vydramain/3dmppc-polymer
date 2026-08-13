#include "rv_burner_text.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "rv_burner_schema.hpp"

namespace rv_pdktools
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

std::string suggest_section(std::string_view word, const section_spec *burn_sections, std::size_t burn_sections_size)
{
	std::vector<std::string_view> names;
	names.reserve(burn_sections_size);
	for (std::size_t i = 0; i < burn_sections_size; ++i) {
		names.push_back(burn_sections[i].name);
	}
	return suggest(word, names.data(), names.size());
}

} // namespace rv_pdktools

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rv_pdklib
{

// Every diagnostic of one manifest, in the order it was found. Each holds the
// line the author must look at; the file name is joined on at render time, so
// the message reads the way a compiler's does: `disc.toml:14: unknown key 'x'`.
class rv_manifest_failer
{
public:
	struct entry {
		int line = 0;
		std::string message;
	};

	// stop_at_first keeps only the leading error, the way the parser behaved
	// before it could recover. Off by default: one run should name every
	// mistake in the file.
	explicit rv_manifest_failer(bool stop_at_first = false)
		: stop_at_first_(stop_at_first)
	{
	}

	// Always false, so a stage can `return failer_.fail(...)`.
	bool fail(int line, const std::string &message);

	bool empty() const
	{
		return entries_.empty();
	}

	// True once no further diagnostic will be recorded — a stage that sees this
	// should stop walking rather than keep looking for errors nobody will read.
	bool exhausted() const
	{
		return stop_at_first_ && !entries_.empty();
	}

	const std::vector<entry> &entries() const
	{
		return entries_;
	}

	// One line per diagnostic. `origin` is the manifest's file name, empty for
	// text that came from nowhere in particular.
	std::string report(std::string_view origin) const;

private:
	std::vector<entry> entries_;
	bool stop_at_first_ = false;
};

} // namespace rv_pdklib

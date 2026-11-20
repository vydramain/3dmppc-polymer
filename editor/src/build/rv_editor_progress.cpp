// The editor's reader of the PDK tools' progress protocol (pdklib/rv_progress/rv_progress.hpp).

#include "build/rv_editor_progress.hpp"

#include "pdklib/rv_progress/rv_progress.hpp"

#include <algorithm>

namespace rv_editor
{

namespace
{

constexpr int permille_full = 1000;

// The innermost open run on the way down from `runs`: each open run's running step holds the next one.
// Null when no run is open. `chain` gets the open runs from the outermost in.
void rv_editor_open_chain(std::vector<rv_editor_run> &runs, std::vector<rv_editor_run *> &chain)
{
    rv_editor_run *run = runs.empty() || runs.back().end != rv_editor_run_end::open ? nullptr : &runs.back();
    while (run != nullptr) {
        chain.push_back(run);
        rv_editor_step *running = nullptr;
        for (rv_editor_step &step : run->steps) {
            if (step.state == rv_editor_step_state::running) {
                running = &step;
            }
        }
        run = running == nullptr || running->runs.empty() || running->runs.back().end != rv_editor_run_end::open ?
            nullptr :
            &running->runs.back();
    }
}

void rv_editor_apply(rv_editor_run &run, const rv_pdklib::rv_progress_line &line)
{
    using rv_pdklib::rv_progress_kind;
    switch (line.kind) {
    case rv_progress_kind::plan:
        run.steps.assign(line.labels.size(), rv_editor_step());
        for (std::size_t i = 0; i < line.labels.size(); ++i) {
            run.steps[i].label = line.labels[i];
        }
        break;
    case rv_progress_kind::step: {
        if (run.steps.size() < static_cast<std::size_t>(line.total)) {
            run.steps.resize(static_cast<std::size_t>(line.total));
        }
        const std::size_t at = static_cast<std::size_t>(line.n - 1);
        for (std::size_t i = 0; i < at; ++i) {
            run.steps[i].state = rv_editor_step_state::done;
        }
        rv_editor_step &step = run.steps[at];
        step.state = rv_editor_step_state::running;
        if (step.label.empty()) {
            step.label = line.label;
        }
        step.detail = line.detail;
        break;
    }
    case rv_progress_kind::end_ok:
        run.end = rv_editor_run_end::ok;
        for (rv_editor_step &step : run.steps) {
            step.state = rv_editor_step_state::done;
        }
        break;
    case rv_progress_kind::end_failed:
        run.end = rv_editor_run_end::failed;
        run.reason = line.reason;
        for (rv_editor_step &step : run.steps) {
            if (step.state == rv_editor_step_state::running) {
                step.state = rv_editor_step_state::failed;
            }
        }
        break;
    }
}

void rv_editor_interrupt(rv_editor_run &run)
{
    if (run.end != rv_editor_run_end::open) {
        return;
    }
    run.end = rv_editor_run_end::interrupted;
    for (rv_editor_step &step : run.steps) {
        if (step.state == rv_editor_step_state::running) {
            step.state = rv_editor_step_state::interrupted;
        }
        for (rv_editor_run &sub : step.runs) {
            rv_editor_interrupt(sub);
        }
    }
}

} // namespace

bool rv_editor_progress_feed(rv_editor_progress &progress, std::string_view line)
{
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    rv_pdklib::rv_progress_line parsed;
    if (!rv_pdklib::rv_progress_parse_line(line, parsed)) {
        return false;
    }
    std::vector<rv_editor_run *> chain;
    rv_editor_open_chain(progress.runs, chain);
    const auto same = std::find_if(chain.rbegin(), chain.rend(), [&](const rv_editor_run *run) {
        return run->tool == parsed.tool;
    });
    if (same != chain.rend()) {
        rv_editor_apply(**same, parsed);
        return true;
    }
    // A tool the open chain does not know: a sub-run of the step running in its innermost open run, else a top-level run.
    std::vector<rv_editor_run> *into = &progress.runs;
    if (!chain.empty()) {
        for (rv_editor_step &step : chain.back()->steps) {
            if (step.state == rv_editor_step_state::running) {
                into = &step.runs;
            }
        }
    }
    into->emplace_back();
    into->back().tool = parsed.tool;
    rv_editor_apply(into->back(), parsed);
    return true;
}

bool rv_editor_progress_feed_text(rv_editor_progress &progress, std::string_view chunk)
{
    bool any = false;
    while (!chunk.empty()) {
        const std::size_t end = chunk.find_first_of("\r\n");
        any = rv_editor_progress_feed(progress, chunk.substr(0, end)) || any;
        chunk.remove_prefix(end == std::string_view::npos ? chunk.size() : end + 1);
    }
    return any;
}

void rv_editor_progress_finish(rv_editor_progress &progress)
{
    for (rv_editor_run &run : progress.runs) {
        rv_editor_interrupt(run);
    }
}

int rv_editor_step_percent(const rv_editor_step &step)
{
    std::string_view rest = step.detail;
    int percent = -1;
    while (!rest.empty()) {
        const std::size_t end = rest.find(' ');
        const std::string_view token = rest.substr(0, end);
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
        int value = 0;
        if (token.size() > 1 && token.back() == '%' &&
            rv_pdklib::rv_progress_detail::parse_number(token.substr(0, token.size() - 1), value)) {
            percent = value <= 100 ? value : -1;
        }
    }
    return percent;
}

int rv_editor_run_permille(const rv_editor_run &run)
{
    if (run.end == rv_editor_run_end::ok) {
        return permille_full;
    }
    if (run.steps.empty()) {
        return 0;
    }
    int sum = 0;
    for (const rv_editor_step &step : run.steps) {
        if (step.state == rv_editor_step_state::done) {
            sum += permille_full;
        } else if (step.state == rv_editor_step_state::running) {
            sum += std::max(rv_editor_step_percent(step), 0) * (permille_full / 100);
        }
    }
    return sum / static_cast<int>(run.steps.size());
}

} // namespace rv_editor

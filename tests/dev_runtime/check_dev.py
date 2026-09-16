#!/usr/bin/env python3
"""Acceptance run for the v0.4 development runtime.

Not a self-check inside the console: this drives the real binary the way an
editor would, over the real protocol, and asserts what came back. It is the
only thing that fails when a guarantee quietly stops holding.

Python and not shell for one reason: every reload carries a byte count, and a
harness that miscounts its own payload tests nothing. Nothing in the console
depends on Python being installed.

Run it through check-dev.sh, which supplies the paths.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time

CONSOLE = os.environ.get("MPPC_CONSOLE", "build/pconsole/3dmppc")
BURNER = os.environ.get("MPPC_BURNER", "pdk/tools/build/mppcburner/mppcburner")
BAKER = os.environ.get("MPPC_BAKER", "pdk/tools/build/mppcbaker/mppcbaker")
DISC_ARCHIVE = os.environ.get("MPPC_DISC", "build/example-lua.mppcdisc")
DISC_DIR = os.environ.get("MPPC_DISCDIR", "build/example-lua.discdir")
FIXTURES = os.environ.get("MPPC_FIXTURES", "tests/dev_runtime/fixtures")

# Per-request patience. Generous: a reload that has to compile and run a
# candidate is still bounded by the console's own instruction ceiling, and this
# only has to be longer than that.
REPLY_TIMEOUT = 20.0

results = []
work = tempfile.mkdtemp(prefix="mppc-dev-")


def record(name, ok, detail=""):
    results.append((name, ok, detail))
    print(("PASS " if ok else "FAIL ") + name + (("  " + detail) if detail else ""))


def fixture(name):
    with open(os.path.join(FIXTURES, name), "rb") as handle:
        return handle.read()


class Session:
    """One console process, spoken to over stdin/stdout.

    Every reply is one line beginning with the request id, so the reader can
    match answers to requests and tell an unsolicited event (id 0) from an
    answer. The child is killed on the way out whatever happens: a harness that
    can leave a paused console behind is a harness that wedges a CI run.
    """

    def __init__(self, disc, extra=(), card=None, dump=None):
        self.next_id = 1
        self.events = []
        argv = [CONSOLE, "--dev", "--fixed-step", "--mode=headless", "--frames", "100000",
                "-m", card or os.path.join(work, "card-%d" % len(results))]
        if dump:
            argv += ["--dump-frame", dump]
        argv += list(extra)
        argv.append(disc)
        self.log = open(os.path.join(work, "log-%d.txt" % len(results)), "w+b")
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.log)

    def send(self, verb, payload=None):
        rid = self.next_id
        self.next_id += 1
        header = "%d %s" % (rid, verb)
        if payload is not None:
            header += " bytes %d" % len(payload)
        self.proc.stdin.write(header.encode() + b"\n")
        if payload is not None:
            self.proc.stdin.write(payload)
        self.proc.stdin.flush()
        return rid

    def read_reply(self, rid):
        deadline = time.time() + REPLY_TIMEOUT
        while time.time() < deadline:
            line = self.proc.stdout.readline()
            if not line:
                raise AssertionError("channel closed while waiting for reply %d" % rid)
            fields = line.decode(errors="replace").strip()
            head = fields.split(" ", 1)[0]
            if head == "0":
                self.events.append(fields)
                continue
            if head == str(rid):
                return fields
            raise AssertionError("out of order reply: wanted %d, got %r" % (rid, fields))
        raise AssertionError("no reply to %d within %.0fs" % (rid, REPLY_TIMEOUT))

    def ask(self, verb, payload=None):
        return self.read_reply(self.send(verb, payload))

    def fields(self, verb, payload=None):
        reply = self.ask(verb, payload)
        out = {"_raw": reply, "_ok": " ok " in reply or reply.endswith(" ok")}
        for token in reply.split(" ")[1:]:
            if "=" in token:
                key, value = token.split("=", 1)
                out[key] = value
        return out

    def stderr_text(self):
        self.log.flush()
        self.log.seek(0)
        return self.log.read().decode(errors="replace")

    def close(self, graceful=True):
        try:
            if graceful and self.proc.poll() is None:
                self.send("quit")
                self.proc.stdin.flush()
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
        except (BrokenPipeError, OSError):
            pass
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=5)
        return self.proc.returncode


def run_plain(disc, frames, dump, card, dev=False, extra=()):
    argv = [CONSOLE, "--fixed-step", "--mode=headless", "--frames", str(frames),
            "-m", card, "--dump-frame", dump]
    if dev:
        argv.append("--dev")
    argv += list(extra)
    argv.append(disc)
    done = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
    return done


def burner_checksum():
    """The checksum the burner stamped into the archive we are running.

    Re-burning to a throwaway path is the honest way to get it: it is the same
    computation over the same sources, so a mismatch means the console is
    reporting something other than what it loaded.
    """
    done = subprocess.run([BURNER, "build", "mppcdiscs/example-lua", "-o",
                           os.path.join(work, "checksum.mppcdisc"), "--baker", BAKER],
                          capture_output=True, timeout=300)
    for line in (done.stdout + done.stderr).decode(errors="replace").splitlines():
        if "checksum:" in line:
            return line.split("checksum:")[1].strip()
    return "(not found)"


def clear_colour(path):
    """First pixel of a binary PPM: the clear colour the frame was filled with."""
    with open(path, "rb") as handle:
        data = handle.read()
    index, fields = 0, []
    while len(fields) < 4:
        while data[index:index + 1].isspace():
            index += 1
        start = index
        while not data[index:index + 1].isspace():
            index += 1
        fields.append(data[start:index])
    return tuple(data[index + 1:index + 4])


# --- A01/A02: --dev must not change the ordinary run ------------------------

def check_dev_is_invisible():
    base = os.path.join(work, "base.ppm")
    withdev = os.path.join(work, "withdev.ppm")
    run_plain(DISC_ARCHIVE, 10, base, os.path.join(work, "c1"))
    run_plain(DISC_ARCHIVE, 10, withdev, os.path.join(work, "c2"), dev=True)
    same = open(base, "rb").read() == open(withdev, "rb").read()
    record("A02 --dev with no commands renders the identical frame", same)
    return base


# --- A03: one machine, two media -------------------------------------------

def check_media_agree(base):
    if not os.path.isdir(DISC_DIR):
        record("A03 archive and unpacked directory agree", False, "no %s" % DISC_DIR)
        return
    out = os.path.join(work, "dir.ppm")
    run_plain(DISC_DIR, 10, out, os.path.join(work, "c3"))
    same = os.path.exists(out) and open(base, "rb").read() == open(out, "rb").read()
    record("A03 archive and unpacked directory agree", same)


# --- A04/A05/A06: time only moves when it is told to -----------------------

def check_frame_control():
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    try:
        first = session.fields("status")
        record("A04 --dev-paused stops before frame 0",
               first.get("frame") == "0" and first.get("mode") == "paused",
               first["_raw"])

        # disc_hash must be the checksum of the disc.so that is actually
        # mapped - the one field an editor uses to decide that the C++ was
        # rebuilt and the process has to be restarted. 16 hex characters, and
        # the same ones the burner printed when it stamped them.
        stamped = burner_checksum()
        record("A04b status reports the checksum of the loaded disc.so",
               first.get("disc_hash") == stamped,
               "status %s, burner %s" % (first.get("disc_hash"), stamped))

        # Several status requests with real time passing between them: the frame
        # number must not move, because a paused console creates no frames.
        session.fields("status")
        time.sleep(0.4)
        held = session.fields("status")
        record("A05 a paused console does not advance machine time",
               held.get("frame") == "0", held["_raw"])

        # Three steps in ONE write. They must not collapse: each is a frame and
        # each gets its own answer, in order.
        ids = [session.send("step"), session.send("step"), session.send("step")]
        replies = [session.read_reply(rid) for rid in ids]
        after = session.fields("status")
        record("A06 three steps in one write are three frames",
               all(" ok completed=1" in r for r in replies) and after.get("frame") == "3",
               after["_raw"])

        resumed = session.fields("resume")
        record("A06b resume reports running", resumed.get("mode") == "running")
    finally:
        session.close()


# --- A07: the reload itself ------------------------------------------------

def check_reload_and_state():
    dump = os.path.join(work, "reloaded.ppm")
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"], dump=dump)
    try:
        session.ask("step")
        session.ask("step")
        before = session.fields("status")
        count_before = session.fields("get frame_count")

        ok = session.fields("reload entry", fixture("ok_b.lua"))
        record("A07 a valid candidate is accepted",
               ok["_ok"] and ok.get("entry_revision") == "1"
               and ok.get("entry_hash") != before.get("entry_hash"),
               ok["_raw"])

        session.ask("step")
        count_after = session.fields("get frame_count")
        preserved = (int(count_after.get("value", -1)) == int(count_before.get("value", -2)) + 1)
        record("A07b state survives the swap: the counter continues",
               preserved, "%s -> %s" % (count_before.get("value"), count_after.get("value")))

        mode = session.fields("status")
        record("A07c reload does not change the pause state",
               mode.get("mode") == "paused", mode["_raw"])
    finally:
        session.close()
    if os.path.exists(dump):
        record("A07d the new code draws the new frame",
               clear_colour(dump) == (41, 41, 198), str(clear_colour(dump)))


# --- A08/A09: every refusal keeps the running code -------------------------

REFUSALS = [
    ("syntax.lua", "compile", "0"),
    ("body_throws.lua", "body", "1"),
    ("not_a_table.lua", "not_a_table", "1"),
    ("no_attach.lua", "no_attach", "1"),
    ("attach_false.lua", "attach_refused", "1"),
    ("attach_throws_dirty.lua", "attach", "1"),
    ("body_hangs.lua", "insn_ceiling", None),
    ("attach_hangs.lua", "insn_ceiling", None),
]


def check_refusals():
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    try:
        session.ask("step")
        anchor = session.fields("status")
        for name, token, effects in REFUSALS:
            reply = session.fields("reload entry", fixture(name))
            after = session.fields("status")
            token_ok = reply.get("error") == token
            kept = (after.get("entry_revision") == anchor.get("entry_revision")
                    and after.get("entry_hash") == anchor.get("entry_hash"))
            effects_ok = effects is None or reply.get("effects") == effects
            record("A08 %-24s -> %-12s, running code kept" % (name, token),
                   token_ok and kept and effects_ok, reply["_raw"])
        # The channel is still usable after every one of them, including the two
        # that had to be interrupted.
        alive = session.fields("status")
        record("A09 the console still answers after an interrupted candidate",
               alive["_ok"], alive["_raw"])
        stepped = session.fields("step")
        record("A09b and it still runs frames", " ok completed=1" in stepped["_raw"])
    finally:
        session.close()


# --- A10: reading state may not run script code ---------------------------

def check_get_types():
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    try:
        session.ask("step")
        missing = session.fields("get definitely_not_there")
        number = session.fields("get frame_count")
        table = session.fields("get cv")
        record("A10 get: a missing key is found=0 type=nil",
               missing.get("found") == "0" and missing.get("type") == "nil", missing["_raw"])
        record("A10b get: a number comes back typed", number.get("type") == "number")
        record("A10c get: a non-scalar is labelled, never evaluated",
               table.get("type") in ("table", "other", "nil"), table["_raw"])
    finally:
        session.close()


# --- A11: repeated reloads must not accumulate ----------------------------

def check_no_accumulation(rounds=60):
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    try:
        session.ask("step")
        session.ask("reload entry", fixture("ok_a.lua"))
        baseline = session.fields("gc")
        good, bad = fixture("ok_b.lua"), fixture("syntax.lua")
        for i in range(rounds):
            session.ask("reload entry", bad)
            session.ask("reload entry", good if i % 2 else fixture("ok_a.lua"))
        after = session.fields("gc")
        slots_held = baseline.get("chunks") == after.get("chunks")
        # A plateau, not an exact match: the collector is allowed to keep
        # different amounts of slack for two different programs.
        used_before, used_after = int(baseline["lua_used"]), int(after["lua_used"])
        plateau = used_after - used_before < 64 * 1024
        record("A11 %d good and %d refused reloads leave no slots behind" % (rounds, rounds),
               slots_held, "chunks %s -> %s" % (baseline.get("chunks"), after.get("chunks")))
        record("A11b and the script heap comes back to a plateau",
               plateau, "lua_used %d -> %d" % (used_before, used_after))
    finally:
        session.close()


# --- A12: a disc from before this contract existed ------------------------

LEGACY_CHUNK = b"""-- A chunk in the shape discs had before attach() existed: state in chunk
-- locals, no attach. It must still boot, and must not be reloadable.
local M = {}
local cv, w, h
function M.disc_initialize(cv_, ca_, cio_, cd_)
\tcv = pdk.cast("rv_cv*", cv_)
\tw = tonumber(pdk.cv_screen_width(cv))
\th = tonumber(pdk.cv_screen_height(cv))
end
function M.frame_update(dt) return false end
function M.frame_render()
\tpdk.cv_frame_configure(cv, 0, pdk.new("rv_color", { 20, 24, 40 }))
end
function M.disc_shutdown() end
return M
"""


def make_dir_copy(name, script=None):
    """A private copy of the unpacked disc, with symlinks dereferenced.

    -L matters: the published script is a symlink back into the repository, and
    a test must never write through it.
    """
    target = os.path.join(work, name)
    shutil.copytree(DISC_DIR, target, symlinks=False)
    if script is not None:
        with open(os.path.join(target, "example-lua.lua"), "wb") as handle:
            handle.write(script)
    return target


def check_legacy_disc():
    if not os.path.isdir(DISC_DIR):
        record("A12 a disc without attach still boots", False, "no %s" % DISC_DIR)
        return
    disc = make_dir_copy("legacy.discdir", LEGACY_CHUNK)
    session = Session(disc, extra=["--dev-paused"])
    try:
        status = session.fields("status")
        record("A12 a disc without attach still boots, and says it cannot reload",
               status["_ok"] and status.get("entry_reloadable") == "0", status["_raw"])
        refused = session.fields("reload entry", fixture("ok_b.lua"))
        record("A12b and a reload of it is refused",
               refused.get("error") == "not_reloadable", refused["_raw"])
        stepped = session.fields("step")
        record("A12c while the disc itself keeps running", " ok completed=1" in stepped["_raw"])
    finally:
        session.close()


# --- A13/A14: where the bytes may come from -------------------------------

def check_file_form():
    if not os.path.isdir(DISC_DIR):
        record("A13 the file form re-reads a live directory", False, "no %s" % DISC_DIR)
        return
    disc = make_dir_copy("live.discdir")
    session = Session(disc, extra=["--dev-paused"])
    try:
        session.ask("step")
        before = session.fields("status")
        record("A13 a directory medium reports itself live",
               before.get("medium") == "live", before["_raw"])
        # Edit the file on disk, exactly as an editor would, then ask the
        # console to re-read it. No payload, no burner.
        with open(os.path.join(disc, "example-lua.lua"), "wb") as handle:
            handle.write(fixture("ok_b.lua"))
        reread = session.fields("reload entry")
        record("A13b and the file form picks the edit up",
               reread["_ok"] and reread.get("entry_revision") == "1"
               and reread.get("entry_hash") != before.get("entry_hash"), reread["_raw"])
    finally:
        session.close()

    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    try:
        fixed = session.fields("status")
        refused = session.fields("reload entry")
        record("A14 an archive refuses the file form instead of pretending",
               fixed.get("medium") == "fixed" and refused.get("error") == "unsupported_medium",
               refused["_raw"])
    finally:
        session.close()


# --- A15/A16: the protocol under abuse ------------------------------------

def check_protocol_abuse():
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    try:
        unknown = session.fields("definitely-not-a-verb")
        record("A15 an unknown verb is an error, not a disconnect",
               unknown.get("error") == "protocol", unknown["_raw"])
        target = session.fields("reload something-else", b"x")
        record("A15b a target this version does not have is named as such",
               target.get("error") == "unsupported_target", target["_raw"])
        # A blank line and a request whose id is not a number: the first is
        # nothing at all, the second is answerable and must not take the
        # channel down with it.
        session.proc.stdin.write(b"\n\nnot-an-id status\n")
        session.proc.stdin.flush()
        line = session.proc.stdout.readline().decode(errors="replace").strip()
        record("A15c a non-numeric id is answered against id 0",
               line.startswith("0 err error=protocol"), line)
        alive = session.fields("status")
        record("A15d and the channel survives all of it", alive["_ok"])
    finally:
        session.close()

    # A payload that never finishes. The channel must refuse and close rather
    # than read the next command out of the middle of a script.
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    try:
        session.proc.stdin.write(b"1 reload entry bytes 4096\nonly-a-few-bytes")
        session.proc.stdin.flush()
        session.proc.stdin.close()
        deadline = time.time() + 15
        saw = ""
        while time.time() < deadline:
            line = session.proc.stdout.readline()
            if not line:
                break
            saw = line.decode(errors="replace").strip()
            if "payload_timeout" in saw:
                break
        record("A16 a truncated payload is refused, not executed",
               "payload_timeout" in saw, saw or "(no answer)")
    finally:
        session.close(graceful=False)


# --- A18: a game hook that breaks after the reload succeeded ---------------

def check_game_error_recovery():
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    try:
        session.ask("step")
        accepted = session.fields("reload entry", fixture("update_throws.lua"))
        record("A18 a candidate that only fails LATER is still accepted",
               accepted["_ok"], accepted["_raw"])

        # The frame runs, the hook fails. The console must stop there: a frozen
        # picture with no explanation is the worst possible answer.
        stepped = session.fields("step")
        held = session.fields("status")
        saw_event = any("event=script_error" in line for line in session.events)
        record("A18b the console stops on a failed game hook and says so",
               saw_event and held.get("mode") == "paused" and held.get("script_error"),
               (session.events[-1] if session.events else "(no event)"))

        # And the session recovers: fix the code, step, carry on. No restart,
        # and no latch that would have silenced lua for the rest of the run.
        fixed = session.fields("reload entry", fixture("ok_b.lua"))
        before = len(session.events)
        again = session.fields("step")
        record("A18c and a fixed chunk runs again without a restart",
               fixed["_ok"] and " ok completed=1" in again["_raw"]
               and len(session.events) == before, again["_raw"])
    finally:
        session.close()


# --- A17: the way out -----------------------------------------------------

def check_quit():
    session = Session(DISC_ARCHIVE, extra=["--dev-paused"])
    reply = session.fields("quit")
    # WAIT for it. Killing the child here would prove nothing: the point of the
    # check is that the console leaves by its own ordinary path - through
    # disc_shutdown, the loader teardown and the frame dump - rather than being
    # taken down from outside.
    try:
        code = session.proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        code = "timed out"
    log = session.stderr_text()
    session.close(graceful=False)
    record("A17 quit leaves by the ordinary path",
           reply.get("mode") == "stopped" and code == 0
           and "shutdown requested over the development channel" in log,
           "exit %s" % code)


def main():
    for path, what in ((CONSOLE, "console"), (DISC_ARCHIVE, "archive")):
        if not os.path.exists(path):
            print("missing %s: %s" % (what, path))
            return 2
    base = check_dev_is_invisible()
    check_media_agree(base)
    check_frame_control()
    check_reload_and_state()
    check_refusals()
    check_get_types()
    check_no_accumulation()
    check_legacy_disc()
    check_file_form()
    check_protocol_abuse()
    check_game_error_recovery()
    check_quit()

    failed = [name for name, ok, _ in results if not ok]
    print("\n%d checks, %d failed" % (len(results), len(failed)))
    for name in failed:
        print("  FAILED: " + name)
    if not failed:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print("artefacts kept in " + work)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

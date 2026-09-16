#!/usr/bin/env python3
"""The checks that need a real desktop session, not a headless one.

Two things cannot be shown headless, and both are claims this release makes:

  1. A long pause does not damage audio pacing. The pause stops feeding the
     device, the queue drains, and the run must still come back at the right
     speed with the audio output still pacing it - not permanently fallen back
     to the steady clock. That needs a real audio device.
  2. The physical Pause key stops the console. The key is read by the platform
     from the real event stream, so the only honest way to check it is to put a
     real key event into that stream - a mock would be testing the mock. That
     needs a window WITH KEYBOARD FOCUS, which no part of this script can take.

The key is injected at the evdev level through /dev/uinput, which is where a
physical keyboard's events come from too. The compositor routes it to the
focused window, SDL sees it there, and the console's platform layer counts it.

Requires: a running display session, and /dev/uinput writable by this user.
Run from the repository root:  python3 tests/dev_runtime/check_pause_key.py
"""

import fcntl
import os
import struct
import subprocess
import sys
import time

CONSOLE = os.environ.get("MPPC_CONSOLE", "build/pconsole/3dmppc")
DISC = os.environ.get("MPPC_DISC", "build/example-lua.mppcdisc")

# linux/uinput.h and linux/input.h. Spelled out rather than parsed from the
# headers: these numbers are kernel ABI and do not move.
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_DEV_SETUP = 0x405C5503

EV_SYN, EV_KEY = 0x00, 0x01
SYN_REPORT = 0
KEY_ESC = 1
KEY_PAUSE = 119

results = []


def record(name, ok, detail=""):
    results.append((name, ok))
    print(("PASS " if ok else "FAIL ") + name + (("  " + detail) if detail else ""))


class VirtualKeyboard:
    """One virtual keyboard that can press exactly one key."""

    def __init__(self):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        for key in (KEY_PAUSE, KEY_ESC):
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, key)
        # struct uinput_setup: input_id{bustype, vendor, product, version},
        # name[80], ff_effects_max. BUS_USB so the compositor treats it as an
        # ordinary keyboard rather than something it may ignore.
        setup = struct.pack("<HHHH80sI", 0x03, 0x1234, 0x5678, 1,
                            b"3dmppc acceptance keyboard", 0)
        fcntl.ioctl(self.fd, UI_DEV_SETUP, setup)
        fcntl.ioctl(self.fd, UI_DEV_CREATE)
        # The compositor has to notice the new device before it will route
        # anything from it. There is no event for "you may now type".
        time.sleep(1.0)

    def emit(self, type_, code, value):
        os.write(self.fd, struct.pack("<qqHHi", 0, 0, type_, code, value))

    def tap(self, key=KEY_PAUSE):
        self.emit(EV_KEY, key, 1)
        self.emit(EV_SYN, SYN_REPORT, 0)
        time.sleep(0.05)
        self.emit(EV_KEY, key, 0)
        self.emit(EV_SYN, SYN_REPORT, 0)

    def hold(self, seconds):
        """Press and keep pressed. Key repeat must NOT pause once per repeat."""
        self.emit(EV_KEY, KEY_PAUSE, 1)
        self.emit(EV_SYN, SYN_REPORT, 0)
        time.sleep(seconds)
        self.emit(EV_KEY, KEY_PAUSE, 0)
        self.emit(EV_SYN, SYN_REPORT, 0)

    def close(self):
        try:
            fcntl.ioctl(self.fd, UI_DEV_DESTROY)
        finally:
            os.close(self.fd)


class Console:
    """A windowed console with the development channel open.

    The channel is here only so the test can ASK what the frame counter is
    doing. The pause itself needs no channel - that is the whole point of the
    key - but a test that cannot read the frame number could only check that
    the window looked frozen, which is not a check.
    """

    def __init__(self, probe=False):
        argv = [CONSOLE, "--mode_ca=null", "--scale", "2",
                "--frames", "0", "-m", "/tmp/mppc-pausekey.card", DISC]
        if not probe:
            # The probe run deliberately has no channel: it only has to die.
            argv.insert(1, "--dev")
        self.log = open("/tmp/mppc-pausekey.log", "w+b")
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.log)
        self.next_id = 1
        time.sleep(1.5)  # let the window come up and take focus

    def status(self):
        rid = self.next_id
        self.next_id += 1
        self.proc.stdin.write(b"%d status\n" % rid)
        self.proc.stdin.flush()
        deadline = time.time() + 10
        while time.time() < deadline:
            line = self.proc.stdout.readline().decode(errors="replace").strip()
            if not line:
                break
            if line.startswith("%d " % rid):
                out = {}
                for token in line.split(" ")[1:]:
                    if "=" in token:
                        key, value = token.split("=", 1)
                        out[key] = value
                return out
        raise AssertionError("no status reply")

    def close(self):
        try:
            if self.proc.stdin is not None and self.proc.poll() is None:
                self.proc.stdin.write(b"999 quit\n")
                self.proc.stdin.flush()
                self.proc.wait(timeout=5)
        except Exception:
            pass
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=5)


def check_audio_pacing_across_a_pause():
    """A pause must cost the audio one underrun, not the pacing mode.

    While stopped the console feeds the device nothing, so its queue empties.
    The stall detector that watches for a device which has stopped draining
    must NOT read that as a broken device and switch the run to the steady
    clock for good - and on resume the frame rate must come back to the target
    rather than sprinting to catch up on a deadline left in the past.
    """
    log = open("/tmp/mppc-pacing.log", "w+b")
    proc = subprocess.Popen([CONSOLE, "--dev", "--scale", "2", "--frames", "0",
                             "-m", "/tmp/mppc-pacing.card", DISC],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)

    def ask(rid, verb):
        proc.stdin.write(b"%d %s\n" % (rid, verb))
        proc.stdin.flush()
        while True:
            line = proc.stdout.readline().decode(errors="replace").strip()
            if not line:
                raise AssertionError("channel closed")
            if line.startswith("%d " % rid):
                return dict(token.split("=", 1) for token in line.split(" ")[1:] if "=" in token)

    try:
        time.sleep(1.5)
        ask(1, b"pause")
        time.sleep(3.0)                      # long enough to empty any queue
        before = ask(2, b"status")
        ask(3, b"resume")
        started = time.time()
        time.sleep(2.0)
        after = ask(4, b"status")
        elapsed = time.time() - started
        ask(5, b"quit")
        proc.wait(timeout=10)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)
    log.flush()
    log.seek(0)
    text = log.read().decode(errors="replace")

    if "paced by audio output" not in text:
        print("SKIP: no audio device is pacing this run, so there is nothing to check here.")
        print("      (the log says: %s)" % next((l for l in text.splitlines() if "paced by" in l), "?"))
        return 0

    fps = (int(after["frame"]) - int(before["frame"])) / elapsed
    # A wide band on purpose: this is a real scheduler on a real desktop, and
    # the claim being checked is "roughly the target", not a stopwatch.
    record("D01 the frame rate comes back to the target after a long pause",
           45.0 < fps < 75.0, "%.1f fps over %.2f s" % (fps, elapsed))
    record("D02 a pause does not permanently switch pacing to the clock",
           "stopped draining" not in text,
           next((l for l in text.splitlines() if "audio output:" in l), ""))
    return 0


def main():
    if not os.path.exists(DISC):
        print("missing disc: " + DISC)
        return 2
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        print("no display session; this check needs a real window")
        return 2

    if check_audio_pacing_across_a_pause() != 0:
        return 1

    keyboard = VirtualKeyboard()

    # PRECONDITION, and not a formality: a synthetic key only reaches an
    # application if the compositor routes it there, which means the console's
    # window must hold keyboard focus. Nothing in this script can take focus -
    # there is no portable way to ask for it - so the state of the desktop
    # decides whether this check can run at all.
    #
    # It is probed with Escape, which this console quits on. If Escape does not
    # end the process, keys are not arriving, and every check below would pass
    # or fail for a reason that has nothing to do with the Pause key. A run that
    # cannot tell those apart must SKIP, not report.
    probe = Console(probe=True)
    keyboard.tap(KEY_ESC)
    time.sleep(1.5)
    reached = probe.proc.poll() is not None
    probe.close()
    if not reached:
        keyboard.close()
        print("SKIP: synthetic keys are not reaching the console window.")
        print("      The window does not hold keyboard focus in this session, so the")
        print("      Pause key cannot be exercised here. Run this from a desktop")
        print("      session where the new window is focused, or click the window")
        print("      once after it opens and run again.")
        return 2

    console = Console()
    try:
        first = console.status()
        time.sleep(0.5)
        second = console.status()
        running = int(second["frame"]) > int(first["frame"])
        record("P01 the console is running before the key is touched",
               running and second.get("mode") == "running",
               "frame %s -> %s" % (first.get("frame"), second.get("frame")))

        keyboard.tap()
        time.sleep(0.3)
        stopped = console.status()
        time.sleep(0.6)
        held = console.status()
        record("P02 one tap of the Pause key stops the console",
               stopped.get("mode") == "paused" and held.get("frame") == stopped.get("frame"),
               "mode %s, frame %s -> %s" % (stopped.get("mode"), stopped.get("frame"),
                                            held.get("frame")))

        keyboard.tap()
        time.sleep(0.3)
        again = console.status()
        time.sleep(0.5)
        moving = console.status()
        record("P03 a second tap starts it again",
               again.get("mode") == "running"
               and int(moving["frame"]) > int(again["frame"]),
               "frame %s -> %s" % (again.get("frame"), moving.get("frame")))

        # Held down, the key must pause once, not once per repeat: an odd number
        # of edges would leave it paused, an even number running, and a stream
        # of repeats would make the result a coin toss.
        keyboard.hold(1.5)
        time.sleep(0.3)
        after_hold = console.status()
        time.sleep(0.5)
        after_hold2 = console.status()
        consistent = (after_hold.get("mode") == "paused"
                      and after_hold2.get("frame") == after_hold.get("frame"))
        record("P04 holding the key is one pause, not one per repeat",
               consistent, "mode %s, frame %s -> %s" % (after_hold.get("mode"),
                                                        after_hold.get("frame"),
                                                        after_hold2.get("frame")))

    finally:
        console.close()
        keyboard.close()

    failed = [name for name, ok in results if not ok]
    print("\n%d checks, %d failed" % (len(results), len(failed)))
    for name in failed:
        print("  FAILED: " + name)
    print("console log: /tmp/mppc-pausekey.log")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

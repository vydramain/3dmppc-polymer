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
import shutil
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
    """A virtual keyboard the compositor will accept as one."""

    def __init__(self):
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        # A FULL key range, not just the two keys this needs. libinput decides
        # what a device IS from the keys it advertises: a device offering two
        # codes is not classified as a keyboard, and the compositor then routes
        # nothing from it - which looks exactly like a focus problem and is not
        # one. Advertising the ordinary range makes it an ordinary keyboard.
        for key in range(KEY_ESC, 128):
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

    def ask_ok(self, verb):
        return self.status(verb)

    def status(self, verb=b"status"):
        rid = self.next_id
        self.next_id += 1
        if isinstance(verb, str):
            verb = verb.encode()
        self.proc.stdin.write(b"%d %s\n" % (rid, verb))
        self.proc.stdin.flush()
        deadline = time.time() + 10
        while time.time() < deadline:
            line = self.proc.stdout.readline().decode(errors="replace").strip()
            if not line:
                break
            if line.startswith("%d " % rid):
                out = {"_raw": line}
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


def read_ppm(path):
    """(width, height, pixels) from a binary PPM. magick writes them; parsing
    one is six lines, which is cheaper than a dependency."""
    data = open(path, "rb").read()
    at = [0]

    def token():
        while data[at[0]:at[0] + 1].isspace():
            at[0] += 1
        start = at[0]
        while not data[at[0]:at[0] + 1].isspace():
            at[0] += 1
        return data[start:at[0]]

    assert token() == b"P6"
    width, height = int(token()), int(token())
    token()
    at[0] += 1
    return width, height, memoryview(data)[at[0]:]


def check_pause_overlay_is_drawn():
    """A stopped console must SAY it is stopped.

    Checked by looking at the window, because that is the only place the answer
    exists: the overlay is drawn into a copy and presented, so it deliberately
    does not appear in --dump-frame, and the disc's own last frame stays exactly
    what the disc drew.
    """
    if not (shutil.which("spectacle") and shutil.which("magick")):
        print("SKIP D03: needs spectacle and magick to look at the window.")
        return
    log = open("/tmp/mppc-overlay.log", "w+b")
    proc = subprocess.Popen([CONSOLE, "--paused", "--scale", "3", "--mode_ca=null",
                             "-m", "/tmp/mppc-overlay.card", DISC],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=log)
    shot = "/tmp/mppc-overlay.png"
    try:
        time.sleep(3.0)
        # The active window, not the whole screen: the console window is the one
        # that just opened, and hunting for it inside a desktop-sized image is
        # work with no payoff.
        subprocess.run(["spectacle", "-b", "-n", "-a", "-o", shot],
                       capture_output=True, timeout=60)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    if not os.path.exists(shot):
        record("D03 a stopped console says CONSOLE PAUSED on screen", False, "no screenshot")
        return

    subprocess.run(["magick", shot, "/tmp/mppc-overlay.ppm"], capture_output=True, timeout=60)
    width, height, pixels = read_ppm("/tmp/mppc-overlay.ppm")
    if width < 600 or height < 450:
        record("D03 a stopped console says CONSOLE PAUSED on screen", False,
               "captured %dx%d - that is not the console window" % (width, height))
        return

    # The console's own picture first, not the whole grab: the window
    # DECORATION has white in it (the title text and the close button), and
    # including that would widen any bounding box until the shape test meant
    # nothing. Paused at frame 0 the content is pure black except the label, so
    # the exactly-black pixels ARE the content rectangle.
    black = [(x, y) for y in range(height) for x in range(0, width, 2)
             if pixels[(y * width + x) * 3] == 0 and pixels[(y * width + x) * 3 + 1] == 0
             and pixels[(y * width + x) * 3 + 2] == 0]
    if len(black) < 1000:
        record("D03 a stopped console says CONSOLE PAUSED, centred in its own picture", False,
               "no black content area found in the grab")
        return
    cx0, cx1 = min(p[0] for p in black), max(p[0] for p in black)
    cy0, cy1 = min(p[1] for p in black), max(p[1] for p in black)

    ink = [(x, y) for y in range(cy0, cy1 + 1) for x in range(cx0, cx1 + 1, 2)
           if pixels[(y * width + x) * 3] > 200 and pixels[(y * width + x) * 3 + 1] > 200
           and pixels[(y * width + x) * 3 + 2] > 200]
    if not ink:
        record("D03 a stopped console says CONSOLE PAUSED, centred in its own picture", False,
               "no white pixels inside the console picture")
        return
    x0, x1 = min(p[0] for p in ink), max(p[0] for p in ink)
    y0, y1 = min(p[1] for p in ink), max(p[1] for p in ink)
    band = (x1 - x0) > 3 * (y1 - y0)   # a line of text, not a blob
    mid_y = (cy0 + cy1) / 2
    mid_x = (cx0 + cx1) / 2
    centred = (abs((y0 + y1) / 2 - mid_y) < (cy1 - cy0) * 0.12
               and abs((x0 + x1) / 2 - mid_x) < (cx1 - cx0) * 0.12)
    record("D03 a stopped console says CONSOLE PAUSED, centred in its own picture",
           band and centred and len(ink) > 200,
           "%d px, %dx%d band; picture %dx%d at (%d,%d)"
           % (len(ink), x1 - x0, y1 - y0, cx1 - cx0, cy1 - cy0, cx0, cy0))


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
    check_pause_overlay_is_drawn()

    keyboard = VirtualKeyboard()
    console = Console()
    try:
        # The MECHANISM first, over the channel. If this fails, nothing below
        # can be blamed on key delivery.
        console.ask_ok("pause")
        by_channel = console.status()
        console.ask_ok("resume")
        record("P00 the pause mechanism itself works over the channel",
               by_channel.get("mode") == "paused", by_channel["_raw"])

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
        arrived = stopped.get("mode") == "paused"
        if not arrived:
            # P00 proved the pause works, so what failed here is the key not
            # reaching the window. This script cannot tell a platform-layer
            # defect from a compositor that will not route a synthetic device,
            # and it must not guess: it says so and fails, because an
            # unprovable claim is not a passing one.
            print("UNPROVEN P02: the pause works (P00) but the injected Pause key did not")
            print("         arrive. Either the platform layer is not reading it, or this")
            print("         compositor does not route a virtual input device to the window.")
            print("         Press the real key by hand to tell the two apart.")
        record("P02 one tap of the Pause key stops the console",
               arrived and held.get("frame") == stopped.get("frame"),
               "mode %s, frame %s -> %s" % (stopped.get("mode"), stopped.get("frame"),
                                            held.get("frame")))
        if not arrived:
            return 1

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

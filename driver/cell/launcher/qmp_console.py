#!/usr/bin/env python3
"""
qmp_console.py — QMP client for driving a QEMU guest's console via
synthetic keyboard input, used to automate ReactOS's text-mode
dialogs and cmd.exe console (no guest networking or agent needed).

Grown out of Phase 5's actual, hands-on driver verification
(driver/vsdev/run-test.sh, ROADMAP.md Phase 5) rather than written
speculatively - every primitive here (screendump, sendkey, type) was
exercised live, repeatedly, driving a real ReactOS boot through its
LiveCD language dialog and a real cmd.exe session.

Usage:
  qmp_console.py <qmp-socket> screendump <out.ppm>
  qmp_console.py <qmp-socket> sendkey <qcode>[,<qcode>...]
  qmp_console.py <qmp-socket> type <text>
  qmp_console.py <qmp-socket> click <x-fraction> <y-fraction>
  qmp_console.py <qmp-socket> dismiss-dialog <x-fraction> <y-fraction> \\
      <r> <g> <b> [--tolerance N] [--timeout SECONDS] [--interval SECONDS] \\
      [--click-x X --click-y Y]

<qmp-socket> is either a filesystem path to a QMP unix socket (the
original, Linux-sandbox-verified form) or "tcp:HOST:PORT". The tcp:
form is a real, narrow addition found live on a Windows host: that
build of Python for Windows has no socket.AF_UNIX at all, so ntcell
falls back to a TCP QMP endpoint there (see ntcell's IS_WINDOWS_HOST
branch) and this script needs to speak it too. Unix-socket behavior on
Linux is unchanged.

`dismiss-dialog` is the real fix for a genuine, reproducible flake this
project hit (driver/vsdev/run-test.sh, see its README): earlier
versions sent exactly two blind `ret` keypresses at fixed offsets to
dismiss the ReactOS LiveCD's language-selection dialog, timed against
one particular boot's observed speed. Real TCG software-emulation boot
timing varies enough between runs (host load-dependent) that a fixed
offset sometimes fires before the dialog has even rendered (a
no-op - the dialog is then still open when later automation assumes a
plain desktop) and sometimes fires after focus has already moved
elsewhere. This polls a screenshot pixel that's reliably part of the
dialog body (a color that never appears on the plain desktop
background) and only proceeds once that pixel reads as "no dialog" for
two consecutive polls, sending `ret` on every poll where the dialog is
still detected - adaptive, not a wider blind guess. Needs Pillow
(`pip install pillow`) to read the PPM screendump's pixel data - a
dev/test-time dependency, not something the built NTLinux image needs.
"""
import json
import os
import socket
import sys
import tempfile
import time


def qmp_call(sock, command, arguments=None):
    payload = {"execute": command}
    if arguments is not None:
        payload["arguments"] = arguments
    sock.sendall((json.dumps(payload) + "\n").encode())
    return read_json(sock)


def read_json(sock):
    buf = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf += chunk
        try:
            return json.loads(buf.decode())
        except json.JSONDecodeError:
            continue
    raise RuntimeError("QMP connection closed before a complete response")


def connect(endpoint):
    if endpoint.startswith("tcp:"):
        _, host, port = endpoint.split(":", 2)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect((host, int(port)))
    else:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(10)
        s.connect(endpoint)
    read_json(s)  # greeting
    qmp_call(s, "qmp_capabilities")
    return s


# US QWERTY character -> (needs_shift, qcode) map. Extend as needed -
# these are exactly the characters Phase 5's verification needed to type
# (paths, `sc create`/`sc start` command lines, a redirection operator).
CHAR_MAP = {
    ':': (True, 'semicolon'),
    '\\': (False, 'backslash'),
    '.': (False, 'dot'),
    ' ': (False, 'spc'),
    '_': (True, 'minus'),
    '-': (False, 'minus'),
    '%': (True, '5'),
    '=': (False, 'equal'),
    '>': (True, 'dot'),
    '<': (True, 'comma'),
}


def char_to_key(c):
    if c.isalpha():
        return (c.isupper(), c.lower())
    if c.isdigit():
        return (False, c)
    if c in CHAR_MAP:
        return CHAR_MAP[c]
    raise ValueError(f"qmp_console: no key mapping for character {c!r} "
                      f"(add one to CHAR_MAP)")


def send_keys(sock, keys):
    """Send a chord: all `keys` (QMP qcode names) pressed together, then
    released together - e.g. ["shift", "a"] for 'A'."""
    qmp_call(sock, "send-key", {"keys": [{"type": "qcode", "data": k} for k in keys]})


def type_string(sock, text, delay_s=0.03):
    for c in text:
        shift, code = char_to_key(c)
        send_keys(sock, ["shift", code] if shift else [code])
        time.sleep(delay_s)


def screendump(sock, out_path):
    r = qmp_call(sock, "screendump", {"filename": out_path})
    if "return" not in r:
        raise RuntimeError(f"screendump failed: {r}")


# QMP's "abs" mouse axis values are normalized to 0..0x7fff across the
# display, not raw pixels - only meaningful if the guest actually has an
# absolute-positioning pointing device attached (`-device usb-tablet`
# on the QEMU command line; a plain PS/2 mouse is relative-only and
# would ignore/misinterpret these). driver/vsdev/run-test.sh's QEMU
# invocation adds that device specifically so this works.
QMP_AXIS_MAX = 0x7fff


def click(sock, x_frac, y_frac):
    """Click at a point given as a 0.0-1.0 fraction of screen width/
    height (not raw pixels - keeps the caller independent of the actual
    -vga resolution). Used to force keyboard focus onto the guest
    desktop before a keyboard-only automation step (e.g. run-test.sh's
    icon-search-by-letter trick) regardless of whatever window might
    already have focus - a real, reproducible flake this project hit
    (see driver/vsdev/README.md) was traced to exactly this: an early
    blind Enter press landing on an already-focused desktop icon and
    opening an unrelated window, which a later blind keypress then had
    no way to recover from."""
    x = int(x_frac * QMP_AXIS_MAX)
    y = int(y_frac * QMP_AXIS_MAX)
    qmp_call(sock, "input-send-event", {"events": [
        {"type": "abs", "data": {"axis": "x", "value": x}},
        {"type": "abs", "data": {"axis": "y", "value": y}},
        {"type": "btn", "data": {"down": True, "button": "left"}},
        {"type": "btn", "data": {"down": False, "button": "left"}},
    ]})


def dismiss_dialog(sock, x_frac, y_frac, target_rgb, tolerance, timeout_s, interval_s,
                    click_x_frac=None, click_y_frac=None):
    """Poll until the pixel at (x_frac, y_frac) reads as `target_rgb`
    (the known plain-desktop-background color, not the dialog's) for two
    consecutive polls, interacting with the dialog on every poll where
    it doesn't - see the module docstring for why this replaces a fixed
    sleep+ret sequence. Returns True once settled, False on timeout.

    click_x_frac/click_y_frac (optional): if given, click that point
    instead of blindly sending `ret`. Real, narrow fix for a real bug
    found live: sending `ret` on every poll can land while a *different*
    control than the intended button has keyboard focus (e.g. the
    dialog's own language combobox) - Enter there cycles that control's
    selection instead of accepting the dialog, observed to actually
    change the guest's UI language mid-run. Clicking a known button
    coordinate directly can't misfire onto the wrong control the same
    way. Kept optional (defaults to the old `ret` behavior) so any other
    caller with a differently-shaped dialog keeps working unchanged.

    Real bug found only by actually running this against a full,
    unattended boot (not caught by manual step-by-step testing, which
    always happened to start polling well after the dialog had already
    rendered): background-colored is also what this exact pixel reads
    as *before* the dialog has appeared at all (nothing drawn there
    yet), which is indistinguishable from "dialog dismissed" by pixel
    color alone. Polling too early therefore saw two background matches
    immediately and returned success before the dialog ever showed up
    or got dismissed - the real symptom being a dialog interacted with
    blind by *later* automation steps instead of this one. Fixed by
    requiring the dialog color to have actually been observed at least
    once before two consecutive background reads counts as "settled" -
    genuinely distinguishes "never appeared yet" from "appeared, then
    got dismissed", which plain background-matching alone cannot.
    """
    from PIL import Image  # imported here, not at module scope, so every
    # other command in this script keeps working even in an environment
    # without Pillow installed - only this one command needs it.

    def close_enough(c1, c2):
        return all(abs(a - b) <= tolerance for a, b in zip(c1, c2))

    deadline = time.time() + timeout_s
    consecutive_matches = 0
    dialog_ever_seen = False
    with tempfile.NamedTemporaryFile(suffix=".ppm", delete=False) as tf:
        tmp_path = tf.name
    try:
        while time.time() < deadline:
            screendump(sock, tmp_path)
            im = Image.open(tmp_path)
            w, h = im.size
            pixel = im.convert("RGB").getpixel((int(x_frac * w), int(y_frac * h)))
            if close_enough(pixel, target_rgb):
                consecutive_matches += 1
                # 5, not 2: a real automated run (not caught by careful
                # manual step-by-step testing, which naturally paced
                # itself slower) still misbehaved with only 2 - plausibly
                # a brief repaint flicker between the language dialog and
                # a second dialog (e.g. a keyboard-layout confirmation)
                # this build shows next, both reading as "background" at
                # this exact point for one frame during the transition.
                # More consecutive confirmations costs a few more seconds
                # but catches that case instead of declaring victory mid-
                # transition.
                if dialog_ever_seen and consecutive_matches >= 5:
                    return True
            else:
                consecutive_matches = 0
                dialog_ever_seen = True
                if click_x_frac is not None and click_y_frac is not None:
                    click(sock, click_x_frac, click_y_frac)
                else:
                    send_keys(sock, ["ret"])
            time.sleep(interval_s)
        return False
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    sock_path, cmd = sys.argv[1], sys.argv[2]
    s = connect(sock_path)

    if cmd == "screendump":
        screendump(s, sys.argv[3])
        print(f"wrote {sys.argv[3]}")
    elif cmd == "sendkey":
        send_keys(s, sys.argv[3].split(","))
    elif cmd == "type":
        type_string(s, sys.argv[3])
    elif cmd == "click":
        click(s, float(sys.argv[3]), float(sys.argv[4]))
    elif cmd == "dismiss-dialog":
        x_frac, y_frac = float(sys.argv[3]), float(sys.argv[4])
        rgb = (int(sys.argv[5]), int(sys.argv[6]), int(sys.argv[7]))
        opts = {"--tolerance": 20, "--timeout": 180, "--interval": 5,
                "--click-x": None, "--click-y": None}
        rest = sys.argv[8:]
        for i in range(0, len(rest) - 1, 2):
            if rest[i] in ("--click-x", "--click-y"):
                opts[rest[i]] = float(rest[i + 1])
            elif rest[i] in opts:
                opts[rest[i]] = int(rest[i + 1])
        ok = dismiss_dialog(s, x_frac, y_frac, rgb, opts["--tolerance"],
                             opts["--timeout"], opts["--interval"],
                             click_x_frac=opts["--click-x"], click_y_frac=opts["--click-y"])
        if not ok:
            print(f"qmp_console.py: dismiss-dialog timed out after {opts['--timeout']}s "
                  f"still not seeing the expected background color", file=sys.stderr)
            sys.exit(1)
        print("qmp_console.py: dialog dismissed (or was never present), background confirmed")
    else:
        print(f"qmp_console.py: unknown command {cmd!r}", file=sys.stderr)
        sys.exit(1)

# Copyright (c) 2025 muzkr
#
#   https://github.com/muzkr
#
# Licensed under the MIT License (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at the root of this repository.
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#     limitations under the License.
#

"""
Serial link helpers: port opening and bounded waiting.

Every request/response state in _dump.py, _restore.py and _prog.py used to send
its request exactly once and then poll forever, so a lost packet, a radio in the
wrong mode or a cable that never answers all looked identical: a silent hang.
Retry retransmits on a timer and gives up with a diagnosis; Deadline bounds the
waits for traffic the device sends unprompted.
"""

from time import monotonic, sleep

BAUDRATE = 38400

# Defaults, overridable from the command line through configure()
SETTLE = 0.5  # seconds between opening the port and the first transmission
INTERVAL = 0.3  # seconds between retransmissions of an unanswered request
ATTEMPTS = 20  # retransmissions before giving up
WAIT = 20.0  # seconds to wait for traffic the device sends on its own


def configure(interval: float = None, attempts: int = None, wait: float = None):
    global INTERVAL, ATTEMPTS, WAIT

    if interval is not None:
        INTERVAL = interval
    if attempts is not None:
        ATTEMPTS = attempts
    if wait is not None:
        WAIT = wait


def open_port(port: str, settle: float = None, dtr: bool = None, rts: bool = None):
    """Open the radio port, optionally forcing the modem-control lines.

    pyserial raises both DTR and RTS when it opens a port, which is not what
    every programming cable wants. dtr/rts of None keep that default; True or
    False force the line. The settle delay matters because the first request
    used to go out microseconds after open, before some USB-serial bridges are
    ready to transmit.
    """

    import serial

    ser = serial.Serial(port, baudrate=BAUDRATE, timeout=0.0001, write_timeout=None)

    for name, value in (("dtr", dtr), ("rts", rts)):
        if value is None:
            continue
        try:
            setattr(ser, name, value)
        except OSError as e:
            # Ports without modem-control lines (ptys, some bridges) refuse this
            print("Cannot set {}: {}".format(name.upper(), e))

    settle = SETTLE if settle is None else settle
    if settle > 0:
        sleep(settle)

    ser.reset_input_buffer()
    return ser


def _describe_seen(seen) -> str:
    return ", ".join("0x{:04x}".format(t) for t in sorted(seen))


def _print_hints(seen, dfu_expected: bool):

    if 0x0518 in seen and not dfu_expected:
        print("The device is sending bootloader beacons: it is in DFU (flash) mode.")
        print("Dump and restore need the radio powered on in normal mode.")
        return

    if seen:
        print("Messages were received, but not the expected reply.")
        print("Message types seen: " + _describe_seen(seen))
    else:
        print("Nothing was received at all.")

    print("Check that:")
    if dfu_expected:
        print("  - the radio is in DFU (flash) mode,")
    else:
        print("  - the radio is powered on in normal mode,")
    print("  - the programming cable is fully seated in both jacks,")
    print("  - no other program holds the port (a UV Studio browser tab,")
    print("    ModemManager just after plug-in),")
    print("  - --dtr/--rts suit your cable (try --dtr 0 --rts 0).")


class Retry:
    """Bounded retransmission of a request that expects an answer."""

    def __init__(self, what: str, interval: float = None, attempts: int = None):
        self.what = what
        self.interval = INTERVAL if interval is None else interval
        self.attempts = ATTEMPTS if attempts is None else attempts
        self.seen = set()
        self.reset()

    def reset(self):
        """Make the request due immediately and restart the attempt count."""
        self.count = 0
        self.deadline = 0.0

    def due(self) -> bool:
        return monotonic() >= self.deadline

    def again(self):
        """Resend immediately, still counting towards exhaustion.

        For a device that answers but rejects: without the attempt count the
        exchange would repeat forever, which is what a bare reset() would do.
        """
        self.deadline = 0.0

    def first(self) -> bool:
        """True before the request has been sent at all."""
        return 0 == self.count

    def retried(self) -> bool:
        """True once at least one retransmission (and so a dot) has happened."""
        return self.count > 1

    def sent(self):
        self.count += 1
        self.deadline = monotonic() + self.interval

    def exhausted(self) -> bool:
        return self.count >= self.attempts

    def note(self, msg_type: int):
        """Record an unexpected message type, to explain a later give-up."""
        self.seen.add(msg_type)

    def give_up(self, dfu_expected: bool = False):
        print()
        print("No response to {} after {} attempts.".format(self.what, self.count))
        _print_hints(self.seen, dfu_expected)


class Deadline:
    """Bounded wait for traffic the device sends unprompted."""

    def __init__(self, what: str, wait: float = None):
        self.what = what
        self.wait = WAIT if wait is None else wait
        self.seen = set()
        self.expires = monotonic() + self.wait

    def extend(self):
        """Restart the clock, after progress towards what we are waiting for."""
        self.expires = monotonic() + self.wait

    def expired(self) -> bool:
        return monotonic() >= self.expires

    def note(self, msg_type: int):
        self.seen.add(msg_type)

    def give_up(self, dfu_expected: bool = False):
        print()
        print("Gave up waiting for {} after {:.0f} s.".format(self.what, self.wait))
        _print_hints(self.seen, dfu_expected)

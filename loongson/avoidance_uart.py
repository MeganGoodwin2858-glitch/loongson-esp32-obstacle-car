"""Single forward HC-SR04 avoidance client; monitor only unless both motion gates are set.

Pure protocol/controller code imports on Windows. POSIX modules load only when
opening a Linux UART. Importing this module never opens a device. See README.md.
"""
import argparse
import json
import os
from pathlib import Path
import stat
import struct
import sys
import time

from car_uart import crc, encode, parse_status

FRAME_SIZE = 20
SAMPLE_MAX_AGE = .180
STATUS_MAX_AGE = .150
CONTROL_PERIOD = .050
ARMED, ESTOP, BLE_CONNECTED, UART_READY = 1, 2, 4, 8


def parse_telemetry(data):
    if (len(data) != FRAME_SIZE or data[:2] != b'\x5b\x01' or
            crc(data[:18]) != int.from_bytes(data[18:], 'little')):
        raise ValueError('invalid telemetry frame or crc')
    result, flags, session, seq, distance, age, echo = struct.unpack_from('<BBIIHHH', data, 2)
    if result > 5 or flags & ~7:
        raise ValueError('unknown telemetry result or flags')
    return dict(result=result, flags=flags, auto_session=session, sample_seq=seq,
                distance_mm=distance, age_ms=age, echo_us=echo)


class StreamParser:
    """Bounded, incremental demultiplexer. Every rejected byte run is observable.

    Consumers must process errors in order even when valid frames follow them.
    Resynchronizing a wire stream does not authorize resuming a stopped controller.
    """
    def __init__(self, max_buffer=4096):
        if max_buffer < FRAME_SIZE:
            raise ValueError('buffer must hold at least one frame')
        self.max_buffer = max_buffer
        self.buffer = bytearray()

    def feed(self, data):
        events = []
        if len(self.buffer) + len(data) > self.max_buffer:
            events.append(dict(kind='error', error='UART buffer overflow'))
            # Keep a bounded tail; overflow is terminal to a motion controller.
            self.buffer = bytearray((self.buffer + data)[-self.max_buffer:])
        else:
            self.buffer.extend(data)
        while self.buffer:
            if self.buffer[0] not in (0x5a, 0x5b):
                count = 1
                while count < len(self.buffer) and self.buffer[count] not in (0x5a, 0x5b):
                    count += 1
                del self.buffer[:count]
                events.append(dict(kind='error', error=f'UART noise: {count} bytes discarded'))
                continue
            if len(self.buffer) < FRAME_SIZE:
                break
            packet = bytes(self.buffer[:FRAME_SIZE])
            if crc(packet[:18]) != int.from_bytes(packet[18:], 'little'):
                events.append(dict(kind='error', error='UART crc mismatch'))
                del self.buffer[0]
                continue
            kind = 'status' if packet[0] == 0x5a else 'telemetry'
            try:
                decoded = parse_status(packet) if kind == 'status' else parse_telemetry(packet)
            except ValueError as error:
                events.append(dict(kind='error', error=str(error)))
                del self.buffer[0]
                continue
            del self.buffer[:FRAME_SIZE]
            events.append(dict(kind=kind, **decoded))
        return events


def elapsed(now, since, duration):
    return now - since >= duration - 1e-9


class AvoidanceFSM:
    """Latched stop state; restart requires constructing a new controller.

    The fixed thresholds and timings are initial engineering settings, not
    physically calibrated stopping distances or demonstrated safety guarantees.
    """
    def __init__(self, turn_direction=1):
        if turn_direction not in (-1, 1):
            raise ValueError('turn direction must be +1 or -1')
        self.turn_direction = turn_direction
        self.state = 'PREPARE'
        self.reason = ''
        self.sample_seq = None
        self.sample_time = None
        self.distance = None
        self.prepared_samples = 0
        self.clear_samples = 0
        self.since = 0.0

    def abort(self, reason):
        if self.state != 'ABORTED':
            self.reason = reason
        self.state = 'ABORTED'

    def observe(self, measurement, now):
        if self.state == 'ABORTED':
            return False
        age = measurement['age_ms'] / 1000.0
        distance = measurement['distance_mm']
        expected_flags = 1 | (2 if distance <= 300 else 0) | (4 if distance <= 120 else 0)
        if measurement['result'] == 0 and (measurement['sample_seq'] == 0 or
                                          measurement['flags'] != expected_flags):
            self.abort('sensor sequence or interlock flags inconsistent with distance')
            return False
        if (measurement['result'] != 0 or not measurement['flags'] & 1 or
                measurement['flags'] & 4 or not 20 <= distance <= 4000):
            self.abort('invalid or disabled sensor / extreme-near interlock')
            return False
        if age > SAMPLE_MAX_AGE or distance <= 120:
            self.abort('stale sensor' if age > SAMPLE_MAX_AGE else 'extreme-near obstacle')
            return False
        seq = measurement['sample_seq']
        measured_at = now - age
        if self.sample_seq is not None:
            delta = (seq - self.sample_seq) & 0xffffffff
            if delta == 0:
                # A duplicate can shorten the valid lifetime, never extend it.
                self.sample_time = min(self.sample_time, measured_at)
                if distance != self.distance:
                    self.abort('duplicate sample changed distance')
                elif now - self.sample_time > SAMPLE_MAX_AGE + 1e-9:
                    self.abort('stale duplicate sensor sample')
                return False
            if delta >= 0x80000000:
                self.abort('out-of-order sensor sample')
                return False
        self.sample_seq = seq
        self.sample_time = measured_at
        self.distance = distance
        if self.state == 'PREPARE':
            self.prepared_samples += 1
            if self.prepared_samples >= 3:
                self.state = 'READY'
        elif self.state == 'CRUISE' and distance <= 300:
            self.state, self.since = 'BRAKE', now
        elif self.state == 'TURN':
            self.clear_samples = self.clear_samples + 1 if distance > 450 else 0
        return True

    def start(self, now):
        if self.state != 'READY':
            raise ValueError('three fresh samples required before start')
        self.state = 'BRAKE' if self.distance <= 300 else 'CRUISE'
        self.since = now

    def tick(self, now):
        if (self.state != 'ABORTED' and self.sample_time is not None and
                now - self.sample_time > SAMPLE_MAX_AGE + 1e-9):
            self.abort('sensor sample expired')
        if self.state == 'BRAKE' and elapsed(now, self.since, .160):
            self.state, self.since, self.clear_samples = 'TURN', now, 0
        if self.state == 'TURN':
            if elapsed(now, self.since, .800):
                self.abort('turn timeout; path not confirmed clear')
            elif elapsed(now, self.since, .200) and self.clear_samples >= 3:
                self.state, self.since = 'SETTLE', now
        if self.state == 'SETTLE' and elapsed(now, self.since, .160):
            self.state = 'BRAKE' if self.distance <= 300 else 'CRUISE'
            self.since = now
        if self.state == 'CRUISE':
            return 0, 1000, 30 if self.distance > 450 else 20
        if self.state == 'TURN':
            return self.turn_direction * 1000, 0, 25
        return 0, 0, 0


class Controller:
    """Session-bound ARM/ack/DRIVE handshake; no I/O and no implicit restart."""
    def __init__(self, enable_motion=False, wiring_confirmed=False, now=0.0, turn_direction=1):
        if enable_motion and not wiring_confirmed:
            raise ValueError('--enable-motion also requires --wiring-confirmed')
        self.motion_enabled = enable_motion and wiring_confirmed
        self.state = 'WAIT_STATUS' if self.motion_enabled else 'MONITOR'
        self.reason = ''
        self.fsm = AvoidanceFSM(turn_direction)
        self.started_at = now
        self.last_status = None
        self.last_status_at = None
        self.bound_at = None
        self.session = None
        self.status_revision = 0
        self.arm_revision = None
        self.arm_sent_at = None
        self.arm_attempted = False
        self.pending_stop = False
        self.sequence = 0
        self.last_diagnostic = ''

    def abort(self, reason, allow_stop=True):
        if self.state == 'MONITOR':
            self.last_diagnostic = reason
            return
        if self.state != 'ABORTED':
            self.reason = reason
            same = (self.last_status and self.last_status['mode'] == 1 and
                    self.last_status['auto_session'] == self.session)
            self.pending_stop = bool(allow_stop and self.arm_attempted and same)
        elif not allow_stop:
            self.pending_stop = False
        self.state = 'ABORTED'
        self.fsm.abort(reason)

    def on_event(self, event, now):
        if event['kind'] == 'error':
            self.abort(event['error'])
            return
        if event['kind'] == 'status':
            self.last_status = event
            self.last_status_at = now
            self.status_revision += 1
            # Also clear a queued STOP if another event in this read showed a
            # mode/session switch. The old session must not affect the new one.
            if self.session is not None and (event['mode'] != 1 or event['auto_session'] != self.session):
                self.abort('phone mode or Auto session changed; restart manually', allow_stop=False)
                return
            if self.state in ('MONITOR', 'ABORTED'):
                return
            flags = event['flags']
            if (event['mode'] != 1 or not flags & BLE_CONNECTED or
                    not flags & UART_READY or flags & ESTOP or event['auto_session'] == 0):
                self.abort('Auto, connected BLE, UART-ready and cleared estop are required')
                return
            if self.state == 'WAIT_STATUS':
                if flags & ARMED:
                    self.abort('session already armed; select Auto again manually', allow_stop=False)
                    return
                self.session = event['auto_session']
                self.bound_at = now
                self.state = 'PREPARE'
            elif self.state == 'PREPARE' and flags & ARMED:
                self.abort('session armed by another controller', allow_stop=False)
            elif self.state == 'ARMING' and flags & ARMED and self.status_revision > self.arm_revision:
                self.fsm.tick(now)
                if self.fsm.state != 'READY':
                    self.abort(self.fsm.reason or 'sensor no longer ready')
                else:
                    self.fsm.start(now)
                    self.state = 'ACTIVE'
            elif self.state == 'ACTIVE' and not flags & ARMED:
                self.abort('remote arming lost; manual restart required')
            return
        if event['kind'] == 'telemetry':
            if self.state in ('WAIT_STATUS', 'MONITOR', 'ABORTED'):
                return
            if event['auto_session'] != self.session:
                self.last_diagnostic = 'ignored telemetry from a different Auto session'
                return
            self.fsm.observe(event, now)
            if self.fsm.state == 'ABORTED':
                self.abort(self.fsm.reason)
            return
        self.abort('unknown parser event')

    def _command(self, op, x=0, y=0, speed=0):
        if self.sequence >= 0xffffffff:
            self.abort('command sequence exhausted; restart manually')
            return None
        self.sequence += 1
        return encode(op, speed, self.sequence, x, y, self.session)

    def tick(self, now):
        if self.state == 'MONITOR':
            return []
        if self.state != 'ABORTED':
            if self.last_status_at is None:
                if now - self.started_at > .500:
                    self.abort('initial status timeout', allow_stop=False)
            elif now - self.last_status_at > STATUS_MAX_AGE + 1e-9:
                self.abort('status link timeout')
            if self.bound_at is not None:
                if self.fsm.sample_time is None and now - self.bound_at > SAMPLE_MAX_AGE:
                    self.abort('initial telemetry timeout')
                self.fsm.tick(now)
                if self.fsm.state == 'ABORTED':
                    self.abort(self.fsm.reason)
            if self.state == 'ARMING' and now - self.arm_sent_at > .150 + 1e-9:
                self.abort('ARM acknowledgement timeout')
        if self.state == 'ABORTED':
            if self.pending_stop:
                self.pending_stop = False
                packet = self._command(2)
                return [packet] if packet else []
            return []
        if self.state == 'PREPARE' and self.fsm.state == 'READY':
            packet = self._command(3)
            self.state = 'ARMING'
            self.arm_attempted = True
            self.arm_revision = self.status_revision
            self.arm_sent_at = now
            return [packet] if packet else []
        if self.state == 'ACTIVE':
            packet = self._command(1, *self.fsm.tick(now))
            return [packet] if packet else []
        return []


def validate_port(port):
    """Reject the debug UART and non-device files before any open/configuration."""
    path = Path(port).resolve()
    if Path(port).name == 'ttyS0' or path.name == 'ttyS0':
        raise ValueError('ttyS0 is reserved for the debug console')
    try:
        info = path.stat()
    except OSError as error:
        raise ValueError(f'UART device cannot be inspected: {error}') from error
    if not stat.S_ISCHR(info.st_mode):
        raise ValueError('UART port must be a character device')
    return path


def preflight(port, sys_root='/sys', proc_root='/proc'):
    """Read metadata only; never starts/stops getty, writes sysfs or opens UART."""
    path = validate_port(port)
    name = path.name
    proc = Path(proc_root)
    report = dict(port=str(path), character_device=True, console_busy=False,
                  getty_busy=False, register_match=False, alias_match=False,
                  uart_address='0x16100800', findings=[], errors=[])
    for filename in ('consoles', 'cmdline'):
        try:
            content = (proc / filename).read_text(errors='replace')
            if filename == 'consoles':
                busy = any(line.split() and line.split()[0] == name for line in content.splitlines())
            else:
                busy = any(token.startswith('console=') and token[8:].split(',')[0] == name
                           for token in content.split())
            report['console_busy'] |= busy
        except OSError as error:
            report['errors'].append(f'cannot inspect /proc/{filename}: {error}')
    try:
        for entry in proc.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                args = (entry / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
            except FileNotFoundError:
                continue  # A process exiting during the scan is normal.
            except OSError as error:
                report['errors'].append(f'cannot inspect process {entry.name}: {error}')
                continue
            words = args.split()
            if words and 'getty' in Path(words[0]).name and any(
                    word == name or word == '/dev/' + name for word in words[1:]):
                report['getty_busy'] = True
                report['findings'].append(f'getty process: {entry.name}')
    except OSError as error:
        report['errors'].append(f'cannot enumerate processes: {error}')
    aliases = proc / 'device-tree/aliases'
    try:
        alias = (aliases / 'serial2').read_bytes().rstrip(b'\0').decode()
        report['serial2_alias'] = alias
        report['alias_match'] = '16100800' in alias.lower()
        reg_file = proc / 'device-tree' / alias.lstrip('/') / 'reg'
        if reg_file.exists():
            reg = reg_file.read_bytes()
            report['device_tree_reg_hex'] = reg.hex()
            report['alias_match'] = report['alias_match'] and b'\x16\x10\x08\x00' in reg
    except (OSError, UnicodeError) as error:
        report['findings'].append(f'serial2 alias not verified: {error}')
    device = Path(sys_root) / 'class/tty' / name / 'device'
    if device.exists():
        resolved = device.resolve()
        report['sysfs_device'] = str(resolved)
        report['register_match'] = '16100800' in str(resolved).lower()
        for parent in [resolved, *list(resolved.parents)[:5]]:
            try:
                if b'\x16\x10\x08\x00' in (parent / 'of_node/reg').read_bytes():
                    report['register_match'] = True
            except OSError:
                pass
            try:
                if '16100800' in (parent / 'uevent').read_text().lower():
                    report['register_match'] = True
            except OSError:
                pass
    else:
        report['findings'].append('sysfs tty device is unavailable; register not verified')
    report['safe_to_open'] = not (report['console_busy'] or report['getty_busy'] or report['errors'])
    report['motion_identity_verified'] = (name == 'ttyS2' and report['register_match'] and report['alias_match'])
    return report


class PosixUART:
    """Exclusive Linux UART, 115200 baud, 8N1; no third-party serial package."""
    def __init__(self, port, motion=False):
        # These imports deliberately stay out of the pure, Windows-testable code.
        import fcntl
        import selectors
        import termios
        report = preflight(port)
        if not report['safe_to_open']:
            raise ValueError('console/getty occupancy or incomplete process inspection: ' + json.dumps(report))
        if motion and not report['motion_identity_verified']:
            raise ValueError('motion requires verified ttyS2 / serial2 / 0x16100800 identity; run --preflight')
        self.fd = None
        self.saved = None
        self.selector = None
        self.termios = termios
        self.motion = motion
        path = Path(report['port'])
        previous = path.stat()
        flags = os.O_NOCTTY | os.O_NONBLOCK | getattr(os, 'O_NOFOLLOW', 0)
        flags |= os.O_RDWR if motion else os.O_RDONLY
        try:
            self.fd = os.open(path, flags)
            opened = os.fstat(self.fd)
            if not stat.S_ISCHR(opened.st_mode) or opened.st_rdev != previous.st_rdev:
                raise ValueError('UART device changed during preflight')
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
            self.saved = termios.tcgetattr(self.fd)
            settings = termios.tcgetattr(self.fd)
            settings[0] = 0
            settings[1] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[3] = 0
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIFLUSH)
            self.selector = selectors.DefaultSelector()
            self.selector.register(self.fd, selectors.EVENT_READ)
        except BaseException:
            self.close()
            raise

    def read(self, timeout):
        if not self.selector.select(max(0, timeout)):
            return None
        try:
            data = os.read(self.fd, 4096)
        except BlockingIOError:
            return None
        if not data:
            raise EOFError('UART closed')
        return data

    def write(self, packet):
        if not self.motion:
            raise RuntimeError('monitor descriptor does not permit writes')
        import selectors
        deadline = time.monotonic() + .050
        view = memoryview(packet)
        with selectors.DefaultSelector() as writable:
            writable.register(self.fd, selectors.EVENT_WRITE)
            while view:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not writable.select(remaining):
                    raise TimeoutError('UART write deadline exceeded')
                try:
                    count = os.write(self.fd, view)
                except BlockingIOError:
                    continue
                if count <= 0:
                    raise EOFError('UART write failed')
                view = view[count:]

    def close(self):
        if self.selector is not None:
            self.selector.close()
            self.selector = None
        if self.fd is not None:
            if self.saved is not None:
                try:
                    self.termios.tcsetattr(self.fd, self.termios.TCSANOW, self.saved)
                except OSError:
                    pass
            try:
                import fcntl
                fcntl.ioctl(self.fd, self.termios.TIOCNXCL)
            except OSError:
                pass
            os.close(self.fd)
            self.fd = None


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyS2')
    parser.add_argument('--preflight', action='store_true', help='inspect Linux metadata only; do not open UART')
    parser.add_argument('--enable-motion', action='store_true')
    parser.add_argument('--wiring-confirmed', action='store_true')
    parser.add_argument('--turn-direction', choices=('right', 'left'), default='right')
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.enable_motion and not args.wiring_confirmed:
        print('--enable-motion requires --wiring-confirmed', file=sys.stderr)
        return 2
    if os.name != 'posix':
        print('This UART runner requires Linux/POSIX; offline tests also run on Windows.', file=sys.stderr)
        return 2
    try:
        if args.preflight:
            report = preflight(args.port)
            print(json.dumps(report, ensure_ascii=False, indent=2))
            return 0 if report['safe_to_open'] and report['motion_identity_verified'] else 2
        uart = PosixUART(args.port, motion=args.enable_motion)
    except (OSError, ValueError) as error:
        print(f'UART preflight/open refused: {error}', file=sys.stderr)
        return 2
    controller = Controller(args.enable_motion, args.wiring_confirmed, time.monotonic(),
                            1 if args.turn_direction == 'right' else -1)
    stream = StreamParser()
    next_tick = time.monotonic()
    try:
        while True:
            data = uart.read(max(0, next_tick - time.monotonic()))
            now = time.monotonic()
            if data is not None:
                for event in stream.feed(data):
                    controller.on_event(event, now)
                    if not args.enable_motion or event['kind'] == 'error':
                        print(json.dumps(event, ensure_ascii=False), flush=True)
            if now >= next_tick or controller.state == 'ABORTED':
                for packet in controller.tick(now):
                    uart.write(packet)
                next_tick = now + CONTROL_PERIOD
            if controller.state == 'ABORTED':
                print('Stopped: ' + controller.reason + '; manual restart required', file=sys.stderr)
                return 1
    except (KeyboardInterrupt, Exception) as error:
        controller.abort(f'client stopped: {error}')
        try:
            for packet in controller.tick(time.monotonic()):
                uart.write(packet)
        except Exception:
            pass  # ESP32 session/interlock/watchdog remains the final authority.
        print('Stopped: ' + controller.reason, file=sys.stderr)
        return 130 if isinstance(error, KeyboardInterrupt) else 1
    finally:
        uart.close()


if __name__ == '__main__':
    raise SystemExit(main())

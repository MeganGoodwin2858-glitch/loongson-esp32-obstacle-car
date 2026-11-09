"""Offline safety tests. No POSIX imports, devices, network or wall-clock sleeps."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / 'loongson'))
AVAILABLE = importlib.util.find_spec('avoidance_uart') is not None
if AVAILABLE:
    import avoidance_uart as a
    import car_uart as wire


def telemetry(seq=1, distance=600, age=0, result=0, flags=None, session=7):
    if flags is None:
        flags = 1 | (2 if distance <= 300 else 0) | (4 if distance <= 120 else 0)
    data = struct.pack('<BBBBIIHHH', 0x5b, 1, result, flags, session,
                       seq, distance, age, 3500)
    return data + struct.pack('<H', wire.crc(data))


def status(flags=12, session=7, mode=1):
    data = struct.pack('<BBBBIIhhH', 0x5a, 1, mode, flags, 1, session, 0, 0, 0)
    return data + struct.pack('<H', wire.crc(data))


def sample(seq, distance=600, **kw):
    return a.parse_telemetry(telemetry(seq, distance, **kw))


class ImplementationExists(unittest.TestCase):
    def test_new_stdlib_client_exists(self):
        self.assertTrue(AVAILABLE, 'missing avoidance_uart.py: implement the tested safety client')


@unittest.skipUnless(AVAILABLE, 'implementation not yet created')
class ProtocolTests(unittest.TestCase):
    def test_telemetry_exact_layout_and_crc(self):
        raw = telemetry(seq=0x12345678, distance=250, age=30, flags=3)
        self.assertEqual(len(raw), 20)
        parsed = a.parse_telemetry(raw)
        self.assertEqual((parsed['sample_seq'], parsed['distance_mm'], parsed['age_ms'],
                          parsed['auto_session'], parsed['echo_us']),
                         (0x12345678, 250, 30, 7, 3500))

    def test_bad_crc_version_result_flags_and_length(self):
        for index, value in ((0, 0x5a), (1, 2), (2, 6), (3, 8)):
            raw = bytearray(telemetry())
            raw[index] = value
            raw[-2:] = wire.crc(raw[:18]).to_bytes(2, 'little')
            with self.assertRaises(ValueError):
                a.parse_telemetry(raw)
        raw = bytearray(telemetry())
        raw[-1] ^= 1
        for bad in (raw, b'', telemetry()[:-1]):
            with self.assertRaises(ValueError):
                a.parse_telemetry(bad)

    def test_all_fragment_boundaries_and_multiple_frames(self):
        data = status() + telemetry() + telemetry(2)
        for cut in range(len(data) + 1):
            parser = a.StreamParser()
            events = parser.feed(data[:cut]) + parser.feed(data[cut:])
            self.assertEqual([e['kind'] for e in events], ['status', 'telemetry', 'telemetry'])
        parser = a.StreamParser()
        events = []
        for byte in data:
            events += parser.feed(bytes([byte]))
        self.assertEqual(len(events), 3)

    def test_noise_and_crc_are_explicit_events_not_silent_recovery(self):
        parser = a.StreamParser()
        bad = bytearray(telemetry())
        bad[-1] ^= 1
        events = parser.feed(b'noise' + bad + status() + telemetry(2))
        self.assertIn('error', [e['kind'] for e in events])
        self.assertIn('crc', ' '.join(e.get('error', '') for e in events).lower())
        self.assertEqual([e['kind'] for e in events if e['kind'] != 'error'], ['status', 'telemetry'])

    def test_buffer_is_bounded_and_overflow_reported(self):
        parser = a.StreamParser(max_buffer=80)
        events = parser.feed(b'X' * 5000 + telemetry())
        self.assertLessEqual(len(parser.buffer), 80)
        self.assertTrue(any(e['kind'] == 'error' and 'overflow' in e['error'] for e in events))


@unittest.skipUnless(AVAILABLE, 'implementation not yet created')
class FSMTests(unittest.TestCase):
    def ready(self, distance=600):
        f = a.AvoidanceFSM()
        for seq, now in ((1, 0), (2, .08), (3, .16)):
            f.observe(sample(seq, distance), now)
        f.start(.16)
        return f

    def test_three_distinct_samples_then_explicit_start(self):
        f = a.AvoidanceFSM()
        f.observe(sample(1), 0)
        f.observe(sample(1, age=50), .05)
        f.observe(sample(2), .08)
        self.assertEqual(f.state, 'PREPARE')
        f.observe(sample(3), .16)
        self.assertEqual(f.state, 'READY')
        self.assertEqual(f.tick(.16), (0, 0, 0))
        f.start(.16)
        self.assertEqual(f.tick(.16), (0, 1000, 30))

    def test_speed_thresholds(self):
        for distance, expected in ((451, 30), (450, 20), (301, 20)):
            f = self.ready(distance)
            self.assertEqual(f.tick(.16), (0, 1000, expected))
        self.assertEqual(self.ready(300).tick(.16), (0, 0, 0))

    def test_brake_160ms_turn_minimum_200ms_and_settle_160ms(self):
        f = self.ready(300)
        f.observe(sample(4, 350), .24)
        self.assertEqual(f.tick(.319), (0, 0, 0))
        f.observe(sample(5, 500), .32)
        self.assertEqual(f.tick(.32), (1000, 0, 25))
        for seq, now in ((6, .38), (7, .44), (8, .50)):
            f.observe(sample(seq, 600), now)
        self.assertEqual(f.tick(.519), (1000, 0, 25))
        self.assertEqual(f.tick(.52), (0, 0, 0))
        self.assertEqual(f.state, 'SETTLE')
        f.observe(sample(9), .58)
        f.observe(sample(10), .66)
        self.assertEqual(f.tick(.679), (0, 0, 0))
        self.assertEqual(f.tick(.68), (0, 1000, 30))

    def test_turn_repeats_do_not_count_as_three_clear_samples(self):
        f = self.ready(300)
        f.observe(sample(4, 500), .30)
        f.tick(.32)
        f.observe(sample(5, 600), .4)
        f.observe(sample(5, 600, age=60), .46)
        f.observe(sample(5, 600, age=120), .52)
        self.assertEqual(f.tick(.52), (1000, 0, 25))

    def test_turn_800ms_timeout_and_direction(self):
        f = a.AvoidanceFSM(turn_direction=-1)
        for seq, now in ((1, 0), (2, .08), (3, .16)):
            f.observe(sample(seq, 300), now)
        f.start(.16)
        f.observe(sample(4, 300), .30)
        self.assertEqual(f.tick(.32), (-1000, 0, 25))
        for i in range(5, 15):
            now = .32 + (i - 4) * .08
            f.observe(sample(i, 350), now)
            f.tick(now)
        self.assertEqual(f.state, 'ABORTED')
        self.assertIn('turn', f.reason)

    def test_extreme_near_and_all_invalid_results_abort_permanently(self):
        for fields in (dict(distance=120), dict(distance=19), dict(distance=4001),
                       dict(result=1, distance=65535), dict(result=2, distance=65535),
                       dict(result=3, distance=65535), dict(result=4, distance=65535),
                       dict(result=5, distance=65535, age=181), dict(flags=0), dict(flags=5)):
            f = self.ready()
            f.observe(sample(4, **fields), .24)
            self.assertEqual(f.state, 'ABORTED', fields)
            f.observe(sample(5), .32)
            self.assertEqual(f.tick(.32), (0, 0, 0))

    def test_local_age_and_reported_age_both_apply(self):
        f = self.ready()
        f.observe(sample(4, age=100), .24)
        self.assertNotEqual(f.tick(.32), (0, 0, 0))
        self.assertEqual(f.tick(.321), (0, 0, 0))
        self.assertEqual(f.state, 'ABORTED')
        f = self.ready()
        f.observe(sample(4, age=181), .24)
        self.assertEqual(f.state, 'ABORTED')

    def test_duplicate_never_refreshes_local_measurement_lifetime(self):
        f = self.ready()
        for now in (.21, .26, .31):
            f.observe(sample(3), now)
        self.assertEqual(f.tick(.341), (0, 0, 0))
        self.assertEqual(f.state, 'ABORTED')

    def test_out_of_order_or_mutated_duplicate_aborts(self):
        for bad in (sample(2), sample(3, 610)):
            f = self.ready()
            f.observe(bad, .24)
            self.assertEqual(f.state, 'ABORTED')

    def test_crc_valid_but_inconsistent_sequence_and_interlock_flags_abort(self):
        initial = a.AvoidanceFSM()
        initial.observe(sample(0), 0)
        self.assertEqual(initial.state, 'ABORTED')
        invalid = [sample(0)]
        for distance, flags in ((600, 3), (250, 1), (600, 5), (250, 7), (100, 3)):
            invalid.append(sample(4, distance, flags=flags))
        for measurement in invalid:
            f = self.ready()
            f.observe(measurement, .24)
            self.assertEqual(f.state, 'ABORTED', measurement)

    def test_no_samples_cannot_prepare(self):
        f = a.AvoidanceFSM()
        self.assertEqual(f.tick(10), (0, 0, 0))
        self.assertEqual(f.state, 'PREPARE')


@unittest.skipUnless(AVAILABLE, 'implementation not yet created')
class ControllerTests(unittest.TestCase):
    def controller(self, arm=True):
        c = a.Controller(enable_motion=True, wiring_confirmed=True, now=0)
        c.on_event(dict(kind='status', **wire.parse_status(status())), 0)
        for seq, now in ((1, 0), (2, .08), (3, .16)):
            c.on_event(dict(kind='status', **wire.parse_status(status())), now)
            c.on_event(dict(kind='telemetry', **sample(seq)), now)
        commands = c.tick(.16)
        self.assertEqual([b[2] for b in commands], [3])
        if arm:
            c.on_event(dict(kind='status', **wire.parse_status(status(flags=13))), .17)
        return c

    def test_default_monitor_and_two_explicit_gates(self):
        self.assertEqual(a.Controller(now=0).tick(1), [])
        with self.assertRaises(ValueError):
            a.Controller(enable_motion=True, wiring_confirmed=False)
        c = a.Controller(wiring_confirmed=True)
        c.on_event(dict(kind='status', **wire.parse_status(status(flags=13))), 0)
        self.assertEqual(c.tick(1), [])

    def test_no_arm_before_three_samples_and_no_drive_before_ack(self):
        c = a.Controller(enable_motion=True, wiring_confirmed=True, now=0)
        c.on_event(dict(kind='status', **wire.parse_status(status())), 0)
        for seq, now in ((1, 0), (2, .08)):
            c.on_event(dict(kind='telemetry', **sample(seq)), now)
            self.assertEqual(c.tick(now), [])
        c.on_event(dict(kind='status', **wire.parse_status(status())), .16)
        c.on_event(dict(kind='telemetry', **sample(3)), .16)
        self.assertEqual([b[2] for b in c.tick(.16)], [3])
        self.assertEqual(c.tick(.17), [])
        c.on_event(dict(kind='status', **wire.parse_status(status(flags=13))), .18)
        raw = c.tick(.18)[0]
        self.assertEqual((raw[2], raw[3], struct.unpack_from('<hh', raw, 8)), (1, 30, (0, 1000)))

    def test_already_armed_session_is_not_adopted(self):
        c = a.Controller(enable_motion=True, wiring_confirmed=True, now=0)
        c.on_event(dict(kind='status', **wire.parse_status(status(flags=13))), 0)
        self.assertEqual(c.state, 'ABORTED')
        self.assertEqual(c.tick(0), [])
        self.assertIn('already armed', c.reason)

    def test_uart_bit_ble_auto_and_estop_required(self):
        for fields in (dict(flags=4), dict(flags=8), dict(flags=14), dict(mode=0), dict(mode=2)):
            c = a.Controller(enable_motion=True, wiring_confirmed=True, now=0)
            c.on_event(dict(kind='status', **wire.parse_status(status(**fields))), 0)
            self.assertEqual(c.tick(0), [])
            self.assertEqual(c.state, 'ABORTED')

    def test_crc_error_stops_own_session_and_never_restarts(self):
        c = self.controller()
        events = a.StreamParser().feed(b'garbage' + telemetry(4))
        for event in events:
            c.on_event(event, .24)
        commands = c.tick(.24)
        self.assertEqual([b[2] for b in commands], [2])
        self.assertEqual(c.state, 'ABORTED')
        self.assertEqual(c.tick(.25), [])

    def test_status_timeout_and_telemetry_timeout(self):
        c = self.controller()
        self.assertEqual([b[2] for b in c.tick(.321)], [2])
        c = self.controller()
        c.on_event(dict(kind='status', **wire.parse_status(status(flags=13))), .34)
        self.assertEqual([b[2] for b in c.tick(.341)], [2])

    def test_emergency_disconnect_and_arming_loss_stop(self):
        for flags in (15, 9, 12):
            c = self.controller()
            c.on_event(dict(kind='status', **wire.parse_status(status(flags=flags))), .20)
            self.assertEqual([b[2] for b in c.tick(.20)], [2])

    def test_mode_or_session_change_never_sends_old_stop(self):
        for fields in (dict(mode=0), dict(mode=2), dict(session=8)):
            c = self.controller()
            c.on_event(dict(kind='status', **wire.parse_status(status(flags=13, **fields))), .20)
            self.assertEqual(c.state, 'ABORTED')
            self.assertEqual(c.tick(.20), [])

    def test_pending_stop_cancelled_by_new_mode_in_same_read_batch(self):
        c = self.controller()
        c.on_event(dict(kind='error', error='crc mismatch'), .20)
        c.on_event(dict(kind='status', **wire.parse_status(status(mode=0))), .20)
        self.assertEqual(c.tick(.20), [])

    def test_foreign_session_telemetry_never_drives_or_stops_current_session(self):
        c = self.controller()
        c.on_event(dict(kind='telemetry', **sample(99, 20, session=6)), .20)
        self.assertNotEqual(c.state, 'ABORTED')
        self.assertEqual(c.fsm.sample_seq, 3)
        self.assertEqual(c.tick(.20)[0][2], 1)

    def test_zero_drive_during_brake_keeps_armed_until_termination(self):
        c = self.controller()
        c.on_event(dict(kind='telemetry', **sample(4, 300)), .24)
        command = c.tick(.24)[0]
        self.assertEqual((command[2], command[3], struct.unpack_from('<hh', command, 8)), (1, 0, (0, 0)))
        self.assertEqual(int.from_bytes(command[18:], 'little'), wire.crc(command[:18]))

    def test_no_initial_status_times_out_without_writes(self):
        c = a.Controller(enable_motion=True, wiring_confirmed=True, now=0)
        self.assertEqual(c.tick(.501), [])
        self.assertEqual(c.state, 'ABORTED')

    def test_no_telemetry_timeout_and_unacknowledged_arm_timeout(self):
        c = a.Controller(enable_motion=True, wiring_confirmed=True, now=0)
        c.on_event(dict(kind='status', **wire.parse_status(status())), 0)
        c.on_event(dict(kind='status', **wire.parse_status(status())), .18)
        self.assertEqual(c.tick(.181), [])
        self.assertEqual(c.state, 'ABORTED')
        c = self.controller(arm=False)
        c.on_event(dict(kind='status', **wire.parse_status(status())), .30)
        c.on_event(dict(kind='telemetry', **sample(4)), .30)
        self.assertEqual([b[2] for b in c.tick(.311)], [2])
        self.assertIn('acknowledgement timeout', c.reason)

    def test_old_session_samples_cannot_prepare_or_arm(self):
        c = a.Controller(enable_motion=True, wiring_confirmed=True, now=0)
        c.on_event(dict(kind='status', **wire.parse_status(status())), 0)
        for seq in range(1, 4):
            c.on_event(dict(kind='telemetry', **sample(seq, session=6)), .01 * seq)
        self.assertEqual(c.tick(.03), [])
        self.assertEqual(c.fsm.prepared_samples, 0)


@unittest.skipUnless(AVAILABLE, 'implementation not yet created')
class PreflightTests(unittest.TestCase):
    def fixture(self, root):
        root = Path(root)
        proc = root / 'proc'
        device = root / 'sys/class/tty/ttyS2/device'
        device.mkdir(parents=True)
        proc.mkdir()
        (proc / 'consoles').write_text('ttyS0 -W- (EC p a) 4:64\n')
        (proc / 'cmdline').write_text('root=/dev/mmcblk0p2 console=ttyS0,115200')
        aliases = proc / 'device-tree/aliases'
        aliases.mkdir(parents=True)
        (aliases / 'serial2').write_bytes(b'/soc/uart@16100800\0')
        node = proc / 'device-tree/soc/uart@16100800'
        node.mkdir(parents=True)
        (node / 'reg').write_bytes(bytes.fromhex('00000000161008000000000000000100'))
        (device / 'uevent').write_text('OF_FULLNAME=/soc/uart@16100800\n')
        return root / 'sys', proc

    def test_ttyS0_and_regular_file_are_rejected_without_opening(self):
        with self.assertRaises(ValueError):
            a.validate_port('/dev/ttyS0')
        with tempfile.NamedTemporaryFile(dir=ROOT / 'loongson') as file:
            with self.assertRaises(ValueError):
                a.validate_port(file.name)

    def test_default_cli_is_monitor_and_preflight_is_explicit(self):
        args = a.build_parser().parse_args([])
        self.assertEqual(args.port, '/dev/ttyS2')
        self.assertFalse(args.enable_motion)
        self.assertFalse(args.wiring_confirmed)
        self.assertTrue(a.build_parser().parse_args(['--preflight']).preflight)

    def test_readonly_preflight_verifies_sysfs_and_device_tree(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'loongson') as temp:
            sys_root, proc = self.fixture(temp)
            before = {str(p): p.read_bytes() for p in Path(temp).rglob('*') if p.is_file()}
            with patch.object(a, 'validate_port', return_value=Path('/dev/ttyS2')):
                report = a.preflight('/dev/ttyS2', sys_root, proc)
            self.assertTrue(report['safe_to_open'])
            self.assertTrue(report['motion_identity_verified'])
            after = {str(p): p.read_bytes() for p in Path(temp).rglob('*') if p.is_file()}
            self.assertEqual(before, after)

    def test_preflight_detects_console_and_getty_and_wrong_register(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'loongson') as temp:
            sys_root, proc = self.fixture(temp)
            (proc / 'cmdline').write_text('console=ttyS2,115200')
            (proc / '123').mkdir()
            (proc / '123/cmdline').write_bytes(b'\0'.join([b'/sbin/agetty', b'-L', b'ttyS2', b'115200']))
            with patch.object(a, 'validate_port', return_value=Path('/dev/ttyS2')):
                report = a.preflight('/dev/ttyS2', sys_root, proc)
            self.assertTrue(report['console_busy'])
            self.assertTrue(report['getty_busy'])
            self.assertFalse(report['safe_to_open'])
            (proc / 'cmdline').write_text('')
            (proc / '123/cmdline').write_bytes(b'')
            (sys_root / 'class/tty/ttyS2/device/uevent').write_text('OF_FULLNAME=/soc/uart@16100900')
            with patch.object(a, 'validate_port', return_value=Path('/dev/ttyS2')):
                report = a.preflight('/dev/ttyS2', sys_root, proc)
            self.assertFalse(report['motion_identity_verified'])


if __name__ == '__main__':
    unittest.main(verbosity=2)

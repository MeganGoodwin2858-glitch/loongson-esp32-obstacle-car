import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('client',Path(__file__).parents[1]/'loongson/car_uart.py')
client=importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)

class ClientTests(unittest.TestCase):
    def test_standard_crc(self):
        self.assertEqual(client.crc(b'123456789'),0x29b1)
    def test_wire_frame(self):
        data=client.encode(1,75,0x12345678,-999,700,0x87654321)
        self.assertEqual(data[:18].hex(),'a501014b7856341219fcbc02214365870000')
        self.assertEqual(len(data),20)
    def test_out_of_range(self):
        for args in [(1,101,1,0,0,1),(1,40,1,1001,0,1),(1,40,0,0,0,1)]:
            with self.assertRaises(ValueError):client.encode(*args)
    def test_status(self):
        raw=bytearray.fromhex('5a01010c0100000002000000e2ff1e0000000000')
        raw[-2:]=client.crc(raw[:-2]).to_bytes(2,'little')
        s=client.parse_status(raw)
        self.assertEqual((s['mode'],s['auto_session'],s['left'],s['right']),(1,2,-30,30))
        raw[12]^=1
        with self.assertRaises(ValueError):client.parse_status(raw)
    def test_fresh_decision(self):
        self.assertEqual(client.validate_decision(dict(x=300,y=800,speed=40,monotonic=10.0),10.1),(300,800,40))
    def test_stale_future_and_nan(self):
        for stamp in (9.8,10.1,float('nan'),float('inf'),True):
            with self.assertRaises(ValueError):client.validate_decision(dict(x=0,y=500,speed=40,monotonic=stamp),10.0)
    def test_bad_motion_fields(self):
        for x,y,speed in ((1001,0,40),(0,1001,40),(0,500,101),(False,500,40),(0,500,0.5)):
            with self.assertRaises(ValueError):client.validate_decision(dict(x=x,y=y,speed=speed,monotonic=10.0),10.0)

if __name__=='__main__':unittest.main(verbosity=2)

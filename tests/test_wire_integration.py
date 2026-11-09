"""Actual compiled C++ firmware core and Python host, with simulated Echo inputs.
No UART, BLE adapter or physical board is opened by these tests.
"""
from pathlib import Path
import os,sys,subprocess,unittest
sys.path.insert(0,str(Path(__file__).parents[1]/'loongson'))
from avoidance_uart import Controller,StreamParser

SIM=Path(os.environ.get('CAR_FIRMWARE_SIMULATOR',str(Path(__file__).parents[2]/'firmware_simulator.exe')))

@unittest.skipUnless(SIM.exists(),'compile tests/firmware_simulator.cpp and set CAR_FIRMWARE_SIMULATOR')
class WireIntegration(unittest.TestCase):
    def setUp(self):
        self.process=subprocess.Popen([str(SIM)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        self.parser=StreamParser();self.host=Controller(True,True,0)
        self.last={};self.now=0;self.instructions=[]
        self.exchange('CONNECT',deliver=False);self.exchange('MODE 1',deliver=False)
    def tearDown(self):
        self.process.stdin.close();self.process.stdout.close();self.process.wait(timeout=5)
    def exchange(self,command,deliver=True):
        self.process.stdin.write(command+'\n');self.process.stdin.flush()
        line=self.process.stdout.readline().strip();self.assertTrue(line,command)
        status,telemetry=line.split();raw=bytes.fromhex(status+telemetry)
        events=self.parser.feed(raw)
        self.assertEqual([e['kind'] for e in events],['status','telemetry'])
        self.last=events[0]
        if deliver:
            # Fragment at non-frame boundaries: this is the real wire parser.
            fragmented=StreamParser();out=[]
            for i in range(0,len(raw),7):out+=fragmented.feed(raw[i:i+7])
            for event in out:self.host.on_event(event,self.now/1000)
        return events
    def tick(self,now,pulse=5831):
        self.now=now
        self.exchange(f'RANGE {pulse} {now}')
        for frame in self.host.tick(now/1000):
            self.instructions.append(frame)
            self.exchange(f'UART {frame.hex()} {now}')
    def start(self):
        for now in (0,80,160,210):self.tick(now)
        self.assertEqual(self.host.state,'ACTIVE');self.assertGreater(self.last['left'],0)
    def test_front_obstacle_turn_then_clear(self):
        self.start();self.tick(290,1458) # 250mm: firmware must inhibit forward now.
        self.assertEqual(self.last['left'],0);self.assertEqual(self.host.fsm.state,'BRAKE')
        for now in (370,450,530):self.tick(now,1458)
        self.assertEqual((self.last['left'],self.last['right']),(25,-25))
        for now in (610,690,770,850):self.tick(now)
        self.assertEqual(self.host.fsm.state,'SETTLE');self.assertEqual(self.last['left'],0)
        self.tick(930);self.tick(1010)
        self.assertEqual(self.host.fsm.state,'CRUISE');self.assertEqual((self.last['left'],self.last['right']),(30,30))
    def test_sensor_timeout_stops_at_firmware(self):
        self.start();self.tick(290,0)
        self.assertEqual(self.last['left'],0);self.assertFalse(self.last['flags']&1)
        self.assertEqual(self.host.state,'ABORTED')
    def test_host_hang_watchdog_without_python_tick(self):
        self.start();self.now=510
        # Keep the sensor valid, but do not run any host command or host tick.
        self.exchange('RANGE 5831 510',deliver=False)
        self.assertEqual(self.last['left'],0);self.assertFalse(self.last['flags']&1)
    def test_manual_mode_change_cancels_host(self):
        self.start();self.now=220;self.exchange('MODE 0')
        self.assertEqual(self.host.state,'ABORTED');self.assertEqual(self.host.tick(.22),[])
        self.assertEqual(self.last['mode'],0);self.assertEqual(self.last['left'],0)
    def test_estop_and_disconnect(self):
        self.start();self.now=220;self.exchange('ESTOP')
        self.assertEqual(self.last['left'],0);self.assertTrue(self.last['flags']&2)
        self.assertEqual(self.host.state,'ABORTED')
        self.exchange('DISCONNECT');self.assertEqual(self.last['flags']&5,0)
    def test_bad_command_crc_disarms_real_core(self):
        self.start();frame=bytearray(self.instructions[-1]);frame[8]^=1
        self.now=220;self.exchange(f'UART {frame.hex()} 220')
        self.assertEqual(self.last['left'],0);self.assertFalse(self.last['flags']&1)
        self.assertEqual(self.host.state,'ABORTED')
    def test_old_auto_partial_frame_cannot_stop_manual_takeover(self):
        self.start();self.now=220;self.exchange('UART a5 220')
        self.exchange('MODE 0');self.exchange('ARM_BLE');self.exchange('BLE_DRIVE 0 1000 40 230')
        self.now=321;self.exchange('TICK 321')
        self.assertEqual(self.last['mode'],0);self.assertTrue(self.last['flags']&1)
        self.assertEqual((self.last['left'],self.last['right']),(40,40))

if __name__=='__main__':unittest.main(verbosity=2)

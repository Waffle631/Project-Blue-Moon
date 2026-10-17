from paths import ROOT
from pathlib import Path
import subprocess
import unittest

EXE=ROOT/'out/eventflag-test'

@unittest.skipUnless(EXE.exists(),'run bash build.sh --test first')
class EventFlagTests(unittest.TestCase):
    def run_case(self,*args,timeout=10):
        return subprocess.run([str(EXE.resolve()),*args],capture_output=True,text=True,timeout=timeout)

    def test_errors_modes_clears_and_timeouts(self):
        r=self.run_case()
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_set_wakes_in_order_and_single_rejects_second_wait(self):
        r=self.run_case('--concurrency')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_cancel_and_delete_wake_all_waiters(self):
        r=self.run_case('--cancel-delete')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_stress_loses_no_wakeups(self):
        r=self.run_case('--stress',timeout=60)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

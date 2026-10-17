from paths import ROOT
from pathlib import Path
import subprocess
import unittest

EXE = ROOT / 'out/runtime-test'


@unittest.skipUnless(EXE.exists(), 'run bash build.sh --test first')
class RuntimeTests(unittest.TestCase):
    def run_case(self, *args):
        return subprocess.run([str(EXE.resolve()), *args], capture_output=True, text=True, timeout=10)

    def test_runtime_contracts(self):
        r=self.run_case()
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        self.assertIn('PASS: callback lifecycle',r.stdout)

    def test_recursive_guard_fails_explicitly(self):
        r=self.run_case('--guard-recursion')
        self.assertEqual(r.returncode,21)
        self.assertIn('recursive/concurrent static initialization',r.stderr)

    def test_stack_corruption_is_not_ignored(self):
        r=self.run_case('--stack-failure')
        self.assertEqual(r.returncode,22)
        self.assertIn('stack protector detected corruption',r.stderr)

    def test_tls_bounds_are_checked(self):
        r=self.run_case('--bad-tls')
        self.assertEqual(r.returncode,21)
        self.assertIn('unsupported TLS module/offset',r.stderr)

    def test_rwlock_lifecycle_and_ownership(self):
        r=self.run_case('--rwlock-lifecycle')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_rwlock_multiple_readers_exclude_writer(self):
        r=self.run_case('--rwlock-concurrency')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_rwlock_timeouts_use_orbis_errors(self):
        r=self.run_case('--rwlock-timeouts')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_libc_thread_dtors_run_on_exiting_thread(self):
        r=self.run_case('--thread-dtors')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        self.assertIn('PASS: thread dtors',r.stdout)

    def test_posix_rwlock_names_share_objects(self):
        r=self.run_case('--posix-rwlocks')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_thread_name_fits_sdk_buffer(self):
        r=self.run_case('--thread-name')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_time_conversion_uses_host_zone(self):
        r=self.run_case('--time-conversion')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_file_metadata_respects_readonly_game(self):
        r=self.run_case('--file-metadata')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_decided_traps_are_named_and_unresolved(self):
        r=self.run_case('--decided-traps')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_explicit_answers_log_first_call_once(self):
        # Spec 2, block E: a defined answer, not simulated success, written once to the log.
        r=self.run_case()
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        for line in ('sceKernelGetSanitizerMallocReplaceExternal: no replacement',
                     'sceKernelGetSanitizerNewReplaceExternal: no replacement',
                     'sceKernelIsAddressSanitizerEnabled: 0', 'sceKernelIsNeoMode: 0 (base PS4)'):
            self.assertEqual(r.stdout.count('Runtime: '+line+'\n'),1,line)

    def test_rtld_atexit_underflow_fails_explicitly(self):
        r=self.run_case('--rtld-underflow')
        self.assertEqual(r.returncode,21)
        self.assertIn('RtldThreadAtexitDecrement',r.stderr)

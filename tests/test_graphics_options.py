import json
import tempfile
import unittest
from pathlib import Path

import paths  # noqa: F401  (puts scripts/ on sys.path)
import graphics_options as g


class GraphicsOptionsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.ini = Path(self.temp.name) / 'tsuki.ini'
        self.state = Path(self.temp.name) / 'state.json'

    def tearDown(self):
        self.temp.cleanup()

    def test_defaults_and_invalid_values(self):
        self.ini.write_text('stale_readback=yes\ndeferred_readback=0\n', encoding='utf-8')
        options, reverted = g.prepare(self.ini, self.state)
        self.assertFalse(reverted)
        self.assertEqual(options, {'deferred_readback': '0', 'stale_readback': '1', 'async_shaders': '1'})

    def test_old_keys_are_ignored(self):
        # The resolution and engine frame rate were patches of the previous game (Spec 8 §5): an old tsuki.ini
        # still has them, and they are neither options nor environment.
        options = g.startup_options({'render_resolution': '4K', 'engine_fps': '240', 'stale_readback': '0'})
        self.assertEqual(options, {'deferred_readback': '1', 'stale_readback': '0', 'async_shaders': '1'})
        self.assertNotIn('TSUKI_ENGINE_FPS', g.environment(options))
        for name in ('RESOLUTIONS', 'ENGINE_FPS', 'RESOLUTION_PREFIX', 'FPS_PREFIX', 'TEXTURE_FIX', 'patch_names',
                     'restrict_to_available', 'applied_engine_fps'):
            self.assertFalse(hasattr(g, name), name)

    def test_write_keys_keeps_other_lines(self):
        self.ini.write_text('# comment\nshow_fps=1\nstale_readback=1\n', encoding='utf-8')
        g.write_keys(self.ini, {'stale_readback': '0', 'vsync': '0'})
        self.assertEqual(self.ini.read_text(encoding='utf-8'), '# comment\nshow_fps=1\nstale_readback=0\nvsync=0\n')

    def test_unconfirmed_restart_is_tried_once_then_reverted(self):
        self.ini.write_text('stale_readback=0\ndeferred_readback=1\n', encoding='utf-8')
        good, _ = g.prepare(self.ini, self.state)  # a normal, confirmed launch
        g.write_keys(self.ini, {'stale_readback': '1', 'deferred_readback': '0', 'restart_unconfirmed': '1'})
        tried, reverted = g.prepare(self.ini, self.state)  # the launch of "Apply and restart"
        self.assertFalse(reverted)
        self.assertEqual((tried['stale_readback'], tried['deferred_readback']), ('1', '0'))
        back, reverted = g.prepare(self.ini, self.state)  # it never confirmed: back to the good ones
        self.assertTrue(reverted)
        self.assertEqual(back, good)
        ini = g.read_ini(self.ini)
        self.assertEqual((ini['stale_readback'], ini['restart_unconfirmed']), ('0', '0'))

    def test_confirmed_restart_becomes_the_last_good(self):
        g.prepare(self.ini, self.state)
        g.write_keys(self.ini, {'deferred_readback': '0', 'restart_unconfirmed': '1'})
        g.prepare(self.ini, self.state)
        g.write_keys(self.ini, {'restart_unconfirmed': '0'})  # the game confirmed
        g.prepare(self.ini, self.state)
        self.assertEqual(json.loads(self.state.read_text())['last_good']['deferred_readback'], '0')

    def test_environment(self):
        env = g.environment(g.startup_options({'stale_readback': '0'}))
        self.assertEqual(env, {'TSUKI_DEFERRED_READBACK': '1', 'TSUKI_STALE_READBACK': '0', 'TSUKI_ASYNC_SHADERS': '1'})

    def test_launch_has_no_temporal_upscaler_switch(self):
        # Spec 7 §10: the temporal upscaler cannot be switched on, so the launch neither reads the
        # experimental switch nor prepares its variables and FSR 4 assets (main() is one block: read the source).
        for name in ('run.py', 'run.sh'):
            text = (paths.ROOT / name).read_text(encoding='utf-8')
            for word in ('TSUKI_EXPERIMENTAL_UPSCALER', 'TSUKI_UPSCALER', 'TSUKI_FSR411_DIR', 'fsr4_optimize'):
                self.assertFalse(word in text, f'{name}: {word}')

    def test_launch_has_no_engine_rate(self):
        # Spec 8 §5: no frame rate patch, so run.py neither reports nor exports an engine rate; the present
        # thread's ceiling is fixed (tsuki_graphics.h kEngineFps).
        for name in ('run.py', 'run.sh'):
            text = (paths.ROOT / name).read_text(encoding='utf-8')
            for word in ('TSUKI_ENGINE_FPS', 'engine_fps', 'Engine frame rate'):
                self.assertFalse(word in text, f'{name}: {word}')


if __name__ == '__main__':
    unittest.main()

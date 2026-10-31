"""What the launch (run.py, run.sh) no longer prepares. main() is one block, so these read the source."""
import unittest

import paths


class LaunchSourceTest(unittest.TestCase):
    def test_launch_has_no_temporal_upscaler_switch(self):
        # Spec 7 §10: the temporal upscaler cannot be switched on, so the launch neither reads the
        # experimental switch nor prepares its variables and FSR 4 assets.
        for name in ('run.py', 'run.sh'):
            text = (paths.ROOT / name).read_text(encoding='utf-8')
            for word in ('TSUKI_EXPERIMENTAL_UPSCALER', 'TSUKI_UPSCALER', 'TSUKI_FSR411_DIR', 'fsr4_optimize'):
                self.assertFalse(word in text, f'{name}: {word}')

    def test_launch_has_no_engine_rate(self):
        # Spec 8 §5: no frame rate patch, so run.py neither reports nor exports an engine rate; the game is
        # paced by the 60 Hz vblank.
        for name in ('run.py', 'run.sh'):
            text = (paths.ROOT / name).read_text(encoding='utf-8')
            for word in ('TSUKI_ENGINE_FPS', 'engine_fps', 'Engine frame rate'):
                self.assertFalse(word in text, f'{name}: {word}')

    def test_launch_has_no_in_game_restart(self):
        # Spec 8 §5 (task 6b): "Apply and restart" existed for the GPU readback options only, which Tsukihime
        # gets nothing from; with them gone from the in-game menu, nothing restarts the game.
        text = (paths.ROOT / 'run.py').read_text(encoding='utf-8')
        for word in ('TSUKI_RESTART_COMMAND', '--after', 'TSUKI_DERIVED', 'graphics_options'):
            self.assertFalse(word in text, f'run.py: {word}')
        self.assertFalse((paths.ROOT / 'scripts' / 'graphics_options.py').exists())
        for name in ('src/probe.c', 'src/runtime.h', 'gpu/shim/tsuki_overlay.cpp'):
            self.assertNotIn('runtime_restart', (paths.ROOT / name).read_text(encoding='utf-8'), name)


if __name__ == '__main__':
    unittest.main()

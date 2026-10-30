"""The launcher has no control, default or launch variable of the temporal upscaler (Spec 7 §10);
the spatial ones went before."""
import os
import sys
import unittest
from unittest import mock

from paths import ROOT
sys.path.insert(0, str(ROOT / 'launcher'))
import gow3_lang  # noqa: E402
import gow3_launcher_win as launcher  # noqa: E402


class UpscalerSettingsTest(unittest.TestCase):
    def test_no_upscaler_defaults_to_save(self):
        for key in ('upscaler', 'preset', 'output_res', 'live_resolution', 'object_motion',
                    'fsr1', 'sharpen', 'sharpness', 'rcas', 'rcas_strength'):
            self.assertNotIn(key, launcher.INI_DEFAULTS)
        self.assertNotIn('object_motion', launcher.INI_FLAGS)
        self.assertNotIn('experimental_upscaler', launcher.APP_DEFAULTS)

    def test_launch_sends_no_upscaler_variable(self):
        # An old settings.json can still hold the switch: it is ignored.
        with mock.patch.dict(os.environ, {}, clear=False):
            for name in ('TSUKI_EXPERIMENTAL_UPSCALER', 'TSUKI_FSR4_DIR'):
                os.environ.pop(name, None)
            env = launcher.game_environment({**launcher.APP_DEFAULTS, 'experimental_upscaler': True})
        self.assertNotIn('TSUKI_EXPERIMENTAL_UPSCALER', env)
        self.assertNotIn('TSUKI_FSR4_DIR', env)

    def test_no_upscaler_texts_left_to_translate(self):
        for text in ('Upscaler', 'Upscaling', 'Quality preset', 'Download FSR 4 assets', 'FSR 3.1 (every GPU)'):
            self.assertNotIn(text, gow3_lang.KEYS)
        self.assertNotIn('Enable the upscaler (experimental)', gow3_lang.EXTRA['pt'])
        for language in gow3_lang.TRANSLATIONS:
            gow3_lang.table(language)  # every table still matches KEYS


if __name__ == '__main__':
    unittest.main()

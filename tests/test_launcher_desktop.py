"""The menu entry that gives the launcher and the game window their icon on Linux."""
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from paths import ROOT
sys.path.insert(0, str(ROOT / 'launcher'))
import tsuki_launcher_win as launcher  # noqa: E402


class DesktopIntegrationTest(unittest.TestCase):
    def setUp(self):
        self.home = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.home, True)
        self.icon = self.home / 'source.png'
        self.icon.write_bytes(b'png one')
        self.data = self.home / 'data'
        self.entry = self.home / '.local' / 'share' / 'applications' / 'tsuki.desktop'

    def integrate(self, enabled=True, appimage='/games/tsuki.AppImage'):
        return launcher.integrate_desktop(enabled, appimage, self.icon, self.data, home=self.home)

    def test_entry_names_the_appimage_the_icon_and_the_window_class(self):
        self.assertEqual(self.integrate(), self.entry)
        text = self.entry.read_text()
        self.assertIn('Exec="/games/tsuki.AppImage"', text)
        self.assertIn(f'Icon={self.data / "tsuki.png"}', text)
        self.assertIn('StartupWMClass=tsuki', text)
        self.assertEqual((self.data / 'tsuki.png').read_bytes(), b'png one')

    def test_a_moved_appimage_or_a_new_icon_updates_the_entry(self):
        self.integrate()
        self.icon.write_bytes(b'png two')
        self.integrate(appimage='/elsewhere/tsuki.AppImage')
        self.assertIn('Exec="/elsewhere/tsuki.AppImage"', self.entry.read_text())
        self.assertEqual((self.data / 'tsuki.png').read_bytes(), b'png two')

    def test_turning_it_off_removes_only_our_entry(self):
        self.integrate()
        self.integrate(enabled=False)
        self.assertFalse(self.entry.exists())
        self.entry.write_text('[Desktop Entry]\nName=made by hand\n')
        self.integrate(enabled=False)
        self.assertTrue(self.entry.exists())

    def test_an_entry_made_by_hand_is_left_alone(self):
        self.entry.parent.mkdir(parents=True)
        self.entry.write_text('[Desktop Entry]\nName=made by hand\n')
        self.assertIsNone(self.integrate())
        self.assertEqual(self.entry.read_text(), '[Desktop Entry]\nName=made by hand\n')

    def test_without_an_appimage_nothing_is_written(self):
        self.assertIsNone(self.integrate(appimage=None))
        self.assertFalse(self.entry.exists())


class PublicNameTest(unittest.TestCase):
    # Spec 8 §5: the port presents itself as "Project Blue Moon", not under the game's trademark; the
    # texts that ask for the dump still name the game.
    def test_the_launcher_and_its_entries_say_project_blue_moon(self):
        self.assertEqual(launcher.APP_NAME, 'Project Blue Moon')
        self.assertIn('\nName=Project Blue Moon\n', launcher.desktop_entry('x', 'y'))
        packaged = (ROOT / 'packaging' / 'linux' / 'tsuki.desktop').read_text(encoding='utf-8')
        self.assertIn('\nName=Project Blue Moon\n', packaged)
        self.assertIn('Comment=PC port of TSUKIHIME -A piece of blue glass moon- (bring your own game dump)', packaged)

    def test_choosing_the_dump_keeps_the_port_name_on_the_window(self):
        # The cover under it is the dump's, with the game's logo, serial and version.
        from unittest import mock
        app = launcher.Launcher.__new__(launcher.Launcher)
        for name in ('root', 'side_title', 'meta_dir', 'draw_banner', 'set_icon', 'refresh_status'):
            setattr(app, name, mock.Mock())
        with mock.patch.object(launcher, 'game_info', return_value=('TSUKIHIME -A piece of blue glass moon-', '01.00')):
            app.game_changed()
        app.root.title.assert_not_called()
        app.side_title.configure.assert_not_called()
        app.draw_banner.assert_called_once()

    def test_play_starts_the_renamed_launcher(self):
        text = (ROOT / 'packaging' / 'windows' / 'play.c').read_text(encoding='utf-8')
        self.assertIn('Project Blue Moon.exe', text)
        self.assertNotIn('Tsukihime', text)

    def test_the_windows_package_uses_the_public_names(self):
        script = (ROOT / 'packaging' / 'windows' / 'package.sh').read_text(encoding='utf-8')
        for name in ('--name "Project Blue Moon"', 'Play Project Blue Moon.exe', 'dist/project-blue-moon-windows.zip',
                     'out/pyi-dist/Project Blue Moon/'):
            self.assertIn(name, script)
        self.assertNotIn('Tsukihime', script)
        self.assertNotIn('tsuki-windows', script)
        # patches/ is gone since Spec 1a; copying it stopped the package.
        self.assertNotIn('patches "$dest/"', script)

    def test_the_shortcuts_are_named_after_the_port(self):
        text = (ROOT / 'launcher' / 'tsuki_launcher_win.py').read_text(encoding='utf-8')
        for old in ('Tsukihime.lnk', 'Tsukihime.desktop', 'Tsukihime.exe'):
            self.assertNotIn(old, text)


if __name__ == '__main__':
    unittest.main()

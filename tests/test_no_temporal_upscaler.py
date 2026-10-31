"""The temporal upscaler (FSR 3.1, FSR 4, DLSS, TAA) and the scene scaling that only it drove are
deleted, not switched off (Spec 7 §12): no submodule, patches, bridge, renderer files or build steps."""
import unittest

from paths import ROOT

RENDERER = 'gpu/shadps4/video_core/renderer_vulkan'
SHADERS = 'gpu/shadps4/video_core/host_shaders'
DELETED = [
    'gpu/third_party/fsr-vulkan', 'gpu/patches/fsr-vulkan', 'gpu/dlss_bridge', 'packaging/windows/build_dlss.sh',
    *(f'{RENDERER}/{name}' for name in (
        'vk_temporal_upscaler.cpp', 'vk_temporal_upscaler.h', 'vk_fsr4.cpp', 'vk_fsr4.h', 'vk_dlss.cpp', 'vk_dlss.h',
        'vk_camera_motion.cpp', 'vk_camera_motion.h', 'vk_object_motion.cpp', 'vk_object_motion.h',
        'motion_history.h', 'ui_composition.h', 'vk_scene_resolution.cpp', 'vk_scene_resolution.h',
        'scene_resolution.h')),
    *(f'{SHADERS}/{name}' for name in (
        'taa.comp', 'taa_sharpen.comp', 'camera_motion.comp', 'upscale_merge.comp', 'upscale_reactive.comp',
        'depth_resample.frag')),
]


class NoTemporalUpscalerTest(unittest.TestCase):
    def test_files_are_gone(self):
        for path in DELETED:
            self.assertFalse((ROOT / path).exists(), path)

    def test_build_does_not_name_it(self):
        words = {'.gitmodules': ('fsr-vulkan',), 'build.sh': ('fsr-vulkan', 'gpu/patches'),
                 'gpu/CMakeLists.txt': ('fsr-vulkan', 'ffx-vulkan', 'taa', 'camera_motion', 'depth_resample'),
                 'packaging/windows/package.sh': ('dlss', 'fsr4'), '.gitignore': ('fsr4',)}
        for name, banned in words.items():
            path = ROOT / name
            text = path.read_text(encoding='utf-8').lower() if path.exists() else ''
            for word in banned:
                self.assertNotIn(word, text, f'{name}: {word}')


if __name__ == '__main__':
    unittest.main()

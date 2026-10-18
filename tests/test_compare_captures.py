from paths import ROOT
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT=ROOT/'tools/compare_captures.sh'

def make_png(path,source):
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i',source,
                    '-frames:v','1','-pix_fmt','rgb24',str(path)],check=True)

# lavfi sources are generated in YUV, which rounds away one-step RGB changes; raw RGB keeps them exact.
def make_rgb_png(path,w,h,pixels):
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24',
                    '-s',f'{w}x{h}','-i','-','-frames:v','1',str(path)],input=bytes(pixels),check=True)

# Oracle for Specs 1b/6 (docs/specs/2026-10-14-hito-primer-fotograma.md, H3): exit 0 only for
# bit-identical pixels, so even a one-pixel change must count as different.
@unittest.skipUnless(shutil.which('ffmpeg') and shutil.which('ffprobe'),'ffmpeg not installed')
class CompareCapturesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(); d=Path(cls.tmp.name)
        cls.pattern=d/'pattern.png'; make_png(cls.pattern,'testsrc=size=64x48:rate=1')
        cls.pattern_copy=d/'pattern_copy.png'; make_png(cls.pattern_copy,'testsrc=size=64x48:rate=1')
        cls.red=d/'red.png'; make_png(cls.red,'color=c=red:size=64x48')
        cls.black=d/'black.png'; make_png(cls.black,'color=c=black:size=64x48')
        gray=[128]*(64*48*3); cls.gray=d/'gray.png'; make_rgb_png(cls.gray,64,48,gray)
        gray[(10*64+10)*3+2]+=1; cls.gray_lsb=d/'gray_lsb.png'; make_rgb_png(cls.gray_lsb,64,48,gray)
        cls.small=d/'small.png'; make_png(cls.small,'color=c=black:size=32x24')

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def compare(self,*args):
        return subprocess.run(['bash',str(SCRIPT),*map(str,args)],capture_output=True,text=True,timeout=60)

    def test_same_file_and_identical_copy_are_identical(self):
        for b in (self.pattern,self.pattern_copy):
            r=self.compare(self.pattern,b)
            self.assertEqual((r.returncode,r.stdout),(0,'idéntico\n'),r.stderr)

    def test_different_scenes_report_psnr(self):
        r=self.compare(self.pattern,self.red)
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertRegex(r.stdout,r'^diferente: PSNR=\d+(\.\d+)?\n$')

    def test_one_step_in_one_channel_of_one_pixel_is_different(self):
        self.assertNotEqual(self.gray.read_bytes(),self.gray_lsb.read_bytes())
        self.assertEqual(self.compare(self.gray,self.gray).returncode,0)
        r=self.compare(self.gray,self.gray_lsb)
        self.assertEqual(r.returncode,1,r.stdout+r.stderr)
        self.assertTrue(r.stdout.startswith('diferente: PSNR='),r.stdout)

    def test_usage_errors_exit_2(self):
        for args in ((),(self.pattern,),(self.pattern,self.red,self.black),
                     (self.pattern,Path(self.tmp.name)/'missing.png'),(self.black,self.small)):
            r=self.compare(*args)
            self.assertEqual(r.returncode,2,(args,r.stdout,r.stderr))
            self.assertEqual(r.stdout,'',args); self.assertTrue(r.stderr,args)

if __name__=='__main__': unittest.main()

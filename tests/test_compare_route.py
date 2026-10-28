from paths import ROOT
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT=ROOT/'tools/compare_route.sh'
W,H=64,48

# Raw RGB, not lavfi: lavfi sources are YUV and would round away the one-step changes below.
def make_rgb_png(path,pixels):
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','rawvideo','-pix_fmt','rgb24',
                    '-s',f'{W}x{H}','-i','-','-frames:v','1',str(path)],input=bytes(pixels),check=True)

def gray(changes=()):
    """A gray image with each (x, y, step) of `changes` added to one channel of that pixel."""
    pixels=[128]*(W*H*3)
    for x,y,step in changes: pixels[(y*W+x)*3+1]+=step
    return pixels

# Spec 6 block A (docs/specs/2026-10-14-spec-6-2d-raton-saves.md, A1): frames.txt says how each frame
# of the recorded route is compared; a mask hides only its boxes, and nothing broken passes as identical.
@unittest.skipUnless(shutil.which('ffmpeg') and shutil.which('ffprobe'),'ffmpeg not installed')
class CompareRouteTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); d=Path(self.tmp.name)
        self.a=d/'a'; self.b=d/'b'; self.a.mkdir(); self.b.mkdir()
        self.frames=d/'frames.txt'

    def tearDown(self): self.tmp.cleanup()

    def capture(self,folder,frame,changes=()):
        make_rgb_png(folder/f'frame_{frame:06d}_{W}x{H}.png',gray(changes))

    def compare(self,text):
        self.frames.write_text(text,encoding='utf-8')
        return subprocess.run(['bash',str(SCRIPT),str(self.a),str(self.b),str(self.frames)],
                              capture_output=True,text=True,timeout=120)

    def test_exact_identical_and_different(self):
        self.capture(self.a,120); self.capture(self.b,120)
        self.capture(self.a,300); self.capture(self.b,300,[(5,5,1)]) # one step in one channel of one pixel
        r=self.compare('120 exact # autosave notice\n\n# a comment line\n')
        self.assertEqual((r.returncode,r.stdout),(0,'120: idéntico\n'),r.stderr)
        r=self.compare('120 exact\n300 exact\n')
        self.assertEqual(r.returncode,1,r.stderr)
        self.assertRegex(r.stdout,r'^120: idéntico\n300: diferente: PSNR=\d+(\.\d+)?\n$')

    def test_mask_hides_difference_inside_box(self):
        self.capture(self.a,120); self.capture(self.b,120,[(20,10,100),(23,13,100)])
        r=self.compare('120 mask 0 0 4 4 20 10 4 4\n') # the second box covers both changes
        self.assertEqual((r.returncode,r.stdout),(0,'120: idéntico\n'),r.stderr)
        self.capture(self.b,120,[(20,10,100),(24,13,1)]) # one step just right of the box
        r=self.compare('120 mask 20 10 4 4\n')
        self.assertEqual(r.returncode,1,r.stdout+r.stderr)
        self.assertTrue(r.stdout.startswith('120: diferente: PSNR='),r.stdout)

    def test_eye_frames_are_not_compared(self):
        self.capture(self.a,900); self.capture(self.b,900,[(1,1,100)])
        self.capture(self.a,1320); self.capture(self.b,1320)
        r=self.compare('900 eye # TM_CI.mp4\n1320 exact\n')
        self.assertEqual((r.returncode,r.stdout),(0,'900: a ojo\n1320: idéntico\n'),r.stderr)

    def test_mask_outside_image_is_an_error(self):
        self.capture(self.a,120); self.capture(self.b,120,[(1,1,100)])
        for box in (f'{W-3} 0 4 4',f'0 {H-3} 4 4','0 0 0 4','0 0 4 0',f'{W} 0 1 1'):
            r=self.compare(f'120 mask {box}\n')
            self.assertEqual(r.returncode,2,(box,r.stdout,r.stderr))
            self.assertNotIn('idéntico',r.stdout,box); self.assertTrue(r.stderr,box)

    def test_missing_capture_is_an_error(self):
        self.capture(self.a,120); self.capture(self.b,120)
        self.capture(self.a,300) # 300 is not in b
        for text in ('120 exact\n300 exact\n','300 eye\n','420 mask 0 0 4 4\n'):
            r=self.compare(text)
            self.assertEqual(r.returncode,2,(text,r.stdout,r.stderr)); self.assertTrue(r.stderr,text)
        self.frames.unlink()
        r=subprocess.run(['bash',str(SCRIPT),str(self.a),str(self.b),str(self.frames)],capture_output=True,text=True,timeout=60)
        self.assertEqual(r.returncode,2,r.stderr)

    def test_malformed_line_is_an_error(self):
        self.capture(self.a,120); self.capture(self.b,120)
        for text in ('120\n','120 exacto\n','x exact\n','120 exact 1\n','120 mask\n','120 mask 0 0 4\n',
                     '120 mask 0 0 4 4 1\n','120 mask -1 0 4 4\n','120 eye 1\n','-120 exact\n','120 exact\n120 eye\n'):
            r=self.compare(text)
            self.assertEqual(r.returncode,2,(text,r.stdout,r.stderr))
            self.assertEqual(r.stdout,'',text); self.assertTrue(r.stderr,text)
        for args in ((),(self.a,self.b)):
            r=subprocess.run(['bash',str(SCRIPT),*map(str,args)],capture_output=True,text=True,timeout=60)
            self.assertEqual(r.returncode,2,args)

if __name__=='__main__': unittest.main()

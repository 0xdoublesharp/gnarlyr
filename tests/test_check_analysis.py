import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_analysis import check


class AnalysisVerificationTests(unittest.TestCase):
    def setUp(self):
        self.mapping={'elf_sha256':'ab'*32,'regions':[{'name':'m000_code','address':0x3050000,
                      'size':64,'file_offset':512,'sha256':'cd'*32}]}
        self.analysis={'import_sha256':'ab'*32,'language':'ARM:LE:32:Cortex',
                       'memory':[{'name':'.m000_code','start':'03050000','size':64,
                                  'initialized':True,'sha256':'cd'*32}],
                       'functions':[{'thumb_mode':1,'decompile_completed':True,'instructions':3},
                                    {'thumb_mode':1,'decompile_completed':True,'instructions':5}]}

    def test_expected_mapping_passes(self):
        self.assertEqual(check(self.mapping,self.analysis),(1,2))

    def test_wrong_loaded_bytes_refused(self):
        self.analysis['memory'][0]['sha256']='ef'*32
        with self.assertRaisesRegex(ValueError,'memory differs'): check(self.mapping,self.analysis)

    def test_arm_mode_instead_of_thumb_refused(self):
        self.analysis['functions'][0]['thumb_mode']=0
        with self.assertRaisesRegex(ValueError,'Thumb'): check(self.mapping,self.analysis)

    def test_wrong_import_identity_refused(self):
        self.analysis['import_sha256']='00'*32
        with self.assertRaisesRegex(ValueError,'digest differs'): check(self.mapping,self.analysis)

if __name__=='__main__': unittest.main()

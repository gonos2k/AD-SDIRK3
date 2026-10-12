"""Focused regression for the RK3 refinement analyzer's labelled step contract."""
from __future__ import annotations
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

TOOLS=Path(__file__).resolve().parent
sys.path.insert(0,str(TOOLS))
import analyze_wrf_reference_refinement as analysis

INPUT='fixture-input-sha'
SHAPES={'U':(16,16,16),'V':(16,17,16),'W':(16,16,16),'PH':(16,16,16),'T':(16,16,16),'MU':(16,16)}
RAW_SHAPES={'U':(16,16,17),'V':(16,17,16),'W':(17,16,16),'PH':(17,16,16),'PHB':(17,16,16),'T':(16,16,16),'MU':(16,16),'MUB':(16,16)}

class PlanContractTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.plan_path=self.root/'plan.json'
        self.contract_path=self.root/'contract.json'
        self.out_path=self.root/'analysis.json'
        rows=[]
        for label,h in zip(analysis.REQUIRED_LABELS,(3.75,1.875,0.9375)):
            rows.append({'label':label,'path':label,'h_seconds':h,'method':'RK3'})
        self.plan={'runs':rows}
        self.contract={'contract_id':'dry-em-b-wave-16cubed-240s-fixture','case':{'input_sha256':INPUT}}
        self.write_inputs()

    def tearDown(self): self.temp.cleanup()

    def write_inputs(self):
        self.plan_path.write_text(json.dumps(self.plan))
        self.contract_path.write_text(json.dumps(self.contract))

    def fake_loader(self,row,_plan_dir):
        value={'coarse_h3p75':0.0,'middle_h1p875':1.0,'fine_h0p9375':3.0}[row['label']]
        frames={}
        for ti,t in enumerate(analysis.TIMES):
            base = 1.0 if ti == 0 else value + ti * 0.1
            frames[t]={name:np.full(shape,base,dtype=np.float64) for name,shape in SHAPES.items()}
        return {'label':row['label'],'path':row['path'],'h_seconds':float(row['h_seconds']),'method':row['method'],
                'wall_seconds':None,'wall_receipt_source':None,'peak_rss_bytes':None,'source_revision':None,
                'source_sha256':None,'build_receipt':None,'input_sha256':{'path':'fixture','sha256':INPUT},
                'namelist_sha256':{'path':'fixture','sha256':'fixture-nml'},'executable_sha256':None,
                'wrfout_files':[],'grid':{'nx':16,'ny':16,'nz':16,'dx_m':100000.0,'dy_m':100000.0},
                'physical_minima_over_all_frames':{},'timestamps':analysis.TIMES,'frames':frames,'step_logs':{}}

    def test_valid_plan_writes_24_metrics_and_48_delta_arrays(self):
        raw={k:np.ones(shape,dtype=np.float64) for k,shape in RAW_SHAPES.items()}
        with patch.object(analysis.benchmark,'_load_run',side_effect=self.fake_loader), patch.object(analysis,'raw_initial',return_value=raw):
            result=analysis.analyze(self.plan_path,self.contract_path,self.out_path)
        self.assertEqual(len(result['metrics']),24)
        self.assertTrue(result['exact_raw_initial_state']['all_exact'])
        self.assertTrue(result['exact_times']['all_three_match_exactly'])
        for metric in result['metrics']:
            self.assertAlmostEqual(metric['successive_difference_shape']['d2_to_d1_l2_norm_ratio'],2.0,places=14)
        with np.load(self.root/'analysis_deltas.npz') as deltas:
            self.assertEqual(len(deltas.files),48)
            self.assertTrue(np.all(deltas['d1_000100_U']==1.0))
            self.assertTrue(np.all(deltas['d2_000100_U']==2.0))

    def test_swapped_resolution_is_rejected_with_label(self):
        self.plan['runs'][1]['h_seconds']=3.75
        self.write_inputs()
        with patch.object(analysis.benchmark,'_load_run',side_effect=self.fake_loader):
            with self.assertRaisesRegex(ValueError,r'middle_h1p875: expected h_seconds=1.875, got 3.75'):
                analysis.analyze(self.plan_path,self.contract_path,self.out_path)

    def test_wrong_method_is_rejected_with_label(self):
        self.plan['runs'][2]['method']='ARK3'
        self.write_inputs()
        with patch.object(analysis.benchmark,'_load_run',side_effect=self.fake_loader):
            with self.assertRaisesRegex(ValueError,r"fine_h0p9375: expected method='RK3', got 'ARK3'"):
                analysis.analyze(self.plan_path,self.contract_path,self.out_path)

if __name__=='__main__': unittest.main(verbosity=2)

#!/usr/bin/env python3

import tempfile
import subprocess
import argparse
import os
import sys
from datasets import load_dataset

import input_gen_module

if __name__ == '__main__':
    parser = argparse.ArgumentParser('MassInputGen')

    parser.add_argument('--dataset', default='llvm-ml/ComPile')
    parser.add_argument('--outdir', required=True)
    parser.add_argument('--start', type=int, default=0)
    parser.add_argument('--end', type=int, default=10)

    input_gen_module.add_option_args(parser)

    args = parser.parse_args()

    ds = load_dataset(args.dataset, split='train', streaming=True)

    os.makedirs(args.outdir, exist_ok=True)

    print('Will input gen for dataset {} in {}'.format(args.dataset, args.outdir))

    global_outdir = args.outdir
    igm_args = vars(args)
    stats = input_gen_module.InputGenModule().get_empty_statistics()

    # TODO maybe use ray to distribute?
    ds = ds.skip(args.start)
    for (i, module) in zip(range(args.start, args.end), ds):
        igm_args['outdir'] = os.path.join(global_outdir, str(i))
        with tempfile.NamedTemporaryFile(dir='/tmp/') as module_file:
            module_file.write(module['content'])
            module_file.flush()
            igm_args['input_module'] = module_file.name

            igm = input_gen_module.InputGenModule(**igm_args)
            igm.generate_inputs()
            igm.run_all_inputs()

            igm.update_statistics(stats)

    print('Statistics: {}'.format(stats))

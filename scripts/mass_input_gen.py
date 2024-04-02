#!/usr/bin/env python3

import tempfile
import subprocess
import argparse
import os
import sys
from datasets import load_dataset

import input_gen_module

def precompile_runtimes(args):
    print('Precompiling runtimes... ', end='', flush=True)
    args.input_gen_runtime = precompile_runtime(args.input_gen_runtime)
    args.input_run_runtime = precompile_runtime(args.input_run_runtime)
    print('Done.')

def precompile_runtime(fname):
    if (fname.endswith('.c') or
        fname.endswith('.cpp') or
        fname.endswith('.cxx')):

        obj = fname + '.o'
        subprocess.run(
            'clang++ -std=c++17 -c -O3 -g'.split(' ') +
            [fname, '-o', obj],
            check=True)
        return obj
    else:
        return fname

if __name__ == '__main__':
    parser = argparse.ArgumentParser('MassInputGen')

    parser.add_argument('--dataset', default='llvm-ml/ComPile')
    parser.add_argument('--outdir', required=True)

    parser.add_argument('--start', type=int, default=0)
    parser.add_argument('--end', type=int, default=10)

    parser.add_argument('--precompile-rts', action='store_true')
    parser.add_argument('--no-precompile-rts',
                        dest='precompile_rts', action='store_false')
    parser.set_defaults(precompile_rts=True)

    input_gen_module.add_option_args(parser)

    args = parser.parse_args()

    if args.precompile_rts:
        precompile_runtimes(args)

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
        os.makedirs(igm_args['outdir'], exist_ok=True)
        with open(igm_args['outdir'] +"/mod.bc", 'wb') as module_file:
        #with tempfile.NamedTemporaryFile(dir='/tmp/', prefix='input-gen-input-', suffix='.bc') as module_file:
            module_file.write(module['content'])
            module_file.flush()
            igm_args['input_module'] = module_file.name

            igm = input_gen_module.InputGenModule(**igm_args)
            igm.generate_inputs()
            igm.run_all_inputs()

            igm.update_statistics(stats)

    print('Statistics: {}'.format(stats))

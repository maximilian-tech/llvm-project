#!/usr/bin/env python3

import subprocess
import argparse
import os
import sys

if __name__ == '__main__':
    parser = argparse.ArgumentParser('InputGen')

    parser.add_argument('--outdir', required=True)
    parser.add_argument('--inputmodule', required=True)
    parser.add_argument('--num', type=int, default=1)
    parser.add_argument('--rt', default='../rt.cpp')

    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    print('Generating inputgen executables for', args.inputmodule, 'in', args.outdir)

    subprocess.run([
        'input-gen',
        '--input-gen-runtime', args.rt,
        '--output-dir', args.outdir,
        args.inputmodule,
        '--compile-input-gen-executable',
    ], check=True)

    with open(os.path.join(args.outdir, 'available_functions')) as available_functions_file:
        for func in available_functions_file.read().splitlines():
            # TODO timeouts here?
            inputgen_exe = os.path.join(args.outdir, 'input-gen.' + func + '.executable')
            func_inputs_dir = os.path.join(args.outdir, 'input-gen.' + func + '.inputs')
            os.makedirs(func_inputs_dir, exist_ok=True)
            print('Generating inputs for function @{}'.format(func))
            subprocess.run([
                inputgen_exe,
                func_inputs_dir,
                '0', str(args.num)
            ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

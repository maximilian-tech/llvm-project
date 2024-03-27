#!/usr/bin/env python3

import subprocess
import argparse
import os
import sys

if __file__ == '__main__':
    parser = argparse.ArgumentParser('InputGen')

    parser.add_argument('--outputdir', required=True)
    parser.add_argument('--inputmodule', required=True)
    parser.add_argument('--num', type=int, default=1)
    parser.add_argument('--rt', default='../rt.cpp')

    args = parser.parse_args()

    os.makedirs(args.targetdir)

    subprocess.run([
        'input-gen',
        '--input-gen-runtime', args.rt,
        '--output-dir', args.outputdir,
        args.inputmodule,
        '--compile-input-gen-executable',
    ], check=True)

    with open(os.path.join(args.outputdir, 'available_functions')) as available_functions_file:
        for func in available_functions_file.readlines():
            # TODO timeouts here?
            inputgen_exe = os.path.join(args.outputdir, 'input-gen.' + func + '.executable')
            func_inputs_dir = os.path.join(args.targetdir, 'input-gen.' + func + '.inputs')
            os.makedirs(func_inputs_dir)
            subprocess.run([
                inputgen_exe,
                func_inputs_dir,
                '0', str(args.num)
            ], check=True)

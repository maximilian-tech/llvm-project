#!/usr/bin/env python3

import json
import subprocess
import argparse
import os
import sys
import multiprocessing
import functools
from datasets import load_dataset
import jug

import mass_input_gen as mig

def handle_single_module_i(i):
    ds_i = ds.skip(i)
    module = list(ds_i.take(1))[0]
    return mig.handle_single_module((i, module), args)

parser = argparse.ArgumentParser('MassInputGenJug')
mig.add_option_args(parser)
args = parser.parse_args()

if not args.get_jug_results:

    if args.precompile_rts:
        if args.verbose:
            print('Precompiling runtimes...')
        igr = jug.Task(mig.precompile_runtime, args.input_gen_runtime, args.g, args.verbose)
        irr = jug.Task(mig.precompile_runtime, args.input_run_runtime, args.g, args.verbose)
        jug.barrier()
        if args.verbose:
            print('Done:')
        args.input_gen_runtime = jug.task.value(igr)
        args.input_run_runtime = jug.task.value(irr)
        if args.verbose:
            print(args.input_gen_runtime)
            print(args.input_run_runtime)

    if args.verbose:
        print('Will input gen for dataset {} in {}'.format(args.dataset, args.outdir))
    ds = load_dataset(args.dataset, split='train', streaming=True)
    os.makedirs(args.outdir, exist_ok=True)

results = []
for i in range(args.start, args.end):
    results.append(jug.Task(handle_single_module_i, i))
results = list(zip(range(args.start, args.end), results))

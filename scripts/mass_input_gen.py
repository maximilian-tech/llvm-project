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

import input_gen_module

def precompile_runtimes(args):
    print('Precompiling runtimes...')
    args.input_gen_runtime = precompile_runtime(args.input_gen_runtime, args.g, args.verbose)
    args.input_run_runtime = precompile_runtime(args.input_run_runtime, args.g, args.verbose)
    print('Done.')

def precompile_runtime(fname, debug, verbose):
    if (fname.endswith('.c') or
        fname.endswith('.cpp') or
        fname.endswith('.cxx')):

        obj = fname + '.o'
        compargs = 'clang++ -Wall -std=c++17 -c'.split(' ') + [fname, '-o', obj]
        if debug:
            compargs.append('-O0')
            compargs.append('-g')
        else:
            compargs.append('-O3')
            compargs.append('-DNDEBUG')

        if verbose:
            print('Comp args:', ' '.join(compargs))

        subprocess.run(
            compargs,
            check=True)
        return obj
    else:
        return fname

def pretty_print_statistics(stats):
    print('Module statistics:')
    print('{')
    for k, v in stats:
        print('{}: {},'.format(k, v))
    print('}')

def aggregate_statistics(stats):
    empty = input_gen_module.InputGenModule().get_empty_statistics()
    agg_stats = functools.reduce(
        input_gen_module.InputGenModule().add_statistics, stats, empty)
    return agg_stats

def handle_single_module_i(i):
    ds_i = ds.skip(i)
    module = list(ds_i.take(1))[0]
    return handle_single_module((i, module), args)

def handle_single_module(task, args):
    (i, module) = task

    global_outdir = args.outdir
    igm_args = vars(args)
    igm_args['outdir'] = os.path.join(global_outdir, str(i))
    os.makedirs(igm_args['outdir'], exist_ok=True)
    with open(igm_args['outdir'] +"/mod.bc", 'wb') as module_file:
        module_file.write(module['content'])
        module_file.flush()
    igm_args['input_module'] = module_file.name

    igm = input_gen_module.InputGenModule(**igm_args)
    igm.generate_inputs()
    igm.run_all_inputs()

    return igm.get_statistics()

def add_option_args(parser):
    parser.add_argument('--dataset', default='llvm-ml/ComPile')
    parser.add_argument('--outdir', required=True)

    parser.add_argument('--start', type=int, default=0)
    parser.add_argument('--end', type=int, default=10)
    # Number of cores available by default
    parser.add_argument('--num-procs', type=int, default=None)

    parser.add_argument('--precompile-rts', action='store_true')
    parser.add_argument('--no-precompile-rts',
                        dest='precompile_rts', action='store_false')
    parser.set_defaults(precompile_rts=True)
    parser.add_argument('--get-jug-results', action='store_true')

    input_gen_module.add_option_args(parser)

if __name__ == '__main__':
    parser = argparse.ArgumentParser('MassInputGen')
    add_option_args(parser)
    args = parser.parse_args()

    if args.precompile_rts:
        precompile_runtimes(args)

    ds = load_dataset(args.dataset, split='train', streaming=True)

    os.makedirs(args.outdir, exist_ok=True)

    print('Will input gen for dataset {} in {}'.format(args.dataset, args.outdir))

    global_outdir = args.outdir
    igm_args = vars(args)
    with multiprocessing.Pool(args.num_procs) as pool:
        tasks = range(args.start, args.end)
        stats = pool.imap(handle_single_module_i, tasks, chunksize=1)
        stats = list(stats)
        agg_stats = aggregate_statistics(stats)

        pretty_print_statistics(list(zip(range(args.start, args.end), stats)))

        print('Statistics: {}'.format(agg_stats))

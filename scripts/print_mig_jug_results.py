#!/usr/bin/env python3

import jug
import sys
import os
import functools
jf = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'jugfile.py')
print(jf, jf[:-3] + '.jugdata/')
jug.init(jf, jf[:-3] + '.jugdata/')
import mass_input_gen as mig
import jugfile

stats = jugfile.results
stats = [x for x in jugfile.results if x[1].can_load()]

if jugfile.args.invalidate_instrumentation_failures:
    for i, task in stats:
        val = jug.task.value(task)
        if val['num_instrumented_funcs'] == 0:
            print('Invalidating {}'.format(i))
            task.invalidate()
    sys.exit(0)

stats = jug.task.value(stats)

agg_stats = mig.aggregate_statistics([x[1] for x in stats])

mig.pretty_print_statistics(stats)
print('Statistics: {}'.format(agg_stats))
print('Len {}'.format(len(stats)))

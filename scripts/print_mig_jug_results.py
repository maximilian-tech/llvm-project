#!/usr/bin/env python3

import jug
import os
jf = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'jugfile.py')
print(jf, jf[:-3] + '.jugdata/')
jug.init(jf, jf[:-3] + '.jugdata/')
import mass_input_gen as mig
import jugfile
mig.pretty_print_statistics(jug.task.value([x for x in jugfile.results if x[1].can_load()]))

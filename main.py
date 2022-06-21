import argparse
import profiler
import reporter
from datetime import datetime

# Parsing arguments
parser = argparse.ArgumentParser(description="The Tool for analyzing conflicts in GPU local memory banks")
parser.add_argument("-gtpin", required=True, type=str, \
    help="Absolute or relative path to Intel GTPin (path to Profiler directory)")
parser.add_argument("-pti", required=True, type=str, \
    help="Absolute or relative path to Intel PTI")
parser.add_argument("-app", required=True, type=str, \
    help="Absolute or relative path to application")
parser.add_argument("-args", required=False, type=str, \
    help="Arguments for application")
parser.add_argument("-kernel", required=True, type=str, \
    help="Kernel name that needs to be traced")
parser.add_argument("-nb", "--number-banks", required=True, type=int, \
    help="Number of locac memory banks")
parser.add_argument("-op", "--output-path", required=True, type=str, \
    help="Absolute or relative path where the result will be written")

args = parser.parse_args()
path_gtpin = args.gtpin
path_pti = args.pti
path_app = args.app
if args.args:
    app_args = args.args
else:
    app_args = ""

kernel_name = args.kernel
number_banks = args.number_banks
path_op = args.output_path

profiler.run_memorytrace(path_gtpin, 1, path_app, app_args)

profiler.run_memorytrace(path_gtpin, 2, path_app, app_args)

#profiler.uncompress_memtrace(path_gtpin, kernel_name)

#result = profiler.analyze_memtrace_result(number_banks, kernel_name)

source_asm = profiler.build_and_run_cl_debug_info(path_pti, path_app, path_op, app_args)

#reporter.create_report(source_asm, path_op, kernel_name, result)

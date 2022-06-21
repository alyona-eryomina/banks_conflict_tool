# banks_conflict_tool
The tool for analyzing conflicts in GPU localmemory banks

How to use:
Run stript main.py with arguments:
-gtpin - Absolute or relative path to Intel GTPin (path to Profiler directory)
-pti - Absolute or relative path to Intel PTI
-app - Absolute or relative path to application
-args - Arguments for application
-kernel - Kernel name that needs to be traced
-nb - Number of local memory banks
-op - Absolute or relative path where the result will be written

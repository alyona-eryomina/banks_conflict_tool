import os

# Function for running Intel GTPin Memorytrace tool
# path_gtpin: absolute or relative path to Intel GTPin (path to Profiler directory)
# phase: phase of running Intel GTPin Memorytrace tool
#   1 - pre-processing phase. A phase in which the tool determines the number of required records to be saved
#   2 - trace gathering phase. A phase in which the actual trace is collected.
# path_app: absolute or relative path to application
# app_args: agruments to application
def run_memorytrace(path_gtpin, phase, path_app, app_args = ""):
    if not os.path.exists(path_gtpin) or \
        os.path.split(os.path.abspath(path_gtpin))[-1] != "Profilers":
        print("Path to Intel GTPin doesn't correct")
        return -1
    if not os.path.exists(path_app):
        print("Path to application doesn't correct")
        return -2
    if not (phase == 1 or phase == 2):
        print("Phase doesn't correct. It must be 1 or 2")
        return -3

    script_dir = os.path.abspath(os.curdir)
    abs_path_gtpin = os.path.abspath(path_gtpin)
    tool_path = os.path.join(abs_path_gtpin, "Examples", "build", "localmemorytrace.so")
    if not os.path.exists(tool_path):
        print("Build localmemorytrace tool")
        command = "cp localmemorytrace.cpp CMakeLists.txt " + os.path.join(abs_path_gtpin, "Examples")
        print(">> " + command)
        os.system(command)

        command = "cd " + os.path.join(abs_path_gtpin, "Examples")
        print(">> " + command)
        os.chdir(os.path.join(abs_path_gtpin, "Examples"))

        print(">> mkdir build && cd build")
        os.system("mkdir build")
        os.chdir(os.path.join(abs_path_gtpin, "Examples", "build"))

        print(">> cmake .. -DCMAKE_BUILD_TYPE=Release -DARCH=intel64 -DGTPIN_KIT=" + abs_path_gtpin)
        os.system("/home/u153558/.local/bin/cmake .. -DCMAKE_BUILD_TYPE=Release -DARCH=intel64 -DGTPIN_KIT=" + abs_path_gtpin)
        
        print(">> make install")
        os.system("make install")

        print("Build success!")
        os.chdir(script_dir)
    
    print("Phase", phase)
    command = os.path.join(abs_path_gtpin, "Bin", "gtpin") + " -t " + os.path.join(os.path.abspath(path_gtpin), "Examples", "build", "localmemorytrace.so") + " --phase " + str(phase) + " -- " + path_app + " " + app_args
    print(">>", command)
    os.system(os.path.abspath(command))
    
    return 0

# Function for uncompress Memorytrace
# path_gtpin: absolute or relative path to Intel GTPin (path to Profiler directory)
# trace_dir: absolute or relative path to generated on phase 2 directory GTPIN_PROFILE_LOCALMEMORYTRACE*
#  can be empty: when was used later GTPIN_PROFILE_LOCALMEMORYTRACE directory
# kernel_name: name of kernel
def uncompress_memtrace(path_gtpin, kernel_name, trace_dir = ""):
    if not os.path.exists(os.path.abspath(path_gtpin)) or \
        os.path.split(os.path.abspath(path_gtpin))[-1] != "Profilers":
        print(os.path.abspath(path_gtpin))
        print(os.path.split(os.path.abspath(path_gtpin))[-1])
        print("Path to Intel GTPin doesn't correct")
        return -1

    if trace_dir != "" and (not os.path.exists(trace_dir) or \
        trace_dir.find("GTPIN_PROFILE_LOCALMEMORYTRACE")) == -1:
        print("Path to trace directory doens't correct")
        return -2
    
    if trace_dir == "":
        number = 0
        for dir in os.listdir(os.path.abspath(os.curdir)):
            if dir.find("GTPIN_PROFILE_LOCALMEMORYTRACE") != -1:
                if int(dir[30:]) > number:
                    number = int(dir[30:])
        trace_dir = os.path.join(os.path.abspath(os.curdir), "GTPIN_PROFILE_LOCALMEMORYTRACE" + str(number))
    
    path_trace = os.path.join(os.path.abspath(trace_dir), "Session_Final", kernel_name)
    if not os.path.exists(path_trace):
        print("Kernel name doesn't correct")
        return -3
    
    print("-- Uncompress Memorytrace...")
    for dir in os.listdir(path_trace):
        command = "python3 " + os.path.join(os.path.abspath(path_gtpin), "Scripts", "uncompress_memtrace.py") + " --input_dir " + os.path.join(path_trace, dir) + " -v"
        print(">>", command)
        os.system(command)
    print("-- Uncompress finished")

# Function for build Intel PTI cl_debug_info sample
# path_pti: absolute or relative path to Intel PTI
# path_app: absolute or relative path to application
# path_op: absolute or relative path where the assembler will be written
# app_args: agruments to application
def build_and_run_cl_debug_info(path_pti, path_app, path_op, app_args = ""):
    if not os.path.exists(path_pti):
        print("Path to Intel PTI doesn't correct")
        return -1
    script_dir = os.path.abspath(os.curdir)

    abs_path_op = os.path.abspath(path_op)
    if not os.path.exists(abs_path_op):
        print("Result path doesn't exist. Create dir...")
        print(">> mkdir", abs_path_op)
        os.system("mkdir " + abs_path_op)
    
    abs_path_pti = os.path.abspath(path_pti)
    change_path = os.path.join(abs_path_pti, "samples", "cl_debug_info")
    print(">> cd", change_path)
    os.chdir(change_path)

    print(">> mkdir build && cd build")
    os.system("mkdir build && cd build")
    os.chdir(os.path.join(change_path, "build"))

    print(">> cmake -DCMAKE_BUILD_TYPE=Release ..")
    #os.system("cmake -DCMAKE_BUILD_TYPE=Release ..")
    os.system("/home/u153558/.local/bin/cmake -DCMAKE_BUILD_TYPE=Release ..")
    
    print(">> make")
    os.system("make")
    os.chdir(script_dir)
    
    out_path = os.path.join(abs_path_op, "source_asm.txt")
    command = os.path.join(abs_path_pti, "samples", "cl_debug_info", "build", "cl_debug_info") + " " + os.path.abspath(path_app) + " " + app_args + " 2> " + out_path
    print(">>", command)
    os.system(command)

    return out_path

# Function for analyze Intel GTPin Memorytrace tool result
# num_banks: number of local memory banks
# kernel_name: name of kernel
# trace_dir: absolute or relative path to generated on phase 2 directory GTPIN_PROFILE_LOCALMEMORYTRACE*
#  can be empty: when was used later GTPIN_PROFILE_LOCALMEMORYTRACE directory
def analyze_memtrace_result(num_banks, kernel_name, trace_dir = ""):
    if trace_dir != "" and (not os.path.exists(trace_dir) or \
        trace_dir.find("GTPIN_PROFILE_LOCALMEMORYTRACE")) == -1:
        print("Path to trace directory doens't correct")
        return -2
    
    if trace_dir == "":
        number = 0
        for dir in os.listdir(os.path.abspath(os.curdir)):
            if dir.find("GTPIN_PROFILE_LOCALMEMORYTRACE") != -1:
                 if int(dir[30:]) > number:
                    number = int(dir[30:])
        trace_dir = os.path.join(os.path.abspath(os.curdir), "GTPIN_PROFILE_LOCALMEMORYTRACE" + str(number))
    
    path_trace = os.path.join(os.path.abspath(trace_dir), "Session_Final", kernel_name)
    if not os.path.exists(path_trace):
        print("Kernel name doesn't correct")
        return -3
    
    all_patterns = []  # [ [send-offset address address ... ] [send-offset address address ... ] ... ]
    
    for dir in os.listdir(path_trace):
        print("|- Directory: " + dir)
        for file in os.listdir(os.path.join(path_trace, dir)):
            if file.find(".bin") == -1:
                print("|-- File: " + file)
                fin = open(os.path.join(path_trace, dir, file),"r")
                pattern = []  # [send-offset address address ... ]
                for line in fin:
                    if line.find("BTI = fe") != -1:  # local memory
                        elems = line.split()
                        send_offset = int(elems[2], 16) # - 8
                        address = int(elems[4], 16)
        
                        if pattern == []:
                            pattern.append(send_offset)
                            pattern.append(address)
                        else:
                            if pattern[0] == send_offset:
                                pattern.append(address)
                            else:
                                if pattern.count(pattern[1]) != len(pattern) - 1:  # Not broadcast
                                    if all_patterns.count(pattern) == 0:
                                        all_patterns.append(pattern)

                                pattern = []
                                pattern.append(send_offset)
                                pattern.append(address)
                                
                if pattern.count(pattern[1]) != len(pattern) - 1: # Not broadcast
                    if all_patterns.count(pattern) == 0:
                        all_patterns.append(pattern)
    
    conflicts = []  # [ [send-offset power] [send-offset power] ...]
    for pattern in all_patterns:
        # Transform from addresses to banks
        send_offset = pattern.pop(0)
        for i in range(len(pattern)):
            pattern[i] = (pattern[i] // 4) % num_banks
    
        count = 0
        for i in range(len(pattern)):
            count = max(count, pattern.count(pattern[i]))
        
        if count == 1:
            conflicts.append([kernel_name, send_offset, 0])
        else:
            conflicts.append([kernel_name, send_offset, count])

    results = {}  # { send-offset : [ [power count] ... ] }
    for conflict in conflicts:
        result = conflict[1]
        count = [conflict[2], conflicts.count(conflict)]

        if results.get(result) == None:
            results[result] = [count]
        elif results.get(result).count(count) == 0:
            results[result].append(count)
    
    for result in results:
        length = 0
        for i in range(len(results[result])):
            length += results[result][i][1]
        for i in range(len(results[result])):
            results[result][i][1] = round(results[result][i][1] / length * 100, 4)
    
    print(results)
    return results

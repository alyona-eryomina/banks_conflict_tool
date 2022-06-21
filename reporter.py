import os

def create_report(source_asm, path_op, kernel_name, conflicts):
    if not os.path.exists(source_asm):
        print("Source asm file doesn't exist")
        return -1
    
    abs_path_op = os.path.abspath(path_op)
    if not os.path.exists(abs_path_op):
        print("Result path doesn't exist. Create dir...")
        print(">> mkdir", abs_path_op)
        os.system("mkdir " + abs_path_op)
    
    fin = open(source_asm, "r")
    fout = open(os.path.join(abs_path_op, "report_" + kernel_name + ".html"), "w")
    
    # Parse source-asm file
    s_asm = {}  # { line : [address, address, ...] }
    asm_lines = {}  # { address : line }
    current_line = 0
    num = 1
    for line in fin:
        if line[0] == "[":
            s_asm[num] = []
            current_line = num
            num += 1
        elif line[0] == "\t" and current_line != 0:
            #s_asm[current_line].append(int(line[3:10], 16))
            asm_lines[line[3:10]] = line
            s_asm[current_line].append(line[3:10])
    fin.close()

    fout.write("<!DOCTYPE html>\n")
    fout.write("<html>\n")
    fout.write("  <head>\n")

    fout.write("    <style>\n")
    fout.write("      .conflict {background-color: lightcoral;}\n")
    fout.write("      .noconflict {background-color: lightgreen;}\n")
    fout.write("      .active {background-color: blanchedalmond;}\n")
    fout.write("      #sourcecol {height: 650px; width: 50%; float:left; outline: 1px solid grey; overflow: scroll;}\n")
    fout.write("      #asmcol {height: 650px; width: 50%; float:right; outline: 1px solid grey; overflow: scroll;}\n")
    fout.write("    </style>\n")

    fout.write("    <script>\n")
    fout.write("      var map = new Map();\n")
    line = ""
    for key in s_asm:
        line = "map.set(\"" + str(key) + "\", [";
        if s_asm.get(key) != []:
            for value in s_asm.get(key):
                line += "\"" + value + "\", "
            line = line[:-2] + "]);\n"
        else:
            line = line[:-1] + "null);\n"
        fout.write("      " + line)
    fout.write("      function doFunction(id, className) {\n")
    fout.write("        var elems = document.querySelector(\"#sourcecol\");\n")
    fout.write("        for (var child of elems.children) {\n")
    fout.write("          child.classList.remove(\"active\");\n")
    fout.write("        }\n")
    fout.write("        elems = document.querySelector(\"#asmcol\");\n")
    fout.write("        for (var child of elems.children) {\n")
    fout.write("          child.classList.remove(\"active\");\n")
    fout.write("          child.classList.remove(\"conflict\");\n")
    fout.write("          child.classList.remove(\"noconflict\");\n")
    fout.write("        }\n")

    fout.write("        var newClass = \"\";\n")
    fout.write("        elem = document.getElementById(id);\n")
    fout.write("        for (var classCor of elem.classList) {\n")
    fout.write("          if (classCor != \"active\") {\n")
    fout.write("            newClass = classCor;\n")
    fout.write("            break;\n")
    fout.write("          }\n")
    fout.write("        }\n")

    fout.write("        if (newClass == \"\") {\n")
    fout.write("          elem.classList.add(\"active\");\n")
    fout.write("        }\n")

    fout.write("        for (var asmId of map.get(id)) {\n")
    fout.write("          elem = document.getElementById(asmId);\n")
    fout.write("          if (newClass != \"\") {\n")
    fout.write("            elem.classList.add(newClass);\n")
    fout.write("          } else {\n")
    fout.write("            elem.classList.add(\"active\");\n")
    fout.write("          }\n")
    fout.write("        }\n")
    fout.write("      }\n")
    fout.write("    </script>\n")

    fout.write("  </head>\n")
    fout.write("  <body>\n")
    fout.write("    <h1>The Tool for analyzing conflicts in GPU localmemory banks</h1>\n")
    fout.write("    <h3>Report for " + kernel_name + " </h3>\n")
    fout.write("    <div id = \"sourcecol\">\n")
    fin = open(source_asm, "r")
    conflict_line = ""
    write_line = ""
    num = 1
    for line in fin:
        if line[0] == "[":  # line with source code
            for address in s_asm[num]:
                if conflicts.get(int(address, 16)) != None:
                    conflict_line = "Conflicts!"
                    for conflict in conflicts[int(address, 16)]:
                        if conflict[0] == 0:
                            conflict_line = "No conflicts!"
                        else:
                            conflict_line += " Power = " + str(conflict[0]) + ", persent = " + str(conflict[1]) + ";"
            if conflict_line.find("Conflicts!") != -1:
                write_line = "      <pre id = " + str(num) + " class = conflict onclick=\"doFunction(id)\">" + str(num) + "\t" + line[7:-1] + conflict_line + "</pre>\n"
            elif conflict_line.find("No conflicts!") != -1:
                write_line = "      <pre id = " + str(num) + " class = noconflict onclick=\"doFunction(id)\">" + str(num) + "\t" + line[7:-1] + conflict_line + "</pre>\n"
            else:
                write_line = "      <pre id = " + str(num) + " onclick=\"doFunction(id)\">" + str(num) + "\t" + line[7:-1] + "</pre>\n"
            fout.write(write_line)
            num += 1
            conflict_line = ""
    fout.write("    </div>\n")
    fout.write("    <div id = \"asmcol\">\n")
    for line in sorted(asm_lines.items()):
        fout.write("      <pre id = \"" + line[0] + "\">" + line[1][2:-1] + "</pre>\n")
    fout.write("    </div>\n")
    fout.write("  </body>\n")
    fout.write("</html>\n")

    fin.close()
    fout.close()

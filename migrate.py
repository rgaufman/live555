## Replace symbolic links with proper files in src directory
import os
from pathlib import Path

def prefix(path):
    n = path.count("/")
    if n==1:
        return "../"
    if n==2:
        return "../../"
    if n==3:
        return "../../../"
    if n==4:
        return "../../../../"

def replace(path, newPath):
    # delete file
    os.unlink(path)
    # create new file
    if newPath.endswith("hh"):
        content = "#pragma once\n// link to original location\n#include \""+prefix(path)+newPath+"\"\n\n"
    else:
        content = "// link to original location\n#include \""+prefix(path)+newPath+"\"\n\n"        
    with open(path, "w") as text_file:
        text_file.write(content)

working_dir = os.getcwd()+"/"
for path, currentDirectory, files in os.walk("src"):
    for file in files:
        p = os.path.join(path, file);
        if (Path(p).is_symlink()):
            linkedPath = str(os.path.realpath(p)).replace(working_dir,"")
            print(p + " ->" + linkedPath);
            replace(p, linkedPath)

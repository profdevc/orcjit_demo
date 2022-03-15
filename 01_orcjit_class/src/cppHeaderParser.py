import CppHeaderParser
import sys
import os


abs_path = os.path.abspath(sys.argv[1])
# print(abs_path)
abs_dir=abs_path[0:abs_path.rfind('/')+1]
# print(abs_dir)

ffname=abs_path[len(abs_dir):]
ffname=ffname[0:ffname.find('.')]
filenamecpp=ffname+".cpp"
filenamell=ffname+".cpp.ll"
if os.stat(abs_dir+filenamecpp).st_mtime > os.stat(abs_dir+filenamell).st_mtime:
    print("cpp")
    sys.exit(0)

header_test=[]

cppHeader=CppHeaderParser.CppHeader(abs_dir+filenamecpp)
headers=cppHeader.includes
# print(headers)
for i in headers:
    if i[0] == '"':
        ref_dir=abs_dir
        path = i[1:-1]
        if path[0:3] =='../':   # relative path
            while path[0:3] == '../':
                ref_dir=ref_dir[0:-1]
                ref_dir=ref_dir[0:ref_dir.rfind('/')+1]
                path=path[3:]
                h_abs_path=ref_dir+path
        elif path.find('/') == -1:  # current path
            h_abs_path=ref_dir+path
        else :  # absolute path
            h_abs_path=path

        # print(h_abs_path)
        if os.path.isfile(h_abs_path):
            header_test.append(h_abs_path)
        else:
            print("Error: Invalid path: "+h_abs_path)
            sys.exit(0)

for h in header_test:
    if os.stat(h).st_mtime > os.stat(abs_dir+filenamell).st_mtime:
        print("cpp")
        sys.exit(0)

print("lll")

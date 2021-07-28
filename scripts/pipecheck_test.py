import sys
import os
import re
import argparse
import subprocess
parser = argparse.ArgumentParser()
parser.add_argument('-test', type=str, help='test')
parser.add_argument('-time', type=str, help='time')
parser.add_argument('-rtlcheck', type=str, help='rtlcheck')
parser.add_argument('-uarch', type=str, help='uarch')
args = parser.parse_args()
if args.test is not None:
    cmd = "pipecheck -v 8 -r -i %s -m ./vscale.uarch -o %s" % (args.test, "a.dot")
    print(cmd)
    os.system(cmd)
    exit(0)
test_dir = "./rtlcheck/tests/rtlcheck/SC"
if not os.path.isdir(test_dir):
    print(test_dir + " doesn't exist")
    sys.exit(0)
sc_tests = os.listdir(test_dir)
res_path = "check_res"
uarch="./vscale.uarch"
if args.rtlcheck is not None:
    uarch="~/rtlcheck/uarches/Vscale.uarch"
if args.uarch is not None:
    uarch=args.uarch
print("uarch file: ", uarch)
if not os.path.isdir(res_path):
    os.mkdir(res_path)
else:
    print("clean")
    os.system("rm -f " + res_path + "/*")
import time
print("num of test: ", len(sc_tests))
start_time = time.time()
total_ = 0
fail_ = False

for litmus in sorted(sc_tests):
    # if not "n2" in litmus: 
    #     continue
    dot_file = res_path + "/" + litmus.split(".")[0] + ".dot"
    cmd = "pipecheck -r -i %s -m %s -o %s" % (test_dir + "/" + litmus, uarch, dot_file)

    tic = time.time_ns()
    out = subprocess.check_output(cmd, shell=True)
    toc = time.time_ns() - tic
    total_ += toc
    print("%s,%f" %(litmus, toc/(10**6)))
    out = out.decode('utf-8')
    if re.search("Forbidden\/Observable", out) is not None or re.search("Required\/Not observable", out) is not None:
        print('fail', cmd)
        fail_ = True
#print("--- %s seconds ---" % (time.time() - start_time))
print("--- %f ms ---" % (total_/(10**6)))
import subprocess
warnexist = False
try:
    result = subprocess.check_output("grep -R \"WARN\" " + res_path + "/*", shell=True)
    warnexist = True
except subprocess.CalledProcessError as ex:
    result = ex.output
    if ex.returncode != 1:
        warnexist = True
    
if fail_ or warnexist:
    print("======= FAIL ===========")
else:
    print("======= ALL TESTS PASSES =======")



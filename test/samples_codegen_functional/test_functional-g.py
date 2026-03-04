import subprocess
import os

# Set the `lib_path` to [where "libcactio.a" is located]
lib_path = "../../build"
# Set the `compler_path` to [where compiler is located]
compiler_path = "../../build/compiler"
# Set the `compiler_flags` to the flags passed to CACT compiler
compiler_flags = "-O0"
temp_path = "temp/"
inout_path = "./"

info_prefix = "\033[1;36mINFO: \033[0m\t"
error_prefix = "\033[1;35m**Error: \033[0m\t"
succ_prefix = "\033[1;32mPassed: \033[0m\t"

def clean():
    print(info_prefix, "Cleaning...")
    for f in os.listdir("."):
        if f.endswith(".exe") or f.endswith(".s") or f.endswith(".c") or f.endswith("_exe.out") or f.endswith("_temp.c"):
            os.remove(f)

# if use riscv64-unknown-elf-gcc to compile, add these declarations
declarations = """#include <stdio.h>
#include <stdbool.h>
void print_int(int x);
void print_double(double x);
void print_float(float x);
void print_bool(bool x);
int get_int();
double get_double();
float get_float();
"""


def test(use_gcc = False):
    # search all file ends with .c, sort them alphabetically
    #succ_files = []
    cact_files = [f for f in os.listdir(".") if f.endswith(".cact")]
    #print(len(cact_files))
    i = 0
    err = 0
    for cact_file in sorted(cact_files):
        #print(i)
        # print(cact_file)
        # read cact_file
        # write to temp file, add declarations
        # for own compiler test, no need for this step
        in_file = inout_path + cact_file[:-5] + ".in"
        c_file = temp_path + cact_file[:-5] + ".c"
        s_file = temp_path + cact_file[:-5] + ".s"
        exe_file = temp_path + cact_file[:-5] + ".exe"
        #out_file = temp_path + exe_file + "_exe.out"
        out_file = temp_path + cact_file[:-5] + ".out"
        out_ref = inout_path + cact_file[:-5] + ".out"
        # ./compiler cact_file -S -o s_file
        print(info_prefix, "Using ", "GCC" if use_gcc else "CACTC", " to compile ", cact_file, "...")
        if use_gcc:
            # .c file content is 
            with open(cact_file, 'r') as f:
                c_code = f.read()
            # write to temp file, add declarations
            # for own compiler test, no need for this step
            with open(c_file, 'w') as f:
                f.write(declarations)
                f.write("\n")
                f.write(c_code)
            ret = subprocess.run(["riscv64-unknown-elf-gcc", c_file, "-S", "-o", s_file])
        else:
            ret = subprocess.run([compiler_path, cact_file, "-S", "-o", s_file, compiler_flags])
        if ret.returncode != 0:
            print(error_prefix, "Failed to compile:", cact_file)
            i += 1
            err += 1
            continue
        # riscv-unknown-elf-gcc s_file -Llib_path -lcactio -o exe_file
        print(info_prefix, "Linking ", exe_file, "...")
        ret = subprocess.run(["riscv64-unknown-elf-gcc", s_file, "-L" + lib_path, "-lcactio", "-o", exe_file])
        if ret.returncode != 0:
            print(error_prefix, "Failed to link!")
            i += 1
            err += 1
            continue        
        # spike pk ./exe_file
        # if exists in_file, feed to stdin
        print(info_prefix, "Running ", exe_file, "...")
        if os.path.exists(in_file):
            ret = subprocess.run(["spike", "pk", exe_file], stdin=open(in_file, 'r'), stdout=subprocess.PIPE)
        else:
            ret = subprocess.run(["spike", "pk", exe_file], stdout=subprocess.PIPE)
        # save return code to file
        with open(out_file, 'w') as f:
            f.write(ret.stdout.decode())
            f.write(str(ret.returncode))
            f.write("\n")
        # compare with reference
        # use string compare instead of diff
        # read out_file
        with open(out_file, 'r') as f:
            out_content = f.read()
        with open(out_ref, 'r') as f:
            out_ref_content = f.read()
        if out_content != out_ref_content:
            print(error_prefix, "Failed to compare:", cact_file)
            i += 1
            err += 1
            continue
        # ret = subprocess.run(["diff", out_file, out_ref])
        # if ret.returncode != 0:
        #     print("Failed to compare:", cact_file)
        #     i += 1
        #     err += 1
        #     continue
        #succ_files.append(cact_file)
        i += 1
        

    print(succ_prefix, len(cact_files)-err, "of", len(cact_files))
    #print(succ_files)




if __name__ == "__main__":
    subprocess.run(["mkdir", "-p", temp_path])
    #test(True)
    test(False)
    #clean()
# This script compiles the given source file into 'malloc.so'.
# Then it sets 'LD_PRELOAD' to './malloc.so'. This way, when
# you run a program that uses malloc, the implementation in
# 'malloc.so' will be used. You can check using gdb.

export LD_PRELOAD=""
echo "$0: NOTE: Source this script to have the LD_PRELOAD set"

if [ -z "$1" ]; then
    echo "$0: expected a source file name"
else
    clang -O0 -g -W -Wall -Wextra -shared -fPIC "$1" -o malloc.so
    export LD_PRELOAD=./malloc.co
    echo "$0: Set LD_PRELOAD to $LD_PRELOAD"
fi

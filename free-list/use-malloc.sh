# This script will compile the first argument as a shared
# library (malloc.so) and will LD_PRELOAD this shared
# library before running the given commend (second argument).
# This way, I can test my malloc implementations on real programs.
# The libraries in LD_PRELOAD will override the defaults.

if [ -z "$1" ]; then
    echo "$0: expected a source file name"
fi
if [ -z "$2" ]; then
    echo "$0: expected a command to run"
fi

clang -O0 -g -W -Wall -Wextra -shared -fPIC "$1" -o malloc.so
LD_PRELOAD=./malloc.so "$2"

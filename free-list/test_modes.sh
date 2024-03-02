#!/bin/env bash

# Test all three modes in free_list1.c

modes=("FIRST" "NEXT" "BEST")

for mode in "${modes[@]}"; do
    gcc -DSEARCH_MODE="${mode}"_FIT -g free_list1.c && ./a.out
done

rm a.out
echo "All modes tests"

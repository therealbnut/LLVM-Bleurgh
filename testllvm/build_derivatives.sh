#!/bin/sh
gcc -S -emit-llvm main.c
llc -march=cpp main.s

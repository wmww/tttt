#!/usr/bin/env python3

max_num = 22
prefix = 'CLEAT_GEN'

print('// auto-generated from gen_metaprogramming.py, DO NOT EDIT!')
print(f'// dealing with close to or more then {max_num} elements may not work')
print('// this limit can be increased in the generator script\n')

print('#pragma once')
print('#if !defined(CLEAT_METAPROGRAMMING_H)')
print('#error this file should only be included by define_CLEAT.h')
print('#endif')

print(f'#define CLEAT_GEN_MAX_NUM {max_num}\n')

print('// CLEAT_INC_ and CLEAT_DEC_')
print('// usage: cat CLEAT_INC_ or CLEAT_DEC_ to any small number to get n+1 or n-1')
for i in range(max_num):
    print(f'#define CLEAT_INC_{i} {i + 1}')
print()
for i in range(max_num):
    print(f'#define CLEAT_DEC_{i + 1} {i}')
print()

print('// enables the usage of CLEAT_EQ on numbers')
for i in range(max_num):
    print(f'#define CLEAT_ENABLE_EQ_{i}_{i}')
print()

print('// CLEAT_REMOVE_COMMAS')
print(f'#define _{prefix}_REMOVE_COMMAS(...) _{prefix}_REMOVE_COMMAS_A(__VA_ARGS__', end='')
for i in range(max_num):
    print(',', end='')
print(')')
print(f'#define _{prefix}_REMOVE_COMMAS_A(', end='')
for i in range(max_num):
    print(f'_{i}, ', end='')
print('...) ', end='')
for i in range(max_num):
    print(f'_{i} ', end='')
print('\n')

print('// CLEAT_COUNT')
print(f'#define _{prefix}_COUNT_NUMBERS ', end='')
for i in range(max_num, 1, -1):
    print(f'{i}, ', end='')
print('1')
print(f'#define _{prefix}_COUNT(', end='')
for i in range(max_num):
    print(f'_{i}, ', end='')
print('n, ...) n\n')

print('// CLEAT_MAP')
print(f'#define _{prefix}_MAP_0(closure, order, index, dir, empty)')
print(f'#define _{prefix}_MAP_1(closure, order, index, dir, item) CLEAT_CLOSURE_INVOKE(closure, item, index)')
for i in range(2, max_num + 1):
    print(f'#define _{prefix}_MAP_{i}(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _{prefix}_MAP_{i - 1}(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))')
print()

// auto-generated from gen_metaprogramming.py, DO NOT EDIT!
// dealing with close to or more then 22 elements may not work
// this limit can be increased in the generator script

#pragma once
#if !defined(CLEAT_METAPROGRAMMING_H)
#error this file should only be included by define_CLEAT.h
#endif
#define CLEAT_GEN_MAX_NUM 22

// CLEAT_INC_ and CLEAT_DEC_
// usage: cat CLEAT_INC_ or CLEAT_DEC_ to any small number to get n+1 or n-1
#define CLEAT_INC_0 1
#define CLEAT_INC_1 2
#define CLEAT_INC_2 3
#define CLEAT_INC_3 4
#define CLEAT_INC_4 5
#define CLEAT_INC_5 6
#define CLEAT_INC_6 7
#define CLEAT_INC_7 8
#define CLEAT_INC_8 9
#define CLEAT_INC_9 10
#define CLEAT_INC_10 11
#define CLEAT_INC_11 12
#define CLEAT_INC_12 13
#define CLEAT_INC_13 14
#define CLEAT_INC_14 15
#define CLEAT_INC_15 16
#define CLEAT_INC_16 17
#define CLEAT_INC_17 18
#define CLEAT_INC_18 19
#define CLEAT_INC_19 20
#define CLEAT_INC_20 21
#define CLEAT_INC_21 22

#define CLEAT_DEC_1 0
#define CLEAT_DEC_2 1
#define CLEAT_DEC_3 2
#define CLEAT_DEC_4 3
#define CLEAT_DEC_5 4
#define CLEAT_DEC_6 5
#define CLEAT_DEC_7 6
#define CLEAT_DEC_8 7
#define CLEAT_DEC_9 8
#define CLEAT_DEC_10 9
#define CLEAT_DEC_11 10
#define CLEAT_DEC_12 11
#define CLEAT_DEC_13 12
#define CLEAT_DEC_14 13
#define CLEAT_DEC_15 14
#define CLEAT_DEC_16 15
#define CLEAT_DEC_17 16
#define CLEAT_DEC_18 17
#define CLEAT_DEC_19 18
#define CLEAT_DEC_20 19
#define CLEAT_DEC_21 20
#define CLEAT_DEC_22 21

// enables the usage of CLEAT_EQ on numbers
#define CLEAT_ENABLE_EQ_0_0
#define CLEAT_ENABLE_EQ_1_1
#define CLEAT_ENABLE_EQ_2_2
#define CLEAT_ENABLE_EQ_3_3
#define CLEAT_ENABLE_EQ_4_4
#define CLEAT_ENABLE_EQ_5_5
#define CLEAT_ENABLE_EQ_6_6
#define CLEAT_ENABLE_EQ_7_7
#define CLEAT_ENABLE_EQ_8_8
#define CLEAT_ENABLE_EQ_9_9
#define CLEAT_ENABLE_EQ_10_10
#define CLEAT_ENABLE_EQ_11_11
#define CLEAT_ENABLE_EQ_12_12
#define CLEAT_ENABLE_EQ_13_13
#define CLEAT_ENABLE_EQ_14_14
#define CLEAT_ENABLE_EQ_15_15
#define CLEAT_ENABLE_EQ_16_16
#define CLEAT_ENABLE_EQ_17_17
#define CLEAT_ENABLE_EQ_18_18
#define CLEAT_ENABLE_EQ_19_19
#define CLEAT_ENABLE_EQ_20_20
#define CLEAT_ENABLE_EQ_21_21

// CLEAT_REMOVE_COMMAS
#define _CLEAT_GEN_REMOVE_COMMAS(...) _CLEAT_GEN_REMOVE_COMMAS_A(__VA_ARGS__,,,,,,,,,,,,,,,,,,,,,,)
#define _CLEAT_GEN_REMOVE_COMMAS_A(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, ...) _0 _1 _2 _3 _4 _5 _6 _7 _8 _9 _10 _11 _12 _13 _14 _15 _16 _17 _18 _19 _20 _21 

// CLEAT_COUNT
#define _CLEAT_GEN_COUNT_NUMBERS 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
#define _CLEAT_GEN_COUNT(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, n, ...) n

// CLEAT_MAP
#define _CLEAT_GEN_MAP_0(closure, order, index, dir, empty)
#define _CLEAT_GEN_MAP_1(closure, order, index, dir, item) CLEAT_CLOSURE_INVOKE(closure, item, index)
#define _CLEAT_GEN_MAP_2(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_1(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_3(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_2(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_4(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_3(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_5(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_4(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_6(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_5(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_7(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_6(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_8(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_7(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_9(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_8(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_10(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_9(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_11(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_10(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_12(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_11(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_13(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_12(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_14(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_13(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_15(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_14(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_16(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_15(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_17(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_16(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_18(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_17(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_19(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_18(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_20(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_19(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_21(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_20(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))
#define _CLEAT_GEN_MAP_22(closure, order, index, dir, item, ...) order(CLEAT_CLOSURE_INVOKE(closure, item, index), _CLEAT_GEN_MAP_21(closure, order, CLEAT_EXPAND_CAT(dir, index), dir, __VA_ARGS__))


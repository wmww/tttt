// Adapted from define_EVIL (https://codeberg.org/sphi/define_EVIL)

#pragma once
#define CLEAT_METAPROGRAMMING_H

// macros auto-generated from generateor script
// should all be prefixed with _GEN_
#include "metaprogramming_generated.h"

// # General Utils

// convert input to string literal, can be used with EXPAND_CALL
// depends on: none
#define CLEAT_TO_STRING(...) #__VA_ARGS__

// used internally by other macros
// depends on: none
#define CLEAT_ORDER_FORWARD(a, b) a b
#define CLEAT_ORDER_BACKWARD(a, b) b a

// expands the arguments and concatenates them
// depends on: none
#define CLEAT_EXPAND_CAT(a, ...) _CLEAT_EXPAND_CAT(a, __VA_ARGS__)
#define _CLEAT_EXPAND_CAT(a, ...) a##__VA_ARGS__

// expands the arguments and then call macro with the expanded arguments
// depends on: none
#define CLEAT_EXPAND_CALL(macro, ...) macro(__VA_ARGS__)

// even I'm not completely sure why this is needed, but it is
// depends on: none
#define CLEAT_EXPAND(...) __VA_ARGS__

// toggleable expansion
// depends on: none
#define CLEAT_EXPAND_TRUE(...) __VA_ARGS__
#define CLEAT_EXPAND_FALSE(...)

// removes commas in a list of arguments, leaves a space between each one
// depends on: _CLEAT_GEN_REMOVE_COMMAS
#define CLEAT_REMOVE_COMMAS(...) _CLEAT_GEN_REMOVE_COMMAS(__VA_ARGS__)

// # Boolean operations
// these expand to the result of boolean logic (not, or, and, xor)
// should only be given inputs TRUE and FALSE, other inputs will yeild unwanted results

// if given TRUE or FALSE, will expand to the oposite
// depends on: CLEAT_EXPAND_CAT
#define CLEAT_NOT(...) _CLEAT_NOT(__VA_ARGS__)
#define _CLEAT_NOT(a) CLEAT_EXPAND_CAT(_CLEAT_BOOL_NOT_, a)
#define _CLEAT_BOOL_NOT_TRUE FALSE
#define _CLEAT_BOOL_NOT_FALSE TRUE

// expands to TRUE if either inputs are TRUE
// depends on: CLEAT_EXPAND_CAT
#define CLEAT_OR(...) _CLEAT_OR(__VA_ARGS__)
#define _CLEAT_OR(a, b) CLEAT_EXPAND_CAT(_CLEAT_BOOL_OR_, CLEAT_EXPAND_CAT(a, CLEAT_EXPAND_CAT(_, b)))
#define _CLEAT_BOOL_OR_FALSE_FALSE FALSE
#define _CLEAT_BOOL_OR_TRUE_FALSE TRUE
#define _CLEAT_BOOL_OR_FALSE_TRUE TRUE
#define _CLEAT_BOOL_OR_TRUE_TRUE TRUE

// expands to TRUE if both inputs are TRUE
// depends on: CLEAT_EXPAND_CAT
#define CLEAT_AND(...) _CLEAT_AND(__VA_ARGS__)
#define _CLEAT_AND(a, b) CLEAT_EXPAND_CAT(_CLEAT_BOOL_AND_, CLEAT_EXPAND_CAT(a, CLEAT_EXPAND_CAT(_, b)))
#define _CLEAT_BOOL_AND_FALSE_FALSE FALSE
#define _CLEAT_BOOL_AND_TRUE_FALSE FALSE
#define _CLEAT_BOOL_AND_FALSE_TRUE FALSE
#define _CLEAT_BOOL_AND_TRUE_TRUE TRUE

// expands to true if exactly one input is TRUE
// depends on: CLEAT_EXPAND_CAT
#define CLEAT_XOR(...) _CLEAT_XOR(__VA_ARGS__)
#define _CLEAT_XOR(a, b) CLEAT_EXPAND_CAT(_CLEAT_BOOL_XOR_, CLEAT_EXPAND_CAT(a, CLEAT_EXPAND_CAT(_, b)))
#define _CLEAT_BOOL_XOR_FALSE_FALSE FALSE
#define _CLEAT_BOOL_XOR_TRUE_FALSE TRUE
#define _CLEAT_BOOL_XOR_FALSE_TRUE TRUE
#define _CLEAT_BOOL_XOR_TRUE_TRUE FALSE

// # Conditionals
// should only be given inputs TRUE and FALSE, other inputs will yeild unwanted results

// expands to the thing in the second set of parentheses only if the thing in the first set is TRUE
// CLEAT_IF(TRUE)(abc)    -> abc
// CLEAT_IF(FALSE)(abc)   -> [empty]
// depends on: CLEAT_NOT, CLEAT_EXPAND_CAT, CLEAT_EXPAND_TRUE, CLEAT_EXPAND_FALSE
#define CLEAT_IF(cond) CLEAT_EXPAND_CAT(_CLEAT_IF_COND_WAS_, cond)
#define _CLEAT_IF_COND_WAS_TRUE(...) __VA_ARGS__
#define _CLEAT_IF_COND_WAS_FALSE(...)

// expands to the thing in the second set of parentheses only if the thing in the first set is FALSE
// CLEAT_IF_NOT(TRUE)(abc)    -> [empty]
// CLEAT_IF_NOT(FALSE)(abc)   -> abc
// depends on: CLEAT_NOT, CLEAT_IF, CLEAT_EXPAND_CAT, CLEAT_EXPAND_TRUE, CLEAT_EXPAND_FALSE
#define CLEAT_IF_NOT(cond) CLEAT_IF(CLEAT_NOT(cond))

// if the thing in the first set of parentheses is true expands to the thing in the second set
// if it's false expands to the thing in the third set
// CLEAT_IF_ELSE(TRUE)(abc)(xyz)  -> abc
// CLEAT_IF_ELSE(FALSE)(abc)(xyz) -> xyz
// depends on: CLEAT_EXPAND_CAT, CLEAT_EXPAND_TRUE, CLEAT_EXPAND_FALSE
#define CLEAT_IF_ELSE(cond) CLEAT_EXPAND_CAT(_CLEAT_IF_ELSE_, cond)
#define _CLEAT_IF_ELSE_TRUE(...) __VA_ARGS__ CLEAT_EXPAND_FALSE
#define _CLEAT_IF_ELSE_FALSE(...) CLEAT_EXPAND_TRUE

// # Equality

// expand to TRUE if two tokens are known to be equal, FALSE otherwise
// only works if there is an ENABLE_EQ_*_* defined for the tokens
// below CLEAT_ENABLE_EQ is defined for booleans, void and empty values, generated.h has it defined for small numbers.
#define CLEAT_EQ(a_val, b_val) CLEAT_IF_ELSE \
    (CLEAT_IS_THING(CLEAT_EXPAND_CAT(CLEAT_ENABLE_EQ_, CLEAT_EXPAND_CAT(b_val, CLEAT_EXPAND_CAT(_, b_val))))) \
    (CLEAT_EXPAND_CALL(CLEAT_TO_STRING, You must define \
        CLEAT_EXPAND_CAT( \
            CLEAT_ENABLE_EQ_, CLEAT_EXPAND_CAT( \
                b_val, CLEAT_EXPAND_CAT(_, b_val))) \
        to use CLEAT_EQ on b_val)) \
    (CLEAT_NOT(CLEAT_IS_THING(CLEAT_EXPAND_CAT(CLEAT_ENABLE_EQ_, CLEAT_EXPAND_CAT(a_val, CLEAT_EXPAND_CAT(_, b_val))))))

// expands to TRUE if the two tokens are not equal (see CLEAT_EQ)
#define CLEAT_NE(a, b) CLEAT_NOT(CLEAT_EQ(a, b))

// enable equality check for various common values, you can add more elsewhere
#define CLEAT_ENABLE_EQ_TRUE_TRUE
#define CLEAT_ENABLE_EQ_FALSE_FALSE
#define CLEAT_ENABLE_EQ_void_void
#define CLEAT_ENABLE_EQ__

// # Info Getters

// NOTE: the final arg can not be the name of a function-like macro! (see below)
// a common way to use is to make two macros (ex. EXAMPLE_TRUE and EXAMPLE_FALSE) and concat your prefix with the
// result of this macro, so you can do different things depending on the thingyness
// the last argument must not be a function-like macro
// IS_THING(one_or_more, args)  -> TRUE
// IS_THING()                   -> FALSE
// IS_THING(FN_MACRO)           -> <fails to compile>
// IS_THING(arg, FN_MACRO)      -> <fails to compile>
// I realize how much of a clusterfuck this is. If you can make it cleaner without failing any tests, plz submit PR
#define CLEAT_IS_THING(...) _CLEAT_IS_THING_A(CLEAT_EXPAND_CAT(_, CLEAT_EXPAND(_CLEAT_IS_THING_LOOK_FOR_PEREN CLEAT_REMOVE_COMMAS(__VA_ARGS__))))
#define _CLEAT_IS_THING_A(a) CLEAT_EXPAND_CALL(_CLEAT_IS_THING_C, _CLEAT_IS_THING_B a (), TRUE)
#define _CLEAT_IS_THING_B() was_not_thing, FALSE
#define _CLEAT_IS_THING_C(a, b, ...) b
#define _CLEAT_IS_THING_LOOK_FOR_PEREN(...) was_thing_with_peren
#define __CLEAT_IS_THING_LOOK_FOR_PEREN

// checks if the argument(s) are completely surrounded by parenthesis
// NOTE: If it starts with parentheses, it can not end with a function-like macro! (see below)
// CLEAT_HAS_PEREN()                 -> FALSE
// CLEAT_HAS_PEREN("a")              -> FALSE
// CLEAT_HAS_PEREN(6, "a")           -> FALSE
// CLEAT_HAS_PEREN((), a, 6, "a")    -> FALSE
// CLEAT_HAS_PEREN(())               -> TRUE
// CLEAT_HAS_PEREN((a, 6, "a"))      -> TRUE
// CLEAT_HAS_PEREN((a) (b))          -> FALSE
// CLEAT_HAS_PEREN(FN_MACRO)         -> FALSE
// CLEAT_HAS_PEREN((FN_MACRO))       -> TRUE
// CLEAT_HAS_PEREN((a) FN_MACRO)     -> <fails to compile>
#define CLEAT_HAS_PEREN(...) _CLEAT_HAS_PEREN_C(CLEAT_EXPAND(_CLEAT_HAS_PEREN_A __VA_ARGS__))
#define _CLEAT_HAS_PEREN_A(...) _CLEAT_HAS_PEREN_B
#define _CLEAT_OTHER_CLEAT_HAS_PEREN_A _CLEAT_NO_PEREN(
#define _CLEAT_OTHER_CLEAT_HAS_PEREN_B _CLEAT_YES_PEREN(
#define _CLEAT_HAS_PEREN_C(...) CLEAT_EXPAND_CAT(_CLEAT_OTHER, __VA_ARGS__) )
#define _CLEAT_NO_PEREN(...) FALSE
#define _CLEAT_YES_PEREN(...) CLEAT_NOT(CLEAT_IS_THING(__VA_ARGS__))

// same as CLEAT_COUNT, except always reports at least 1 item and is not bothered by function-like macros at the end
#define CLEAT_COUNT_AT_LEAST_1(...) CLEAT_EXPAND_CALL(_CLEAT_GEN_COUNT, __VA_ARGS__, _CLEAT_GEN_COUNT_NUMBERS)

// expands to the number of arguments; empty arguments are counted; zero arguments is handeled correctly
// NOTE: can not end with a function-like macro!
#define CLEAT_COUNT(...) CLEAT_IF_ELSE \
    (CLEAT_IS_THING(__VA_ARGS__)) \
    (CLEAT_COUNT_AT_LEAST_1(__VA_ARGS__)) \
    (0)

// a closure (sometimes called a lamda expression) is a chunk of functionality that can be passed around like data
// what we're calling a closure here is a function-like macro be bundled with zero or more captured values
// CLEAT_CLOSURE_INVOKE() expands the contined macro with the captured values as well as zero or more additional arguments
// the closure macro receives first the captured values, then the arguments supplied to CLEAT_CLOSURE_INVOKE()
// the closure macro and all captured values need to be surounded by parentheses
// CLEAT_CLOSURE_INVOKE((MACRO))             -> MACRO()
// CLEAT_CLOSURE_INVOKE((MACRO, a))          -> MACRO(a)
// CLEAT_CLOSURE_INVOKE((MACRO), x)          -> MACRO(x)
// CLEAT_CLOSURE_INVOKE((MACRO, a, b), x, y) -> MACRO(a, b, x, y)
#define CLEAT_CLOSURE_INVOKE(...) \
    /* check that the closure is enclosed by parentheses */ \
    /* (not doing this for closures with no captured values is an easy mistake) */ \
    CLEAT_IF_ELSE(CLEAT_HAS_PEREN(_CLEAT_CLOSURE_INVOKE_EXTRACT_FIRST(__VA_ARGS__))) \
    ( \
        _CLEAT_CLOSURE_INVOKE_EXPAND_CALL ( \
            /* extract the closure, and from the closure extract the function-like macro at the start */ \
            _CLEAT_CLOSURE_INVOKE_EXTRACT_FIRST _CLEAT_CLOSURE_INVOKE_EXTRACT_FIRST(__VA_ARGS__), \
            CLEAT_EXPAND( \
                /* extract the closure, and from the closure extract it's captured values */ \
                _CLEAT_CLOSURE_INVOKE_EXTRACT_ARGS _CLEAT_CLOSURE_INVOKE_EXTRACT_FIRST(__VA_ARGS__) \
                /* if there are both captured values and arguments, we need a comma between them */ \
                CLEAT_IF_NOT(CLEAT_OR( \
                    CLEAT_EQ(CLEAT_COUNT_AT_LEAST_1(__VA_ARGS__), 1), \
                    CLEAT_EQ(CLEAT_COUNT_AT_LEAST_1 _CLEAT_CLOSURE_INVOKE_EXTRACT_FIRST(__VA_ARGS__), 1))) \
                (,) \
                /* extract the arguments */ \
                _CLEAT_CLOSURE_INVOKE_EXTRACT_ARGS(__VA_ARGS__) \
            ) \
        ) \
    ) \
    ("Closures need to be enclosed by parentheses, dumbass")
#define _CLEAT_CLOSURE_INVOKE_EXTRACT_FIRST(first, ...) first
#define _CLEAT_CLOSURE_INVOKE_EXTRACT_ARGS(macro, ...) __VA_ARGS__
// everything blows up if we use the normal EXPAND_CALL here. We're probably somehow calling it recursivly
#define _CLEAT_CLOSURE_INVOKE_EXPAND_CALL(macro, ...) macro(__VA_ARGS__)

// repeats closure count number of times (see CLEAT_CLOSURE_INVOKE)
// closure is invoked with the current index as it's argument (in addition to any captured values)
// CLEAT_REPEAT((FOO), 3)    -> FOO(0) FOO(1) FOO(2)
// CLEAT_REPEAT((FOO), 0)    -> [empty]
#define CLEAT_REPEAT(closure, count) CLEAT_EXPAND_CAT(_CLEAT_GEN_MAP_, count) \
    ((_CLEAT_REPEAT_FUNC, closure), CLEAT_ORDER_FORWARD, 0, CLEAT_INC_,)
// we have to duplicate CLEAT_CLOSURE_INVOKE here because no recursion
#define _CLEAT_REPEAT_FUNC(closure, unused_item, index) \
    CLEAT_IF_ELSE(CLEAT_HAS_PEREN(closure)) \
    ( \
        _CLEAT_REPEAT_FUNC_EXPAND_CALL ( \
            _CLEAT_CLOSURE_INVOKE_EXTRACT_FIRST closure, \
            CLEAT_EXPAND( \
                _CLEAT_CLOSURE_INVOKE_EXTRACT_ARGS closure \
                CLEAT_IF_NOT(CLEAT_EQ(CLEAT_COUNT_AT_LEAST_1 closure, 1)) (,) \
                index \
            ) \
        ) \
    ) \
    ("Closures need to be enclosed by parentheses, dumbass")
#define _CLEAT_REPEAT_FUNC_EXPAND_CALL(macro, ...) macro(__VA_ARGS__)

// same as CLEAT_REPEAT, but in reverse order
// CLEAT_REPEAT_DOWN((FOO), 3)    -> FOO(2) FOO(1) FOO(0)
#define CLEAT_REPEAT_DOWN(closure, count) CLEAT_EXPAND_CAT(_CLEAT_GEN_MAP_, count) \
    ((_CLEAT_REPEAT_FUNC, closure), CLEAT_ORDER_FORWARD, CLEAT_EXPAND_CAT(CLEAT_DEC_, count), CLEAT_DEC_,)

// applies the given closure to all additional arguments
// closure is given the item and the index, see CLEAT_CLOSURE_INVOKE for details
// CLEAT_MAP((FOO), a, b, c)   -> FOO(a, 0) FOO(b, 1) FOO(c, 2)
#define CLEAT_MAP(closure, ...) CLEAT_EXPAND_CAT(_CLEAT_GEN_MAP_, CLEAT_COUNT(__VA_ARGS__)) \
    (closure, CLEAT_ORDER_FORWARD, 0, CLEAT_INC_, __VA_ARGS__)

// same as CLEAT_MAP, but indexes count down instead of up
// CLEAT_MAP_DOWN((FOO), a, b, c)   -> FOO(a, 2) FOO(b, 1) FOO(c, 0)
#define CLEAT_MAP_DOWN(closure, ...) CLEAT_EXPAND_CAT(_CLEAT_GEN_MAP_, CLEAT_COUNT(__VA_ARGS__)) \
    (closure, CLEAT_ORDER_FORWARD, CLEAT_EXPAND_CAT(CLEAT_DEC_, CLEAT_COUNT(__VA_ARGS__)), CLEAT_DEC_, __VA_ARGS__)

// same as CLEAT_MAP but reverses the order of the items
// CLEAT_MAP_REVERSE((FOO), a, b, c)   -> FOO(c, 0) FOO(b, 1) FOO(a, 2)
#define CLEAT_MAP_REVERSE(closure, ...) CLEAT_EXPAND_CAT(_CLEAT_GEN_MAP_, CLEAT_COUNT(__VA_ARGS__)) \
    (closure, CLEAT_ORDER_BACKWARD, CLEAT_EXPAND_CAT(CLEAT_DEC_, CLEAT_COUNT(__VA_ARGS__)), CLEAT_DEC_, __VA_ARGS__)

// same as CLEAT_MAP but reverses the order of items and counts down
// pairs the same indexes with the same items as CLEAT_MAP()
// CLEAT_MAP_REVERSE_DOWN((FOO), a, b, c)   -> FOO(c, 2) FOO(b, 1) FOO(a, 0)
#define CLEAT_MAP_REVERSE_DOWN(closure, ...) CLEAT_EXPAND_CAT(_CLEAT_GEN_MAP_, CLEAT_COUNT(__VA_ARGS__)) \
    (closure, CLEAT_ORDER_BACKWARD, 0, CLEAT_INC_, __VA_ARGS__)

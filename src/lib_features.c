// Generated from tsc's getScriptTargetFeatures (src/compiler/utilities.ts) by lib_features.py: which lib adds
// which member of which global type. Regenerate rather than edit
#include "lib_features.h"

static LibFeature const lib_features[] = {
    { "Array", "es2015", (char const* const[]){ "find", "findIndex", "fill", "copyWithin", "entries", "keys", "values" }, 7 },
    { "Array", "es2016", (char const* const[]){ "includes" }, 1 },
    { "Array", "es2019", (char const* const[]){ "flat", "flatMap" }, 2 },
    { "Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Iterator", "es2015", nullptr, 0 },
    { "AsyncIterator", "es2015", nullptr, 0 },
    { "ArrayBuffer", "es2024", (char const* const[]){ "maxByteLength", "resizable", "resize", "detached", "transfer", "transferToFixedLength" }, 6 },
    { "Atomics", "es2017", (char const* const[]){ "add", "and", "compareExchange", "exchange", "isLockFree", "load", "or", "store", "sub", "wait", "notify", "xor" }, 12 },
    { "Atomics", "es2024", (char const* const[]){ "waitAsync" }, 1 },
    { "SharedArrayBuffer", "es2017", (char const* const[]){ "byteLength", "slice" }, 2 },
    { "SharedArrayBuffer", "es2024", (char const* const[]){ "growable", "maxByteLength", "grow" }, 3 },
    { "AsyncIterable", "es2018", nullptr, 0 },
    { "AsyncIterableIterator", "es2018", nullptr, 0 },
    { "AsyncGenerator", "es2018", nullptr, 0 },
    { "AsyncGeneratorFunction", "es2018", nullptr, 0 },
    { "RegExp", "es2015", (char const* const[]){ "flags", "sticky", "unicode" }, 3 },
    { "RegExp", "es2018", (char const* const[]){ "dotAll" }, 1 },
    { "RegExp", "es2024", (char const* const[]){ "unicodeSets" }, 1 },
    { "Reflect", "es2015", (char const* const[]){ "apply", "construct", "defineProperty", "deleteProperty", "get", "getOwnPropertyDescriptor", "getPrototypeOf", "has", "isExtensible", "ownKeys", "preventExtensions", "set", "setPrototypeOf" }, 13 },
    { "ArrayConstructor", "es2015", (char const* const[]){ "from", "of" }, 2 },
    { "ArrayConstructor", "esnext", (char const* const[]){ "fromAsync" }, 1 },
    { "ObjectConstructor", "es2015", (char const* const[]){ "assign", "getOwnPropertySymbols", "keys", "is", "setPrototypeOf" }, 5 },
    { "ObjectConstructor", "es2017", (char const* const[]){ "values", "entries", "getOwnPropertyDescriptors" }, 3 },
    { "ObjectConstructor", "es2019", (char const* const[]){ "fromEntries" }, 1 },
    { "ObjectConstructor", "es2022", (char const* const[]){ "hasOwn" }, 1 },
    { "ObjectConstructor", "es2024", (char const* const[]){ "groupBy" }, 1 },
    { "NumberConstructor", "es2015", (char const* const[]){ "isFinite", "isInteger", "isNaN", "isSafeInteger", "parseFloat", "parseInt" }, 6 },
    { "Math", "es2015", (char const* const[]){ "clz32", "imul", "sign", "log10", "log2", "log1p", "expm1", "cosh", "sinh", "tanh", "acosh", "asinh", "atanh", "hypot", "trunc", "fround", "cbrt" }, 17 },
    { "Math", "esnext", (char const* const[]){ "f16round" }, 1 },
    { "Map", "es2015", (char const* const[]){ "entries", "keys", "values" }, 3 },
    { "MapConstructor", "es2024", (char const* const[]){ "groupBy" }, 1 },
    { "Set", "es2015", (char const* const[]){ "entries", "keys", "values" }, 3 },
    { "Set", "esnext", (char const* const[]){ "union", "intersection", "difference", "symmetricDifference", "isSubsetOf", "isSupersetOf", "isDisjointFrom" }, 7 },
    { "PromiseConstructor", "es2015", (char const* const[]){ "all", "race", "reject", "resolve" }, 4 },
    { "PromiseConstructor", "es2020", (char const* const[]){ "allSettled" }, 1 },
    { "PromiseConstructor", "es2021", (char const* const[]){ "any" }, 1 },
    { "PromiseConstructor", "es2024", (char const* const[]){ "withResolvers" }, 1 },
    { "Symbol", "es2015", (char const* const[]){ "for", "keyFor" }, 2 },
    { "Symbol", "es2019", (char const* const[]){ "description" }, 1 },
    { "WeakMap", "es2015", (char const* const[]){ "entries", "keys", "values" }, 3 },
    { "WeakSet", "es2015", (char const* const[]){ "entries", "keys", "values" }, 3 },
    { "String", "es2015", (char const* const[]){ "codePointAt", "includes", "endsWith", "normalize", "repeat", "startsWith", "anchor", "big", "blink", "bold", "fixed", "fontcolor", "fontsize", "italics", "link", "small", "strike", "sub", "sup" }, 19 },
    { "String", "es2017", (char const* const[]){ "padStart", "padEnd" }, 2 },
    { "String", "es2019", (char const* const[]){ "trimStart", "trimEnd", "trimLeft", "trimRight" }, 4 },
    { "String", "es2020", (char const* const[]){ "matchAll" }, 1 },
    { "String", "es2021", (char const* const[]){ "replaceAll" }, 1 },
    { "String", "es2022", (char const* const[]){ "at" }, 1 },
    { "String", "es2024", (char const* const[]){ "isWellFormed", "toWellFormed" }, 2 },
    { "StringConstructor", "es2015", (char const* const[]){ "fromCodePoint", "raw" }, 2 },
    { "DateTimeFormat", "es2017", (char const* const[]){ "formatToParts" }, 1 },
    { "Promise", "es2015", nullptr, 0 },
    { "Promise", "es2018", (char const* const[]){ "finally" }, 1 },
    { "RegExpMatchArray", "es2018", (char const* const[]){ "groups" }, 1 },
    { "RegExpExecArray", "es2018", (char const* const[]){ "groups" }, 1 },
    { "Intl", "es2018", (char const* const[]){ "PluralRules" }, 1 },
    { "NumberFormat", "es2018", (char const* const[]){ "formatToParts" }, 1 },
    { "SymbolConstructor", "es2020", (char const* const[]){ "matchAll" }, 1 },
    { "SymbolConstructor", "esnext", (char const* const[]){ "metadata", "dispose", "asyncDispose" }, 3 },
    { "DataView", "es2020", (char const* const[]){ "setBigInt64", "setBigUint64", "getBigInt64", "getBigUint64" }, 4 },
    { "DataView", "esnext", (char const* const[]){ "setFloat16", "getFloat16" }, 2 },
    { "BigInt", "es2020", nullptr, 0 },
    { "RelativeTimeFormat", "es2020", (char const* const[]){ "format", "formatToParts", "resolvedOptions" }, 3 },
    { "Int8Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Int8Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Uint8Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Uint8Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Uint8ClampedArray", "es2022", (char const* const[]){ "at" }, 1 },
    { "Uint8ClampedArray", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Int16Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Int16Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Uint16Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Uint16Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Int32Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Int32Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Uint32Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Uint32Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Float16Array", "esnext", nullptr, 0 },
    { "Float32Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Float32Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Float64Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "Float64Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "BigInt64Array", "es2020", nullptr, 0 },
    { "BigInt64Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "BigInt64Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "BigUint64Array", "es2020", nullptr, 0 },
    { "BigUint64Array", "es2022", (char const* const[]){ "at" }, 1 },
    { "BigUint64Array", "es2023", (char const* const[]){ "findLastIndex", "findLast", "toReversed", "toSorted", "toSpliced", "with" }, 6 },
    { "Error", "es2022", (char const* const[]){ "cause" }, 1 },
};

char const* lib_feature_of_global(csview name) {
    for (u32 i = 0; i < sizeof(lib_features) / sizeof(lib_features[0]); i++) {
        if (csview_equals(name, lib_features[i].type)) return lib_features[i].lib;
    }
    return nullptr;
}

char const* lib_feature_of_member(csview type, csview member) {
    for (u32 i = 0; i < sizeof(lib_features) / sizeof(lib_features[0]); i++) {
        if (!csview_equals(type, lib_features[i].type)) continue;
        for (u32 j = 0; j < lib_features[i].member_count; j++) {
            if (csview_equals(member, lib_features[i].members[j])) return lib_features[i].lib;
        }
    }
    return nullptr;
}

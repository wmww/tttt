//// [test/cases/ttttMappedTypeExtraMembersEmit.ts] ////

//// [ttttMappedTypeExtraMembersEmit.ts]
// A mapped type's declaration emit is its `[K in T]: X` alone: the members after it are only tsgo's parser's recovery
// (TS7061), no nodes of the mapped type (mutation seed 20261102, 02a144d6b's leftover)
export type M = { [K in string]: 1; x: 2 };
export declare const m: { [K in "a"]: K; y: 1 };


//// [ttttMappedTypeExtraMembersEmit.js]
export {};


//// [ttttMappedTypeExtraMembersEmit.d.ts]
export type M = {
    [K in string]: 1;
};
export declare const m: {
    [K in "a"]: K;
};

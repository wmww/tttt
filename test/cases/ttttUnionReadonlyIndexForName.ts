// @strict: true
// A union's property through a member's index signature takes the signature that applies to the name (tsc's
// getApplicableIndexInfoForName in createUnionOrIntersectionProperty): a numeric name reaches a readonly number index
// signature, so the write is TS2540 (bun 99-V4 getTypeOfSymbolWithDeferredType__05)
declare const v1: { readonly 0: "p" } | { 0: "q" };
v1[0] = "q";
declare const v2: { 0: "q" } | { readonly [k: number]: null };
v2[0] = "q";
declare const v3: [boolean, ...number[]] | { readonly [k: number]: null };
v3[0] = null;
declare const v4: { 0: "q" } | [boolean];
v4[0] = "q";
declare const v5: { a: 1 } | { readonly [k: string]: 1 };
v5.a = 1;

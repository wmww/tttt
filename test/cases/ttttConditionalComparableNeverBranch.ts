// A conditional source is comparable through the union of its branches, where a never branch is no member: `null as
// (T extends number ? string : never)` is TS2352 (mutation testing, simplifyingConditionalWithInteriorConditionalIsRelated)
// @strict: true
function f<T>() {
    type One = T extends string ? string : string;
    type A = T extends number ? One : never;
    type B = T extends number ? string : never;
    type C = T extends number ? One : string;
    const x1 = null as A;
    const x2 = null as B;
    const x3 = null as One;
    const x4 = null as C;
}

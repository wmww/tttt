// @strict: true
// A binding element's default that is a mutual strict subtype of the element's type: the subtype reduction's union
// order decides which one removeSubtypes drops. `B & object` (B empty) and a fresh `{}` are mutual subtypes and
// tsgo keeps `{}`; with a weak W the fresh `{}` is no supertype of `W & object`, which stays.
// Seed 20261118, nonPrimitiveAndEmptyObject delete-stmt
interface B {}
interface W { w?: string }
declare const bo: B & object;
declare const wo: W & object;
declare const b: B;
var a1 = [bo, {}];
var a2 = [{}, bo];
var a3 = [wo, {}];
var a4 = [b, {}];
declare const foo: { p?: B, q?: W & object, r?: B & object };
const { p = {}, q = {}, r = {} } = foo;
r.barProp;
q.barProp;

// @strict: true
// tsc: all errors (Record<any, any> is { [x: string]: any }, and primitives never get the any-index pass)
// tttt: t1/t2 accepted (mapped type over `any` kept unexpanded), A/B = 1
declare let s: "test1";
let t1: Record<any, any> = s;
let t2: {[P in any]: any} = s;
type A = "test1" extends Record<any, any> | undefined ? 1 : 2;
type B = string extends {[k: string]: any} ? 1 : 2;
declare const a: A;
declare const b: B;

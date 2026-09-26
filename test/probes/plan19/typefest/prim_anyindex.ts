// @strict: true
type A = "test1" extends Record<any, any> ? 1 : 2;
type B = string extends {[k: string]: any} ? 1 : 2;
type C = 42 extends Record<string, any> | undefined ? 1 : 2;
declare const a: A;
declare const b: B;
declare const c: C;
const d: Record<string, any> = "x";

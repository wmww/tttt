// @strict: true
type StrictEq<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends <T>() => T extends (R & T) | T ? true : false ? true : false;
type Eq<A, B> = (<T>() => T extends A ? 1 : 2) extends (<T>() => T extends B ? 1 : 2) ? true : false;
interface I1 { name: string; sub?: I1[] }
type C1 = { name: string; sub?: C1[] };
const a: StrictEq<C1, I1> = true;
const b: Eq<C1, I1> = true;
interface I2 { name: string; sub: I2 }
type C2 = { name: string; sub: C2 };
const c: StrictEq<C2, I2> = true;
const d: Eq<C2, I2> = true;

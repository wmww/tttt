// @strict: true
// @exactOptionalPropertyTypes: true
declare const sym: unique symbol;
type X = typeof sym | undefined | { [sym]?: never };
type CK2<Base, Condition> = { [Key in keyof Base]-?: Base[Key] extends Condition ? Key : never };
type R = CK2<{ s: string; o: { a: string } }, X>;
declare const r: R;
const r1: { s: 1, o: 1 } = r;
type R2 = { s: string; o: { a: string } }["s"] extends X ? 1 : 2;

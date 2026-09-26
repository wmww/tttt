// @strict: true
type O = { condition?: 'extends' | 'equality'; };
type AC<T, C, Op extends O> = Op['condition'] extends 'equality' ? 1 : T extends C ? true : false;
type R<T, C, Op extends O = {}> = { [K in keyof T]: AC<T[K], C, Op> extends true ? T[K] : T[K] extends object ? R<T[K], C, Op> : 0 };
declare const a: R<{ o: { s: string } }, string>;
const b: { o: { s: string } } = a;
declare const c: AC<string, string, {}>;

// @strict: true
interface OE { [key: string]: number | string }
type A = Record<string, number> & OE;
type B = Record<string, string> & OE;
declare const u: A | B;
declare const k: any;
const v = u[k];
declare const w: A;
const v2 = w[k];
declare const x: { a: number } & { b: string };
const v3 = x[k];
declare const y: { a: number } | ({ b: string } & { [k: string]: string });
const v4 = y[k];

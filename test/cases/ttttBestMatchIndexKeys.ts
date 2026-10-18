// findMostOverlappyType counts the member names a source's index signature keys too (keyof G & keyof any[]): a string
// index source elaborates against `any[]` in `any[] | Promise<any[]>` (docmost's slash menu)
// @strict: true
// @target: es2017
type G = { [category: string]: number[] };
declare const f: () => G;
const g: () => any[] | Promise<any[]> = f;
declare const h: G;
const i: any[] | Promise<any[]> = h;
const j: string[] | Promise<string[]> = h;

// origin: npm @point0/compiler@0.4.2 https://github.com/1gr14/point0 packages/compiler/tsconfig.bench.json seed 20261106
// flip: no
// A binding element with a default under a written type loses undefined and void alike (tsc's getNonUndefinedType is
// getTypeWithFacts(NEUndefined), and void's facts are undefined's): `{ input = {} }: { input: X | undefined | void }`
// is X. A parameter's own default keeps void (tsc's parameter initializer only removes undefined)
// @strict: true
type InputRaw = { a?: string };
export function f({ input = {} }: { input: InputRaw | undefined | void }) {
    const x: InputRaw = input;
}
export function g(input: InputRaw | void = {}) {
    const x: InputRaw = input;
}
export function h([v = 1]: [number | void]) {
    const n: number = v;
}
declare const o: { p: string | void };
const { p = "x" } = o;
export const q: string = p;

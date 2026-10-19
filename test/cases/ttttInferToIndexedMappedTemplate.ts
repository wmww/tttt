// @strict: true
// An object source infers to an indexed access through its apparent type (tsc's inferFromTypes falls through to
// inferFromObjectTypes): `FnMap<H>[T]`, FnMap mapped over a non-generic key, is `(item: DataMap<H>[T], orig: H) => T`
// there, so a contextual return type infers T and H (lightweight-charts' getSeriesPlotRowCreator)
interface DataMap<H = number> { A: { a: H }; B: { b: H } }
type FnMap<H> = { [T in keyof DataMap]: (item: DataMap<H>[T], orig: H) => T };
declare function wrap<T extends keyof DataMap, H>(fn: unknown): FnMap<H>[T];

export function make<H>() {
    const m: FnMap<H> = { A: wrap(1), B: wrap(2) };
    return m;
}

type Plain = { A: (item: { a: number }) => "A"; B: (item: { b: number }) => "B" };
declare function wrap2<T extends keyof Plain>(): Plain[T];
const y: Plain["A"] = wrap2();

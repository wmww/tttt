// @strict: true
// A homomorphic mapped type over a generic modifiers type whose constraint is a union of intersections has the
// properties every member has, listed from the first member's parts (tsc's getPropertiesOfType of the union):
// `Readonly<OMap[T]>` with `OMap[T]` a union of `X & Common` (lightweight-charts' series options)
interface Common { hitTestTolerance: number; lineWidth?: number }
type SeriesOptions<T> = T & Common;
interface BarStyle { up: string; shared: string }
interface CandleStyle { wick: string; shared: string }
interface OMap { Bar: SeriesOptions<BarStyle>; Candlestick: SeriesOptions<CandleStyle> }

function f<T extends "Bar" | "Candlestick">(r: Readonly<OMap[T]>, p: Partial<OMap[T]>) {
    const a = r.hitTestTolerance;
    const b = r.shared;
    const c = p.lineWidth;
    const d = r.up;
    return [a, b, c, d];
}

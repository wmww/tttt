// @strict: true
// @noEmit: true
// @lib: es2015
// tsc: clean. Two conditional types with `infer` relate after inferring the source's `infer`s from the target's extends
// type (structuredTypeRelatedToWorker): webpack's StatsFactory `Caches<H>` with H constrained by such a conditional
interface Box<T> { t: T }
type Caches<T> = Map<string, T[]>;
declare function g<HM extends Box<any>, H extends (HM extends Box<infer H> ? H : never)>(hm: HM, c: Caches<H>): H[];
function f<HM extends Box<any>, H extends (HM extends Box<infer H> ? H : never)>(hm: HM, c: Caches<H>) {
    g(hm, c);
}
type C1<HM> = HM extends Box<infer H> ? H : never;
type C2<HM> = HM extends Box<infer H> ? H : never;
function k<HM>(a: C1<HM>): C2<HM> { return a; }

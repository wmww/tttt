// @strict: true
type Assume<T, U> = T extends U ? T : U;
interface Cfg { execute: unknown }
declare class PQ<T extends Cfg> { t: T; m(): T["execute"] }
function f<T extends Cfg>(p: PQ<T>) {
    return p as PQ<Assume<T, Cfg>>;
}
function g<T extends Cfg>(t: T) {
    return t as Assume<T, Cfg>;
}

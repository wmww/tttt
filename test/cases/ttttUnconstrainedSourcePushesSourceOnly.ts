// @strict: true
// An indexed access or type parameter without a constraint relates as `unknown`, pushing only the source on the deep
// nesting stacks (tsc's RecursionFlags.Source): pushing the target too at each step of `T[P][string]` ->
// `T[keyof T][string]` -> unknown made the target conditional deeply nested, a Maybe taken as related
type QP<T> = { [P in keyof T]?: (T[P] extends Array<infer U> ? 1 : QP<T[P]>) | (() => string) };
type DP<T> = { [P in keyof T]?: T[P] extends Array<infer U> ? 1 : DP<T[P]> | T[P] };
function f<T>(x: DP<T>) {
    const y: QP<T> = x;
}
type DPA<T> = { [P in keyof T]?: T[P] extends Array<infer U> ? Array<DPA<U>> : DPA<T[P]> | T[P] };
type QPA<T> = { [P in keyof T]?: (T[P] extends Array<infer U> ? Array<QPA<U>> : QPA<T[P]>) | (() => string) };
function g<E>(r: DPA<E>) {
    const q: QPA<E> = r;
}

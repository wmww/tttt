// @strict: true
// @target: es2022
function f<T extends object>(values: T) {
    const v = Object.values(values);
    const o: object = values;
    const r: { [s: string]: unknown } = o;
    const r2: { [s: string]: unknown } = values;
    return v;
}

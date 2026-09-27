// @strict: true
// @exactOptionalPropertyTypes: true
// jsonify:57: the properties an object literal in a union is padded with are tsc's missingType under
// exactOptionalPropertyTypes (`a?: never` in the declared-type view), which index signatures accept
const arr = [{}, {a: 'hi'}];
const x = true ? {a: 1} : {};
const r: {[k: string]: number} = x;
const read = x.a;
const r3: {a?: number} = x;
declare const q: {a?: undefined};
const r2: {[k: string]: number} = q;
const s = { ...x };

// TS7053's elaboration for a union index is its first member's (tsc distributes the index): 'string' of 'string | symbol'
// @strict: true
declare const o: { [k: string]: number };
declare const k: string | symbol;
o[k];
declare const p: { a: 1 };
declare const k2: "a" | "b" | number;
p[k2];
declare const k3: number | "a";
p[k3];
function f<T>(t: T, key: string | symbol) { t[key]; }
declare const q: {};
declare const k4: symbol | string;
q[k4];

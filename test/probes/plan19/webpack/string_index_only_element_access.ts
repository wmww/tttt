// @strict: true
declare const o: { [p: string]: number };
const a = o.toString;
const b = o["toString"];
declare const p: { x: number };
const c = p["toString"];

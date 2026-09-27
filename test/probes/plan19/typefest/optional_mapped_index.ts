// @strict: true
type O = {[K in string]?: number};
const o: O = {a: undefined};
declare const x: O;
const v = x.a;
type J = string | {[K in string]?: J};
const j: J = {a: undefined};

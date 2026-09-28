// @strict: true
// @target: es2022
const T = Symbol("t");
type State = boolean | typeof T;
declare const s: State;
let m = s;
const z: State = m;

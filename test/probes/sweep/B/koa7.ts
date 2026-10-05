// @strict: true
type A = any;
interface DS extends A {}
interface IX { [k: string]: any }
function f<T>(t: T) { const d: DS = t; const e: IX = t; const g: {} = t; }
declare const ds: DS;
const n: number = ds.foo;

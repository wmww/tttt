// @strict: true
type A = any;
interface DS extends A {}
interface DS2 extends A { x?: number }
declare const u: unknown;
const d: DS = u;
const d2: DS2 = u;
const d3: DS = null;
const d4: DS = 3;

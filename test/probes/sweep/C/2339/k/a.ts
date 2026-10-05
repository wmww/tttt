type Ext = any;
interface S extends Ext { a: number }
declare const s: S;
s.b = "";
const n: number = s.zz;
interface S2 extends Ext {}
declare const s2: S2 & { c: boolean };
s2.d;

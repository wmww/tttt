// A spread keeps an accessor pair whose setter takes another type as that symbol (tsc's getSpreadSymbol), and
// widening keeps it (getWidenedProperty): writes go by the setter
const o1 = { get d(): number { return 1 }, set d(v: string | number) {} };
const sp = { ...o1 }; sp.d = "x";
const s1: string = sp;
interface I { get d(): number; set d(v: string | number); r: 1 }
declare const i: I;
const { r, ...rest } = i; rest.d = "x";
const sp2 = { ...i }; sp2.d = "y";
const sp3 = { ...i, d: 1 };
const sp4 = { a: 1, ...(i as Partial<I>) };
const sentinel: number = "";

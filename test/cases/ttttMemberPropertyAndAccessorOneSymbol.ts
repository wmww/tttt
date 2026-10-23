// @strict: true
// A property and accessors of one name in a class, interface or type literal (TS2300) are one symbol in tsgo, a
// property written as it is read (getWriteTypeOfSymbol of a SymbolFlags.Property)
class C2 { get a(): number { return 1 } set a(v: string | number) {} a = 2; }
new C2().a = "s";
declare const t1: { get a(): number; set a(v: string | number); a: number };
t1.a = "s";
const p1: boolean = t1;
interface I2 { get a(): number; set a(v: string | number); a: number }
declare const i2: I2;
i2.a = "s";
class C1 { a = 2; get a(): number { return 1 } set a(v: string | number) {} }
new C1().a = "s";
const sentinel: number = "";

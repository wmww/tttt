// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A1 { [+2] = 1; }
const m1: null = null! as keyof A1;
interface I2 { [+2]: 1; [-0]: 2; [-3]: 3 }
const m2: null = null! as keyof I2;
type T3 = { [+2]: 1 };
const m3: null = null! as keyof T3;
const o4 = { [+2]: 1 };
const m4: null = null! as keyof typeof o4;
type M5 = { [K in keyof I2]: K }; const m5: null = null! as M5;
declare const a1: A1; a1[2]; a1["+2"]; a1["2"];
declare const i2: I2; i2[2]; i2["+2"]; i2[0]; i2["-0"]; i2[-0]; i2[-3]; i2["-3"];
type P6 = Pick<I2, 2>; type P7 = Pick<I2, "+2">; type P8 = I2[2]; type P9 = I2["+2"];
const k10: keyof I2 = 2; const k11: keyof I2 = "+2"; const k12: keyof I2 = 0; const k13: keyof I2 = "-0";
const sentinel: number = "";

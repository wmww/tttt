// @strict: true
// An instantiated method whose class type parameters only its type parameters' defaults mention prints the defaults
// instantiated (tsc's isTypeParameterPossiblyReferenced sees them, the signature is instantiated)
declare class Ext<Options = any, Storage = any> {
  extend<EO = Options, ES = Storage>(c?: EO): Ext<EO, ES>;
}
declare class Mk<Options = any, Storage = any> {
  extend<EO = Options, ES = Storage>(c?: ES): Mk<EO, ES>;
}
declare const e: Ext<string, number>;
const n1: number = e.extend;
declare const u: Ext | Mk;
const n2: number = u.extend;
u.extend(1)(2);
const n3: number = e.extend();
declare class Ext2<Options = any, Storage = any> {
  extend<EO extends Options, ES = Storage>(c?: EO): Ext2<EO, ES>;
  ext2<EO = Options>(c?: EO): Ext2<EO>;
}
declare const e2: Ext2<string, number>;
const m1: number = e2.extend;
const m2: number = e2.ext2;

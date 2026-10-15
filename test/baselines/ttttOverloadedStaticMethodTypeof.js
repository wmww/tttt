//// [test/cases/ttttOverloadedStaticMethodTypeof.ts] ////

//// [ttttOverloadedStaticMethodTypeof.ts]
// An overloaded static method's type is written `typeof C.m` like a single-signature one's (tsgo's
// shouldWriteTypeOfFunctionSymbol: isStaticMethodSymbol), not as its signatures (zod's recordType)
export class ZodRecord<K, V> {
  static create<V>(v: V): ZodRecord<string, V>;
  static create<K, V>(k: K, v: V): ZodRecord<K, V>;
  static create(a: any, b?: any): any { return null; }
  static single(x: number): number { return x; }
  m(x: number) { return x; }
}
const recordType = ZodRecord.create;
const singleType = ZodRecord.single;
export { recordType, singleType };
export const inst = new ZodRecord<1, 2>().m;


//// [ttttOverloadedStaticMethodTypeof.js]
// An overloaded static method's type is written `typeof C.m` like a single-signature one's (tsgo's
// shouldWriteTypeOfFunctionSymbol: isStaticMethodSymbol), not as its signatures (zod's recordType)
export class ZodRecord {
    static create(a, b) { return null; }
    static single(x) { return x; }
    m(x) { return x; }
}
const recordType = ZodRecord.create;
const singleType = ZodRecord.single;
export { recordType, singleType };
export const inst = new ZodRecord().m;


//// [ttttOverloadedStaticMethodTypeof.d.ts]
export declare class ZodRecord<K, V> {
    static create<V>(v: V): ZodRecord<string, V>;
    static create<K, V>(k: K, v: V): ZodRecord<K, V>;
    static single(x: number): number;
    m(x: number): number;
}
declare const recordType: typeof ZodRecord.create;
declare const singleType: typeof ZodRecord.single;
export { recordType, singleType };
export declare const inst: (x: number) => number;

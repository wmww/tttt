// A pair that ran out of budget is cached as that: assigning it again reports TS2859 only, no TS2322 (tsc's
// recursiveTypeRelatedTo reports the cached overflow while explaining)
type Digits = '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9';
type T1 = `${Digits}${Digits}${Digits}${Digits}` | undefined;
type T2 = { a: string } | { b: number };
declare let y: T1 & T2; declare let z: T1 | T2;
z = y; z = y;
const sentinel: number = "";

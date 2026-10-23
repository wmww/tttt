// `unique` is an operator on the postfix type after it (tsc's parseTypeOperator): `unique symbol[]` is `unique (symbol[])`,
// TS1005 'symbol' expected, of an error type
declare const r2: unique symbol[];
let s2: string = r2;
declare const r3: unique symbol
[];
const sentinel: number = "";

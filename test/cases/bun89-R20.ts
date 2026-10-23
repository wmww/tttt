// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Options { a?: number }
declare const defaults: Options;
declare function take(o: Options): void;
// Valid in tsc: getSpreadSymbol makes a NEW symbol, without a value declaration, for a get-only accessor (it is read-only, the copy is not).
const o1: Options = { ...defaults, get extra() { return 1; } };
const o2: Options = { get extra() { return 1; }, ...defaults };
take({ ...defaults, get extra() { return 1; } });
const o3: { n: Options }[] = [{ n: { ...defaults, get extra() { return 1; } } }];
function ret(): Options { return { ...defaults, get extra() { return 1; } }; }
const o4: Readonly<Options> = { ...defaults, get extra() { return 1; } };
// another error in place of TS2353
const o5: Options = { ...{}, get extra() { return 1; } };
const o6: { b: number } = { ...defaults, get extra() { return 1; } };
// the two agree: TS2353 for a property, a method, a getter with a setter; none for a setter alone
const p1: Options = { ...defaults, extra: 1 };
const p2: Options = { ...defaults, extra() {} };
const p3: Options = { ...defaults, get extra() { return 1; }, set extra(v) {} };
const p4: Options = { ...defaults, set extra(v: number) {} };
const sentinel: number = "";

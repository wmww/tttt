// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noErrorTruncation": true } }
// @filename: S136.txt
a.ts(6,7): error TS2322: Type 'null' is not assignable to type '[[unknown]]'.
a.ts(7,7): error TS2322: Type 'null' is not assignable to type '[[unknown]]'.
a.ts(8,7): error TS2322: Type 'null' is not assignable to type '[[unknown]]'.
a.ts(10,7): error TS2322: Type 'null' is not assignable to type '[[unknown]]'.
a.ts(11,7): error TS2322: Type 'null' is not assignable to type '[[unknown]]'.
a.ts(12,7): error TS2322: Type 'null' is not assignable to type '[[unknown]]'.
a.ts(17,7): error TS2322: Type 'null' is not assignable to type '[[unknown]]'.
a.ts(18,7): error TS2322: Type 'null' is not assignable to type '[unknown]'.
a.ts(19,7): error TS2322: Type 'null' is not assignable to type '[unknown] | [[unknown]]'.
a.ts(24,7): error TS2322: Type 'null' is not assignable to type '[5 | 6 | [unknown]]'.
a.ts(25,7): error TS2322: Type 'null' is not assignable to type '6 | [unknown]'.
a.ts(26,7): error TS2322: Type 'null' is not assignable to type '6 | [unknown] | [5 | 6 | [unknown]]'.
a.ts(27,7): error TS2322: Type 'string' is not assignable to type 'number'.
// @filename: a.ts
// `getConditionalTypeInstantiation` calls `getConditionalType` for each member of the union it distributes over: that call reads
// no entry of `root.instantiations` and stores none. It shows where the result depends on when it is computed: here the mapped type
// has its members in place (empty) while they are resolved, so the inner `D<S1>` is `[unknown]` and the outer one `[[unknown]]`.
type S1 = { [k: string]: 1 }; type S2 = { [k: string]: 2 }; type S3 = { [k: string]: 3 };
type D<T> = T extends object ? [{ [P in keyof T]: D<T> }[keyof T]] : 2;
const d1: D<S1 | S2> = null; // the outer evaluations, not stored
const d2: D<S1> = null;      // the entry of the inner one
const d3: D<S1 | S3> = null; // computed again
type E<T> = T extends object ? [{ [P in keyof T]: E<T> }[keyof T]] : 2;
const e1: E<S1> = null;
const e2: E<S1 | S2> = null;
const e3: E<S2> = null;
// Through a tail call `F` is evaluated in the loop of `getConditionalType`: nothing is stored for it. A member is computed again.
type F<T> = T extends object ? [{ [P in keyof T]: F1<T> | F2<T> }[keyof T]] : 2;
type F1<T> = T extends object ? F<T> : 3;
type F2<T> = T extends object ? F<T> : 4;
const f1: F<S1> = null;
const f2: F2<S1> = null;
const f3: F2<S1 | S2> = null;
// Not a tail call: `H2<S1>` reads the entry that the inner `H<S1>` has stored. A member is computed again, with the entry of the outer one.
type H<T> = T extends object ? [{ [P in keyof T]: H1<T> | H2<T> }[keyof T]] : 2;
type H1<T> = T extends object ? H<T> | 5 : 3;
type H2<T> = T extends object ? H<T> | 6 : 4;
const h1: H<S1> = null;
const h2: H2<S1> = null;
const h3: H2<S1 | S2> = null;
const sentinel: number = '';

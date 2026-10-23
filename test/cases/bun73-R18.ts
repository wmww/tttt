// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Props = Readonly<{ x: number }>;
interface Box<T> { p: Props; v: T }
type GA<T> = { p: Props; t: T };
declare const b: Box<number>;
declare const u0: Props | Box<number>["p"]; const n0: never = u0;
declare const u1: [Props] | [typeof b.p]; const n1: never = u1;
declare const u2: [Props] | [Box<number>["p"]]; const n2: never = u2;
declare const u3: [Props] | [GA<number>["p"]]; const n3: never = u3;
declare const u4: Props & Box<number>["p"]; const n4: never = u4;
// right since batch 1: the return type of a generic function, an element of a generic tuple alias
declare function g<T>(t: T): Props; const r = g(1); declare const u5: [Props] | [typeof r]; const n5: never = u5;
type GT<T> = [Props, T]; declare const u6: [Props] | [GT<1>[0]]; const n6: never = u6;
const sentinel: number = "";

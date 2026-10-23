// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const w1: { readonly p: keyof W1 };
interface W1 { x(): void; [w1.p](): void }
declare const w2: { readonly p: keyof W2 };
interface W2 { x: 1; [w2.p]: number }
declare const w3: { readonly p: W3 extends { x: 1 } ? "a" : "b" };
interface W3 { x: 1; [w3.p]: number }
declare class H4 { readonly p: keyof W4 }
declare const h4: H4;
interface W4 { x(): void; [h4.p](): void }
// `p` is asked for before its declaration is checked: the two agree
w5.p;
declare const w5: { readonly p: keyof W5 };
interface W5 { x(): void; [w5.p](): void }
const sentinel: number = "";

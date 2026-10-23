// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(x: T): T;
declare const v1: (typeof f<string>);
const n1: number = v1;
declare const v2: { a: (typeof f<string>) };
const n2: number = v2;
type Q = (typeof f<string>) | null;
declare const v3: Q;
const n3: number = v3;
declare const v4: (typeof f<string>) | null;
const n4: number = v4;
declare const v5: (typeof f<string>)[];
const n5: number = v5;
declare const v6: ( /* c */ typeof f<string> );
const n6: number = v6;
function g(p: (typeof f<string>) | undefined) { const n7: number = p; }
let v8 = null! as (typeof f<number>);
const n8: number = v8;
const sentinel: number = "";

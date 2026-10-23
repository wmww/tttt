// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a01.ts
export {}; declare const dec: any;
async function f() { interface I { m(@await y: any): void } }
const sentinel: number = "";
// @filename: a02.ts
export {}; declare const dec: any;
async function f() { interface I { (@await y: any): void } }
const sentinel: number = "";
// @filename: a03.ts
export {}; declare const dec: any;
async function f() { interface I { new (@await y: any): void } }
const sentinel: number = "";
// @filename: a11.ts
export {}; declare const dec: any;
interface I { m(@await y: any): void }
const sentinel: number = "";
// @filename: a14.ts
export {}; declare const dec: any;
async function f() { interface I { set s(@await y: any) } }
const sentinel: number = "";
// @filename: a16.ts
export {}; declare const dec: any;
async function f() { interface I { m(a: (@await y: any) => void): void; n(a: new (@await y: any) => void): void } }
const sentinel: number = "";
// @filename: a17.ts
export {}; declare const dec: any;
async function f() { interface I { m(@await.x y: any): void; n(@await() y: any): void } }
const sentinel: number = "";
// @filename: a19.ts
export {}; declare const dec: any;
class K { static { interface I { m(@await y: any): void; n(@dec(await 1) y: any): void } } }
const sentinel: number = "";

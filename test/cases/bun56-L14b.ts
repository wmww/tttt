// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const dec: any;
async function f() {
  let x: new (@await y: any) => void;
  let x2: abstract new (a: any, @await.b y: any) => void;
  let x3: <T>(@await y: any) => void;
  let x4: { m(@await y: any): void; new (@await y: any): void };
}
const sentinel: number = "";

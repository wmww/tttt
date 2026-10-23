// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare const x: any;
const { [await]: a } = x;
interface I { [await]: 1 }
interface J { [await](): void; [await.x]: 1 }
const sentinel: number = "";

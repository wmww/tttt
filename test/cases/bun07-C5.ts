// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare const obj: { x: any; y: number };
(obj.x) = function () { return this.y; };
const sentinel: number = "";

// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const q1: (this: number) => void = function (this) {};
declare function ft<T>(cb: (this: T) => void, v: T): void;
ft(function (this) { return 1 }, 1);
const sentinel: number = "";

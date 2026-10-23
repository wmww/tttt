// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function o<T>(cb1: (x: T) => void, ob: { m(this: any): T }): void;
o(x => x.toFixed(), { m(this) { return 1 } });
const sentinel: number = "";

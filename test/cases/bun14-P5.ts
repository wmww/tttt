// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(...args: [(x: number) => void] | ([(x: string) => void] & { k: "a" } & { k: "b" })): void;
f(x => {});
const sentinel: number = "";

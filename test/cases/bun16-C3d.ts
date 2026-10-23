// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f8<T extends { 0?: (x: number) => void } & Iterable<(x: string) => void>>([a = x => {}]: T) {}
const sentinel: number = "";

// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Tail<T> = T extends (a: any, ...args: infer P) => any ? P : never;
type X1 = Tail<(...args: readonly [number, ...string[]]) => void>;
declare const ro: readonly string[];
const x1: X1 = ro;
const sentinel: number = "";

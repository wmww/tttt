// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let q3: import("./b").BFoo<"a",>;
let q5: typeof import("./b").bf<>;
const sentinel: number = "";
// @filename: b.ts
export interface BFoo<X extends string> { x: X }
export declare function bf<X extends string>(): X;

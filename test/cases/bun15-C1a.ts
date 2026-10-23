// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Tagged<K extends string> = Uppercase<string & { [P in K]: 1 }>;
function q<K extends string>(x: Tagged<K> extends Uppercase<infer U> ? U : never) { const y: { [P in K]: 1 } = x; }
const sentinel: number = "";

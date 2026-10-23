// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true, "noUnusedParameters": true } }
// @filename: a.ts
export class C<T> { private T = 1; m(x: T) { return x } }
export const sentinel: number = "";

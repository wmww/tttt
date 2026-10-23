// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedParameters": true } }
// @filename: a.ts
export interface I {
  /** {@link T} */
  p: (<T>() => void);
}
const sentinel: number = "";

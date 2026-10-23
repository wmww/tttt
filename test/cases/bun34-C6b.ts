// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true, "noUnusedParameters": true } }
// @filename: a.ts
class Out {}
export interface I {
  /** {@link Out} */
  p: (<Out>() => void);
}
const sentinel: number = ""; sentinel;

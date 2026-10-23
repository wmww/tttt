// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true, "noUnusedParameters": true } }
// @filename: a.ts
export class K<T> {
  /** {@link T} */
  static p = 1
}
export const sentinel: number = "";

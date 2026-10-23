// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
export function k(): number {
  if (Math.random() > 0.5) {
    const limit = 10;
    /** At most {@link limit}. */
    const x = 1;
    return x;
  }
  for (const item of [1]) {
    /** See {@link item}. */
    return 1;
  }
  return 0;
}
export const sentinel: number = "";

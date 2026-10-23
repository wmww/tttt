// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "allowJs": true, "checkJs": true } }
// @filename: b.js
export const x = /** @type {number | undefined} */ (undefined);
export const j1 = x!;
export const j2 = x!!;
export const j3 = x!!!;

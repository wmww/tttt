// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "allowJs": true, "checkJs": true } }
// @filename: a.ts
const enum E { A, B }
enum R { A }
export const v1 = E!.A;
export const v2 = E!!.A;
export const v3 = E!!!.A;
export const v4 = E! ! /* c */ !.A;
export const v5 = (E!)!.A;
export const v6 = (E!!)!.A;
export const v7 = E!!["A"];
export const v8 = R!!.A;
export const v9 = [E!!];
export const v10 = typeof E!!;
export const v11 = { ...E!! };
const sentinel: number = "";

// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class BA { constructor(x: { a: 1 }) {} }
class DA extends BA {}
class BD { constructor(x: { a: 1; d: 1 }) {} }
declare const cnd: boolean;
(cnd ? DA : BD).nope;
const sentinel: number = "";

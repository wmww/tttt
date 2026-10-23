// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext" } }
// @filename: a.ts
class B {} class C {}
/** @augments B */
class A0 extends C {}
/** @augments B {@link C} */
class A1 extends C {}
/**
 * @see C
 * @extends B
 */
export class A2 extends C {}
/** {@link C} */
/** @augments B */
class A3 extends C {}
const sentinel: number = "";

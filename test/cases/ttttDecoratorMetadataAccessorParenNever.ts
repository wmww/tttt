// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "isolatedModules": true, "experimentalDecorators": true, "emitDecoratorMetadata": true, "module": "esnext" } }
// @filename: m.ts
export interface A {}
export interface B {}
// @filename: a.ts
// TS1272 (tsc's markDecoratorMedataDataTypeNodeAsReferenced): a decorated accessor's type node is its own annotation,
// else the other accessor's (getAnnotatedAccessorTypeNode), so an unannotated getter's is the setter's parameter's and
// a setter's own comes before the getter's; a parenthesized `(never)` in a union is not elided
// (getEntityNameForDecoratorMetadataFromTypeList looks at the member's kind): no entity name (bun 34-P4, 15-C11)
import { A, B } from "./m";
declare const dec: any;
const k = "x";
class C { @dec get [k]() { return null! } set [k](v: A) {} }
class D { @dec set y(v: A) {} get y(): B { return null! } }
class E { @dec p!: A | (never); @dec q!: B | never; }

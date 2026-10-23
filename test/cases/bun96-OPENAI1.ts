// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const c: boolean;
// The shape of openai-node, src/_vendor/zod-to-json-schema: `{} & Meta` is `Meta`, a supertype of the other members.
type Meta = { title?: string };
type Schema = ({ type: "string" } | { type: "number" } | {}) & Meta;
type MapType = { maxItems: 125; items: [Schema, Schema] };
type RecordType = { type: "object"; additionalProperties: Schema };
declare function parseDef(): Schema | undefined;
function parseMapDef(): MapType | RecordType {
  const keys = parseDef() || {};
  return { type: "array", maxItems: 125, items: [keys, keys] };
}
// Ordinary code: a named union with a member that is a supertype of all the others.
interface Animal { name: string }
interface Dog extends Animal { bark(): void }
interface Cat extends Animal { meow(): void }
type Pet = Animal | Dog;
declare const a: Pet, b: Pet, u: Pet | undefined, dog: Dog, cat: Cat;
const x = c ? a : b;
if ("bark" in x) x.bark();
for (const e of [a, b]) if ("bark" in e) e.bark();
let y = u ?? b;
y = { name: "", bark() {} };
const t1: number = c ? a : b;
const t2: number = [a, b];
const t3: number = u || b;
const t4: number = u ?? b;
const t5: number = c ? a : dog;
const t6: number = c ? a : cat;
// Named by an origin, without an alias.
declare const o: ({ type: "s" } | { type: "n" } | {}) & Meta;
const t7: number = c ? o : o;
// reduceVoidUndefined of removeRedundantLiteralTypes: one member is left, two are left.
type VU = void | undefined;
type VUS = void | undefined | string;
declare const vu: VU, vus: VUS;
const t8: number = c ? vu : vu;
const t9: number = c ? vus : vus;
// The two agree: two members are left, two named unions, a named union inside `Pet | undefined`.
type Three = Animal | Dog | string;
type Pet2 = Animal | Cat;
declare const three: Three, p2: Pet2;
const s1: number = c ? three : three;
const s2: number = c ? a : p2;
const s3: number = c ? u : a;
const sentinel: number = "";

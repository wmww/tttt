// @strict: true
type SchemaType = "object" | "array" | "string";
type _JSONSchema = boolean | JSONSchema;
type JSONSchema = {
  [k: string]: unknown;
  type?: SchemaType | SchemaType[];
  items?: _JSONSchema | _JSONSchema[];
  additionalItems?: _JSONSchema;
};
declare function from(schema: JSONSchema | boolean): void;
from({ type: "array", items: [{ type: "string" }, { type: "object" }], additionalItems: false });
const s: JSONSchema = { items: [{ type: "string" }] };

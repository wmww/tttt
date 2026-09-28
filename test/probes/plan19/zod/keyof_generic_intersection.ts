// @strict: true
type optionalKeys<T extends object> = { [k in keyof T]: undefined extends T[k] ? k : never }[keyof T];
type requiredKeys<T extends object> = { [k in keyof T]: undefined extends T[k] ? never : k }[keyof T];
type addQuestionMarks<T extends object> = { [K in requiredKeys<T>]: T[K] } & { [K in optionalKeys<T>]?: T[K] } & { [k in keyof T]?: unknown };
type flatten<T> = { [k in keyof T]: T[k] };
interface Schema<O = unknown> { _output: O }
type baseOut<Shape extends Record<string, Schema>> = { [k in keyof Shape]: Shape[k]["_output"] };
type Out<Shape extends Record<string, Schema>> = flatten<addQuestionMarks<baseOut<Shape>>>;
function strip<TData extends Schema>(schema: TData, data: Out<{ nested: TData }>) {
    const n = data.nested;
    return n;
}
declare const s: Schema<{ a: string }>;
const r = strip(s, null!);

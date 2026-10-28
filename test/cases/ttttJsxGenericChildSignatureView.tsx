// origin: npm firecmsco/firecms @ c5764a3b packages/firecms_core (tsconfig.bench.json), src/form/PropertyFieldBinding.tsx:102 (formex Field)
// flip: no
// A generic component's function child takes its contextual signature from the view the call's inference gives it
// (C's default through the non-fixing mapper picks the conditional's false branch), as an attribute does
// @strict: true
// @jsx: preserve
declare namespace JSX {
  interface Element {}
  interface IntrinsicElements { div: { children?: any } }
  interface ElementChildrenAttribute { children: {} }
}
type ElementType = keyof JSX.IntrinsicElements | ((props: any) => JSX.Element | null);
type CP<C> = C extends (p: infer P) => any ? P : {};
interface FProps<V> { field: { value: V }; form: { values: any } }
interface FieldConfig<Value, C extends ElementType | undefined = undefined> {
    as?: C | string;
    children?: ((props: FProps<Value>) => JSX.Element) | string;
    name: string;
}
type FieldProps<T, C extends ElementType | undefined> = { as?: C } & (C extends ElementType ? (CP<C> & FieldConfig<T, C>) : FieldConfig<T, C>);
declare function Field<T, C extends ElementType | undefined = undefined>(p: FieldProps<T, C>): JSX.Element;
const x = <Field name="a">{(fieldProps) => <div>{fieldProps.form.values}</div>}</Field>;

// @jsx: preserve
// @strict: true
// A generic function component whose attributes mix a plain attribute (`schema`, the only inference source for S)
// with context-sensitive arrows. tsc's first inference round checks the attributes object with the
// context-sensitive members skipped (SkipContextSensitive) and infers S from `schema`; TValues (no candidates) then
// gets its default instantiated with the inferred S. tttt skips the whole attributes object in round 1, so `values`
// ends up typed `object` (TValues' constraint). Same for a plain call with an object literal argument.
// Reduced from bulletproof-react's `<Form schema={...} onSubmit={(values) => ...}>{({ register }) => ...}</Form>`.
declare namespace JSX {
    interface Element { el: true }
    interface IntrinsicAttributes { key?: string }
    interface IntrinsicElements { [k: string]: any }
}
interface Schema<Out> { _out: Out }
type Infer<S extends Schema<any>> = S["_out"];
type FormProps<TValues, S> = {
    schema: S;
    onSubmit: (values: TValues) => void;
    render: (methods: { values: TValues }) => JSX.Element;
};
declare function Form<S extends Schema<any>, TValues extends object = Infer<S>>(props: FormProps<TValues, S>): JSX.Element;
declare const loginSchema: Schema<{ email: string }>;
declare function login(v: { email: string }): void;

const a = <Form schema={loginSchema} onSubmit={(values) => login(values)} render={({ values }) => <b>{values.email}</b>} />;

// Same without JSX
F({ schema: loginSchema, onSubmit: (values) => login(values) });
declare function F<S extends Schema<any>, TValues extends object = Infer<S>>(props: { schema: S; onSubmit: (values: TValues) => void }): void;

// Simplest form: T inferred from `value`, used by the context-sensitive `onChange`
declare function G<T>(props: { value: T; onChange: (v: T) => void }): void;
G({ value: { b: 2 }, onChange: (v) => { const n: number = v.b; } });

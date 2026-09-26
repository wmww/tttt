type FieldValues = Record<string, any>;
interface ZodType<Output = any, Def = any, Input = Output> { _output: Output; }
type Infer<T extends ZodType<any, any, any>> = T["_output"];
declare const schema: ZodType<{ email: string }>;
type FormProps<TFormValues extends FieldValues, Schema> = {
  onSubmit: (v: TFormValues) => void;
  schema: Schema;
  children: (methods: TFormValues) => void;
};
declare function Form<Schema extends ZodType<any, any, any>, TFormValues extends FieldValues = Infer<Schema>>(p: FormProps<TFormValues, Schema>): TFormValues;
const r = Form({ onSubmit: (values) => {}, schema, children: (m) => { const e: string = m.email; } });
const r2 = Form({ schema, onSubmit: (values) => {} });

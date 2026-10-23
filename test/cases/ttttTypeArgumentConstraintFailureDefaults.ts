// A call whose explicit type arguments fail their constraints (TS2344) returns what tsc's getTypeArgumentsFromNodes
// gives: a missing type argument is its default as written, not instantiated (portabase's react-hook-form useForm:
// UseFormReturn<output<Z>, any, TFieldValues>, TS2322 against the annotation)
type FieldValues = Record<string, any>;
type output<T> = T extends {} ? T extends { _zod: { output: infer O } } ? O : unknown : unknown;
type UseFormHandleSubmit<TFieldValues extends FieldValues, TTransformedValues = TFieldValues> = (onValid: (data: TTransformedValues) => void, onInvalid?: (e: TFieldValues) => void) => void;
type UseFormReturn<TFieldValues extends FieldValues = FieldValues, TContext = any, TTransformedValues = TFieldValues> = {
    handleSubmit: UseFormHandleSubmit<TFieldValues, TTransformedValues>;
};
declare function useForm<TFieldValues extends FieldValues = FieldValues, TContext = any, TTransformedValues = TFieldValues>(props?: {}): UseFormReturn<TFieldValues, TContext, TTransformedValues>;
const useZodForm = <Z extends object>(): UseFormReturn<output<Z>> => useForm<output<Z>>({});

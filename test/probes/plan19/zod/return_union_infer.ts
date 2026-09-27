// @strict: true
// @exactOptionalPropertyTypes: true
declare class ZodError<T> { t: T }
type SafeParseSuccess<Output> = { success: true; data: Output; error?: never } ;
type SafeParseError<Input> = { success: false; error: ZodError<Input>; data?: never };
type SafeParseReturnType<Input, Output> = SafeParseSuccess<Output> | SafeParseError<Input>;
declare const handleResult: <Input, Output>(r: Output) => { success: true; data: Output } | { success: false; error: ZodError<Input> };
function safeParse<Input, Output>(o: Output): SafeParseReturnType<Input, Output> {
    return handleResult(o);
}

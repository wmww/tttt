// origin: npm @fireproof/core-types-protocols-cloud@0.24.19 https://github.com/fireproof-storage/fireproof tsconfig.json seed 20261201
// flip: no
// @strict: true
// @noEmit: true
// zod 4 SafeParseResult destructured: `!success || !data` narrows the parent union to the error member (the success
// member's data is never falsy), so `error` is defined in the true branch; either test alone agrees with tsgo, the
// `||` of the two does not (spurious TS18048).
type SafeParseSuccess<T> = { success: true; data: T; error?: never };
type SafeParseError<T> = { success: false; data?: never; error: { message: string } };
type SafeParseResult<T> = SafeParseSuccess<T> | SafeParseError<T>;
declare function safeParse(x: unknown): SafeParseResult<{ kty: string }>;
export function f(x: unknown) {
  const { success, data, error } = safeParse(x);
  if (!success || !data) {
    throw new Error(`Invalid public JWK: ${error.message}`);
  }
  return data;
}

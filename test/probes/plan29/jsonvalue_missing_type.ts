// tsc (strict, exactOptionalPropertyTypes): no errors. tttt: TS2345 on line 7: the widened `{}` gets `a?: undefined`
// where tsc gives the missing type (getUndefinedProperty: undefinedOrMissingType), which index signatures skip
type JsonObject = {[Key in string]: JsonValue} & {[Key in string]?: JsonValue | undefined};
type JsonArray = JsonValue[] | readonly JsonValue[];
type JsonPrimitive = string | number | boolean | null;
type JsonValue = JsonPrimitive | JsonObject | JsonArray;
declare function expectAssignable<T>(value: T): void;
expectAssignable<JsonValue>([{}, {a: 'hi'}, null, 3]);
const o: JsonObject = {a: 'hi'};
const e: JsonObject = {};

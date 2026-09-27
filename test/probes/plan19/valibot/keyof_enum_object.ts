interface Enum { [key: string]: string | number; }
type EnumValues<TEnum extends Enum> = {
  [TKey in keyof TEnum]: TKey extends number
    ? TEnum[TKey] extends string
      ? TEnum[TEnum[TKey]] extends TKey
        ? never
        : TEnum[TKey]
      : TEnum[TKey]
    : TKey extends 'NaN' | 'Infinity' | '-Infinity'
      ? TEnum[TKey] extends string
        ? TEnum[TEnum[TKey]] extends number
          ? never
          : TEnum[TKey]
        : TEnum[TKey]
      : TKey extends `+${number}`
        ? TEnum[TKey]
        : TKey extends `${infer TNumber extends number}`
          ? TEnum[TKey] extends string
            ? TEnum[TEnum[TKey]] extends TNumber
              ? never
              : TEnum[TKey]
            : TEnum[TKey]
          : TEnum[TKey];
}[keyof TEnum];
enum specialEnum {
  option1 = 'foo',
  option2 = 0,
  option3,
  'Infinity',
  '-Infinity',
  'NaN',
}
type V = EnumValues<typeof specialEnum>;
type K = keyof typeof specialEnum;
type I = (typeof specialEnum)['NaN'];
type J = (typeof specialEnum)[3];
declare const v: V; const w: null = v;

// @strict: true
// A template literal type's placeholders are related to string | number | ... at the template (tsc's checkSourceElement
// of it): a runaway there is reported at the template, not at the declaration around it (vencord's Settings.ts)
type UseSettings<T extends object> = ResolveUseSettings<T>[keyof T];

type ResolveUseSettings<T extends object> = {
    [Key in keyof T]:
    Key extends string
    ? T[Key] extends Record<string, unknown>
    ? `${Key}.*` | (ResolveUseSettings<T[Key]> extends Record<string, string> ? `${Key}.${ResolveUseSettings<T[Key]>[keyof T[Key]]}` : never)
    : Key
    : never;
};

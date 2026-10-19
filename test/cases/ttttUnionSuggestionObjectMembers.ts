// @strict: true
// A union's spelling suggestions are the properties every member has, Object's members included (tsc's union property
// looks each member up with getPropertyOfType): `value` on `string | Ref<string>` suggests `valueOf` (vue-i18n's
// lazy-loading example, `i18n.global.locale.value` with legacy and composition modes)
interface Ref<T> { value: T }
declare const a: string | Ref<string>;
declare const b: number | { valueX: number };
declare const c: string | { z: 1 };
export const r = [a.value, b.valueY, c.valu, c.toStrin];

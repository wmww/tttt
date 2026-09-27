// @strict: true
interface A { kind: 'a'; x: number; std: string }
interface B { kind: 'b'; y: string; std: string }
declare function take<T extends A | B>(t: T): T;
type With<T extends A | B> = T extends A ? Omit<T, 'std'> & { std: string; extra: 1 } : T extends B ? Omit<T, 'std'> & { std: string; extra: 2 } : never;
function f<T extends A | B>(s: T): With<T> {
  // @ts-expect-error
  return {
    ...s,
    get std() {
      take(this);
      return '';
    },
  };
}

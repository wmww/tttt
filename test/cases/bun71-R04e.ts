// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare function use(...x: any[]): void;
class G<T> { f() {} g(): void {} h(x: T): T { return x; } }
class A { f() {} g(): void {} p = () => {};
  m(other: A) {
    use(this.f && other.f());
    use(this.p && other.p());
    use(other.f && this.f());
    use(this.g && other.g());
    use(this.f && this.f());
  }
}
declare const g1: G<number>, g2: G<string>, g3: G<number>;
use(g1.f && g2.f());
use(g1.g && g2.g());
use(g1.h && g2.h(""));
use(g1.f && g3.f());
function tp<T extends A>(t: T, a: A) { use(t.f && a.f()); use(t.g && a.g()); use(t.f && t.f()); }
const sentinel: number = "";

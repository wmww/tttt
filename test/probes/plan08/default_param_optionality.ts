// @strict: true
interface Opts { a?: number }
declare function use(o: Opts): void;
export function f(options: Opts | undefined = {}, more?: boolean) {
    use(options);
    let x: number | undefined;
    x ||= 1;
    use(options);
}

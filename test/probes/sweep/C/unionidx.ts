// @strict: true
type Hook<T> = { noBubble: boolean } | { setup(this: T, data: any, ns: string): void } | { add(this: T, h: number): void } | { [key: string]: any };
interface Hooks { [event: string]: Hook<EventTarget> }
declare const special: Hooks;
special.multi = {
    noBubble: true,
    setup(data, ns) {},
    add(h) {},
};
const x: Hook<EventTarget> = { noBubble: true, setup(data, ns) {}, add(h) {} };

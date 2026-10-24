// @strict: true
// A substitution type of a non-generic type (`NoInfer<P>` instantiated with an object, which stays a substitution type:
// tsc's isNoInferTargetType) has the facts of its base: an intersection `Ctx & NoInfer<Props>` isn't possibly nullish,
// so destructuring it reads the property (styled-components' `${({ $align }) => ...}`, fui-fancyui)
type Props = { $align?: 'left' | 'right' };
interface Ctx { x?: number }
declare function call<P extends object>(s: string, cb: (ctx: Ctx & NoInfer<P>) => unknown): void;
call<Props>('a', ({ $align }) => { const z: [number] = [$align]; });
call<Props>('a', (c) => { const z: [number] = [c.$align]; });
call<Props>('a', (c) => { const { $align } = c; const z: [number] = [$align]; });

export {};

// @declaration: true
// @strict: true
// Inferring from an aliased intersection to an intersection target with an inference candidate: tsc's
// inferFromTypes remakes the source with getIntersectionType after inferFromMatchingTypes, so the inferred type is
// unaliased (vueuse: vue's `components: Comp` infers LC as DefineComponent's members, not `DefineComponent<...>`)
type Named<T> = { new (): T } & { name: string };
declare const comp: Named<number>;
declare function use<LC>(options: { components?: LC & Record<string, unknown> }): LC;
export const inferred = use({ components: comp });
declare function plain<LC>(options: { components?: LC }): LC;
export const kept = plain({ components: comp });

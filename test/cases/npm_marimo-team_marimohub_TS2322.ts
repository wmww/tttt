// origin: npm marimo-team/marimohub@main (2139c26c50ad) https://github.com/marimo-team/marimohub packages/core/tsconfig.bench.json large seed 20261402
// flip: yes
// @strict: true
// @target: es2022
// better-all's `all(tasks)`: the `as` clause's `T[K] extends Function` over the tasks literal (checking the argument)
// walked the methods' types for the type variables they mention and inferred `load`'s return type, from a class
// method's body where the literal's methods aren't closed (the class's `this`). Inferring it there took `load`'s
// contextual `this` (the ThisType marker) before the call's inference was under way: `this.$.reachable` stayed
// `Promise<unknown> | Promise<ReturnType<T[string]>>`, not assignable to `() => Promise<void>`'s return
declare function all<T extends Record<string, any>>(tasks: T & ThisType<{
    $: { [K in keyof T]: ReturnType<T[K]> extends Promise<infer R> ? Promise<R> : Promise<ReturnType<T[K]>> };
}> & {
    [K in keyof T as T[K] extends Function ? K : never]: T[K];
}): void;
declare const launchStrategy: any;
export class SandboxProvisioner {
	prepareInto() {
		const loadWorkspace = (waitUntilReady: () => Promise<void>) => launchStrategy;
		all({
			async reachable() {},
			async load() {
				return loadWorkspace(() => this.$.reachable);
			},
		});
	}
}

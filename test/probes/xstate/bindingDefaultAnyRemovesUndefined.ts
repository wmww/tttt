// @strict: true
// A default of type any removes undefined from an annotated binding element like any other default that can't be
// undefined (tsc's UnknownFacts lack IsUndefined): xstate spawnChild's ...[src, { id, systemId } = {} as any]
function g(...[src, { id } = {} as any]: [src: number, options?: { id?: string }]) {}
function h([src, { id } = {} as any]: [src: number, options?: { id?: string }]) {}
function k([src, { id } = {}]: [src: number, options?: { id?: string }]) {}

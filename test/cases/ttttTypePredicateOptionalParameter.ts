// @strict: true
// A type predicate's type is checked against the parameter symbol's type (tsc's checkTypePredicate, getTypeOfSymbol):
// an optional parameter's has undefined, so `asserts size is Size | undefined` fits `size?: string` (tanstack-ai's
// ai-grok validateVideoSize). Without strictNullChecks nothing changes; a default value adds nothing
type AR = '1:1' | '16:9'
type Size = AR | `${AR}_${'480p' | '720p'}`
export function validate(model: string, size?: string): asserts size is Size | undefined {
    if (size === undefined) return
}
export function isSize(size?: string): size is Size | undefined {
    return true
}
export function withDefault(size: string = '1:1'): size is Size | undefined {
    return true
}

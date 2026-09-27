// @strict: true
// @exactOptionalPropertyTypes: true
interface Options { readonly target: string; readonly libraryOptions?: Record<string, unknown> | undefined }
type P = Parameters<(options: Options) => void>[0];
function f(params?: P) {
    const { libraryOptions, target } = params ?? {};
    return [libraryOptions, target];
}
function g(params?: Options) {
    const { libraryOptions, target } = params ?? {};
    return [libraryOptions, target];
}

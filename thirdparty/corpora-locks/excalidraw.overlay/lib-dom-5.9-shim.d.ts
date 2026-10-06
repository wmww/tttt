// tttt corpus overlay: TypeScript 5.9's lib.dom types TextEncoder.encode() as returning Uint8Array<ArrayBuffer>, 5.8's
// as Uint8Array<ArrayBufferLike>. Excalidraw relies on the former (it moved to TypeScript 5.9.3 in this commit); the
// merged overload comes first, so tsc 5.8 picks it.
interface TextEncoder {
  encode(input?: string): Uint8Array<ArrayBuffer>;
}

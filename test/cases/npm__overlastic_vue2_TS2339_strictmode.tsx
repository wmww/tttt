// origin: npm @overlastic/vue2@0.9.0 https://github.com/hairyf/overlastic tsconfig.bench.json seed 20261106
// flip: no
// @jsx: preserve
// @module: esnext
// @moduleResolution: bundler
// @esModuleInterop: true
// @strict: true
// @filename: tsconfig.json
{ "compilerOptions": { "jsx": "preserve", "module": "esnext", "moduleResolution": "bundler", "esModuleInterop": true, "strict": true, "noEmit": true, "paths": { "react": ["./global.react.d.ts"] } } }
// @filename: global.react.d.ts
declare module 'react' {
  export const createContext: any;
}
// @filename: main.tsx
import React from 'react'
const x = <React.StrictMode></React.StrictMode>

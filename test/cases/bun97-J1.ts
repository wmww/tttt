// @filename: tsconfig.json
{
 "compilerOptions": {
  "strict": true,
  "noEmit": true,
  "target": "esnext",
  "module": "esnext",
  "moduleResolution": "bundler",
  "lib": [
   "esnext"
  ],
  "types": [],
  "skipLibCheck": true,
  "jsx": "react-jsx",
  "allowJs": true,
  "checkJs": true
 },
 "files": [
  "App.TSX",
  "b.Tsx",
  "z.JSX",
  "c.tsx",
  "s.ts"
 ]
}
// @filename: App.TSX
export const e = <div />;
// @filename: b.Tsx
export const e = <div />;
// @filename: c.tsx
export const e = <div />;
// @filename: s.ts
export const sentinel: number = "";
// @filename: z.JSX
export const e = <div />;

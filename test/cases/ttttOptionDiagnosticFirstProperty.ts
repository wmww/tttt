// @noImplicitReferences: true

// An option diagnostic naming two options goes on the first property of compilerOptions named either (tsgo's
// ForEachPropertyAssignment returns at the first match): TS5069 at sourceMap, not at mapRoot as well
// @filename: /app/index.ts
export const x = 0;

// @filename: /app/tsconfig.json
{
    "compilerOptions": {
        "outDir": "bin",
        "sourceMap": false,
        "mapRoot": "myMapRoot",
        "declaration": true
    }
}

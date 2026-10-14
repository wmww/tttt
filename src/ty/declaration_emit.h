#pragma once

#include "prelude.h"

typedef struct SourceFile SourceFile;
typedef struct ErrorCtx ErrorCtx;

// Declaration emit (tsgo's transformers/declarations with the node builder's pseudo types, pseudo.h): the diagnostics
// its transformer reports, the isolatedDeclarations ones (TS9007-TS9039) and what the symbol tracker reports (TS4xxx)
// (plan 40), and the `.d.ts` text (plan 64). Run by the checker that checked the file, after checking it

// tsc's getEmitDeclarations (declaration or composite) and the file is one tsc emits (getSourceFilesToEmit): not a
// declaration file, JSON or JavaScript
bool declaration_diagnostics_wanted(SourceFile const* file);
// `emit`: also write the file's declarations (SourceFile.declaration_output, plan 64)
void declaration_diagnostics_check(SourceFile const* file, ErrorCtx* errs, bool emit);

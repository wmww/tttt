#!/usr/bin/env bash
# Generates src/compiler/diagnosticInformationMap.generated.ts (and diagnosticMessages.generated.json), which
# src/compiler imports and TypeScript's build (hereby generate-diagnostics) writes. Run by ensure-corpora.sh in the
# checkout after installing.
set -euo pipefail
node scripts/processDiagnosticMessages.mjs src/compiler/diagnosticMessages.json

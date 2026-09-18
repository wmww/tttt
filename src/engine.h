#pragma once

typedef struct SourceFile SourceFile;
typedef union arc_js_object ArcObject;

void run_in_engine(SourceFile const* scope, ArcObject root);

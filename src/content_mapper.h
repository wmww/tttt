#pragma once

#include "prelude.h"
#include "compiler_options.h"

// tsgo 7.1's content mappers (plan 56): a tsconfig's `contentMappers` name packages whose program turns files of other
// extensions (`.box`, `.vue`) into TypeScript. Only with --runExternalCode (tsconfig.c's check_content_mappers()
// reports the rest). The mapper runs as a child process speaking JSON-RPC with LSP framing on its stdin/stdout
// (tsgo's internal/contentmapper/hostimpl.go): initialize, openProject, transform per file, closeProject.

typedef struct ContentMapper {
    // The tsconfig entry
    cstr package;
    StrList extensions; // As written, each beginning with '.'
    cstr options_json; // The entry's `options` as JSON text, empty if absent
    // The package's manifest (tsgo's resolveContentMapperManifest): resolved is false (and the rest empty) if the
    // package or its package.json's `typescript.contentMapper` couldn't be found
    bool resolved;
    cstr name, version;
    StrList exec;
    StrList compiler_options; // The options the mapper declared it depends on, sent to openProject
    bool dynamic_config;
    cstr package_dir; // Absolute
} ContentMapper;

void content_mapper_drop(ContentMapper* me);
// name@version (tsgo's Identity), the name alone without a version
cstr content_mapper_identity(ContentMapper const* me);
// The mapper (of options') registered for path's extension, null if there is none
ContentMapper const* content_mapper_for_path(CompilerOptions const* options, csview path);
// Whether `extension` (".box") is registered by one of options' mappers
bool content_mapper_has_extension(CompilerOptions const* options, csview extension);

// One segment of a span map (tsgo's spanmap.Segment): virtual [virtual_start, virtual_end) maps to original
// [original_start, original_end); a verbatim one (kind 0) position by position, an atom (1) or alias (2) as a whole
typedef struct ContentMapSegment {
    u32 virtual_start, virtual_end, original_start, original_end;
    uint8_t kind;
} ContentMapSegment;

typedef struct ContentMapperDiagnostic {
    u32 start, length; // In the original text
    unsigned code;
    cstr message;
} ContentMapperDiagnostic;

// A diagnostic directive (tsgo's MappedDiagnosticDirective): the semantic diagnostics starting in the virtual range
// aren't reported; an `expect` one nothing was suppressed by reports unused_code at its original range
typedef struct ContentMapDirective {
    u32 virtual_start, virtual_end, original_start, original_end;
    bool expect;
    bool used; // program_apply_content_mapper_directives()
    unsigned unused_code;
    cstr unused_message;
} ContentMapDirective;

// A supplemental output (tsgo's MappedResult): another file of the program, `<file>.<i><extension>`
typedef struct ContentMapperSupplemental {
    cstr text;
    cstr extension;
    ContentMapSegment* segments;
    u32 segment_count;
    ContentMapDirective* directives;
    u32 directive_count;
} ContentMapperSupplemental;

// What a transform returned (tsgo's contentmapper.Result), or why it failed (failure_code non-zero: tsgo's
// contentMapperTransformDiagnostic, at the start of an empty placeholder file; detail_code the chained reason, if any)
typedef struct ContentMapperOutput {
    cstr text;
    cstr extension; // The virtual extension: the text parses as `<file><extension>`
    ContentMapSegment* segments; // Sorted by virtual start
    u32 segment_count;
    ContentMapperDiagnostic* diagnostics;
    u32 diagnostic_count;
    ContentMapDirective* directives;
    u32 directive_count;
    cstr diagnostic_source; // The mapper's prefix for its codes (`box` in `box1000`)
    unsigned failure_code;
    cstr failure;
    unsigned detail_code;
    cstr detail;
    unsigned detail_sub_code; // Chained under detail (TS18109: which supplemental output's directive)
    cstr detail_sub;
    ContentMapperSupplemental* supplemental;
    u32 supplemental_count;
} ContentMapperOutput;

void content_mapper_output_drop(ContentMapperOutput* me);

// The mapper processes of one program (tsgo's Host and Project): spawned on first use, one per mapper identity,
// each opened once for the program's project. Thread safe (the load pool transforms from its workers)
typedef struct ContentMapperHost ContentMapperHost;
// config_file is the absolute tsconfig path, options the program's (alive as long as the host)
ContentMapperHost* content_mapper_host_new(csview config_file, CompilerOptions const* options);
// Closes the projects and ends the processes
void content_mapper_host_drop(ContentMapperHost* me);
// Transforms a file (absolute path, its text)
ContentMapperOutput content_mapper_host_transform(ContentMapperHost* me, ContentMapper const* mapper, csview file,
    csview content);

// The program a manifest's `exec` runs under: null runs it as written. The harness sets tsgo's Go test mappers'
// (`thirdparty/typescript/built/tttt-test-mapper`, test/oracle), so `exec: ["compiler-test-mapper"]` runs
// `tttt-test-mapper compiler-test-mapper` like tsgo's in-process test spawner
void content_mapper_set_launcher(char const* path);

// tsgo's spanmap VirtualToOriginalSpan: false if [start, end) lies in synthesized text (tsgo's FidelityNone)
bool content_map_virtual_to_original(ContentMapSegment const* segments, u32 count, u32 start, u32 end, u32* out_start,
    u32* out_end);

// The longest of options' mapper extensions name ends with (tsgo's LongestExtension of extraExtensions), empty if none
csview content_mapper_extension_of(CompilerOptions const* options, csview name);

struct SourceFile;
struct Error;

// What a content-mapped file keeps of its mapper's output (tsgo's ContentMapperSourceFileInfo), SourceFile.content_mapped
typedef struct ContentMapped {
    ContentMapper const* mapper; // The program's options own it
    cstr identity; // name@version: what TS18074 names
    // The untransformed text as a file of its own (registered, never in the program): diagnostics that map back
    // are shown against it. Owned by the canonical file's
    struct SourceFile* original;
    // Of a supplemental output: the file it comes from (tsgo's CanonicalSourceFile), whose name its diagnostics show
    struct SourceFile const* canonical;
    // Of the canonical file: its supplemental outputs, which the loader adds to the program (which then owns them)
    struct SourceFile** supplementals;
    u32 supplemental_count;
    // The span map. has_map false: a failure's empty placeholder, whose diagnostics stay in the transformed text
    ContentMapSegment* segments;
    u32 segment_count;
    bool has_map;
    ContentMapDirective* directives;
    u32 directive_count;
    cstr diagnostic_source;
    cstr virtual_extension; // What the text parsed as (`.ts` for a failure's placeholder)
} ContentMapped;

void content_mapped_free(ContentMapped* me);
// tsgo's diagnosticwriter resolve() for a diagnostic in a content-mapped file (and its related information): a
// mapper's own diagnostic is already in the original text; the compiler's are mapped back there, unless they lie in
// synthesized text, which keeps them in the transformed text with TS18074 chained. Errors elsewhere are left alone.
// To apply just before printing: spans no longer point into the program's files afterwards. False: the diagnostic
// isn't reported at all (an unused declaration's in synthesized text, tsgo's filterAndSortDiagnostics)
[[nodiscard]] bool content_mapped_map_error(struct Error* error);

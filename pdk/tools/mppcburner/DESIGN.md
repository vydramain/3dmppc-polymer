
                       disc.toml
                           │ characters
                           ▼
    ┌──────────────────────────────────────────────┐
    │ 1. LEXER (scanner)                           │
    ├──────────────────────────────────────────────┤
    │ detail/rv_manifest_lexer.hpp                 │
    │ characters → tokens, whitespace and # eaten  │
    │ NEWLINE — a real token                       │
    │ garbage → INVALID, carries its own text      │
    └──────────────────────────────────────────────┘
                           │ std::vector<rv_manifest_token>
                           ▼
    ┌──────────────────────────────────────────────┐       ┌──────────────────────────────────────┐
    │ 2. PARSER (syntax)                           │       │ ERROR HANDLER ← stages 2 and 3       │
    ├──────────────────────────────────────────────┤       ├──────────────────────────────────────┤
    │ detail/rv_manifest_parser.hpp                │       │ detail/rv_manifest_failer.hpp        │
    │ tokens → tree: section → key → value         │─────▶ │ line + text, input order              │
    │ knows NOTHING of rv_pdklib::rv_manifest's    │       │ all errors | first one (stop_at_first)│
    │ fields                                       │       │ report(origin) →                     │
    │ recover(): to the start of the next          │       │   disc.toml:14: unknown key 'titel'  │
    │ statement, a broken header → poisoned,       │       │ suggest_key/suggest_section() →      │
    │ no cascade                                   │       │   did you mean 'title'?              │
    │                                               │       │   rv_manifest_text.cpp               │
    └──────────────────────────────────────────────┘       └──────────────────────────────────────┘
                           │ rv_manifest_tree              │
                           ▼                               │
    ┌──────────────────────────────────────────────┐       ┌──────────────────────────────────────┐
    │ 3. SEMANTICS                                 │       │ SYMBOL TABLE ← stages 3 and 4        │
    ├──────────────────────────────────────────────┤       ├──────────────────────────────────────┤
    │ detail/rv_manifest_semantic.cpp              │       │ static — rv_manifest_schema.hpp      │
    │ is the section known? is the key its own?    │◀────▶ │   sections, keys, value types         │
    │ is the type right?                           │       │ dynamic — rv_manifest_symbols.hpp    │
    │ not set twice?                               │       │   what was seen and on which line     │
    │ runs ONLY if the parser stayed silent        │       │   → 'first at line N'                │
    └──────────────────────────────────────────────┘       └──────────────────────────────────────┘
                           │ rv_manifest_tree (approved)
                           ▼
    ┌──────────────────────────────────────────────┐
    │ 4. IR GEN (binder)                           │
    ├──────────────────────────────────────────────┤
    │ detail/rv_manifest_binder.cpp                │
    │ table {section, key, lambda}                 │
    │ the ONLY one that knows the struct field names│
    └──────────────────────────────────────────────┘
                           │ rv_pdklib::rv_manifest
                           ▼
    ┌──────────────────────────────────────────────┐
    │ BACK-END                                     │
    ├──────────────────────────────────────────────┤
    │ rv_manifest_validate() — id, format, budgets │
    │ rv_burn_run() → textures, scripts, zip       │
    │ rv_manifest_render() → copy to disc          │
    └──────────────────────────────────────────────┘

Paths of .cpp/.hpp are given relative to pdk/lib/include/pdklib/rv_manifest/.

--- music in the burner ---

A .mp3 in [assets] is a music track (rv_manifest_asset_kind music). The track is not
loaded into sound memory: the disc reads it itself, as a drive asset.

    plan   rv_burner_assets/rv_burner_plan.cpp   .mp3 of [assets] -> .pcm name in the sound dir,
                                                 shares the sound collision check
    bake   rv_burner_assets/rv_burner_bake.cpp   bake_music(): mppcmusbaker, same
                                                 range helper as bake_sounds()
    map    rv_burner_build/rv_burner_map.cpp     one "music" line per track
    burn   rv_burner_burn/rv_burner_burn.cpp     copied into the disc directory like
                                                 a baked sound (never a symlink);
                                                 a zip archive takes it as a file

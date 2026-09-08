
                       disc.toml
                           │ characters
                           ▼
    ┌──────────────────────────────────────────────┐
    │ 1. LEXER (scanner)                           │
    ├──────────────────────────────────────────────┤
    │ detail/rv_manifest_lexer.cpp                 │
    │ characters → tokens, whitespace and # eaten  │
    │ NEWLINE — a real token                       │
    │ garbage → INVALID, carries its own text      │
    └──────────────────────────────────────────────┘
                           │ std::vector<rv_manifest_token>
                           ▼
    ┌──────────────────────────────────────────────┐       ┌──────────────────────────────────────┐
    │ 2. PARSER (syntax)                           │       │ ERROR HANDLER ← stages 2 and 3       │
    ├──────────────────────────────────────────────┤       ├──────────────────────────────────────┤
    │ detail/rv_manifest_parser.cpp                │       │ detail/rv_manifest_failer.cpp        │
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

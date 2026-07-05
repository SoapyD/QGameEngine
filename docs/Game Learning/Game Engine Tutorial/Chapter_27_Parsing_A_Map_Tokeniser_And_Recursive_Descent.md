# Chapter 27: Parsing a `.map` — a Tokeniser and a Recursive-Descent Parser

## What You'll Learn
- The **intermediate representation** a parsed map lands in — `MapData` → `MapEntity` → `MapBrush` →
  `MapFace` — and why the IR holds coordinates *exactly as written*, with no conversion baked in
- Why parsing splits into **two passes** — a tokeniser then a recursive-descent parser — and what each
  pass's job is
- Writing the **tokeniser**: stripping `//` comments, tracking line numbers, and lexing the text into
  braces, parens, quoted strings, and bare words
- Writing the **recursive-descent parser**: one function per grammar rule (`parseEntity`, `parseBrush`,
  `parseFace`, `parsePoint`, `number`), and how the grammar in a comment maps straight onto the code
- **Line-numbered error reporting** via a `ParseError` exception caught at the boundary, so malformed
  input fails with a message instead of crashing or producing half-built data
- The public API — `parseMapString` (in-memory) and `loadMapFile` (from disk) — and why the string
  form is the one the tests drive
- Proving it headless with **`map_parse`** (an embedded fixture) and **`map_file`** (an on-disk map),
  split into their own `map_scenarios.cpp` translation unit, wired into CMake and the dispatch

---

## Where We Are

Chapter 26 turned TrenchBroom into a QEngine level editor and produced `assets/maps/smoke.map` — a
hollow box room saved as plain Standard-format text. The engine still can't read a byte of it. This
chapter writes the **front end** that turns that text into structs: a parser. And *only* a parser —
this is the deliberately narrow first slice of the loader. When we're done, the engine can read a
`.map` file into an in-memory tree of entities, brushes, and faces, with a clean error on malformed
input. It will still build **no geometry** and spawn **no entities** — turning the structs into a
walkable room is Chapter 28. Keeping the text→struct step isolated is what makes it testable in
complete isolation from GL, physics, and the ECS.

The work, built data-first as always:

1. **The IR** — `map_data.h`, the struct tree the parser fills.
2. **The tokeniser** — text → a flat list of tokens.
3. **The recursive-descent parser** — tokens → `MapData`, one function per grammar rule.
4. **The public entry points** — `parseMapString` and `loadMapFile`, with line-numbered errors.
5. **The headless proof** — `map_parse` and `map_file` scenarios in a new `map_scenarios.cpp`.

Everything below is grounded in `src/engine/level/types/map_data.h`, `src/engine/level/map_loader.{h,cpp}`,
and `src/harness/map_scenarios.{h,cpp}`.

---

## Step 1: The Intermediate Representation

Data before code. Before we can parse anything we need the shape the parse *produces*. A `.map` file is
a list of entities; each entity is a bag of string properties plus zero or more brushes; each brush is a
list of faces; each face is a plane (three points) plus a texture and its placement. That nests four
deep, and the IR mirrors it one struct per level. Create `src/engine/level/types/map_data.h`.

The header opens with a comment that pins down two things — that this is the *raw* text→struct output
with no conversion applied, and the grammar it mirrors:

```cpp
#pragma once
// Intermediate representation of a parsed TrenchBroom `.map` file (Standard
// format). This is the raw text→struct output of step 2.1 (the parser); it holds
// geometry and properties EXACTLY as written in the file — no coordinate/scale
// conversion is applied here. Z-up→Y-up axis swap and the Quake-unit→engine-unit
// scale happen downstream when brushes become meshes/colliders (2.2/2.4) and
// entities become SpawnParams (2.3), so the conversion is defined in one place.
//
// Grammar this mirrors (Standard .map):
//   map    := entity*
//   entity := '{' (keyval | brush)* '}'
//   keyval := '"' key '"' '"' value '"'
//   brush  := '{' face+ '}'
//   face   := '(' x y z ')' '(' x y z ')' '(' x y z ')' TEX offX offY rot sclX sclY
```

That five-line grammar *is* the design of both this file and the parser — read it once and everything
below is a mechanical consequence. Now the innermost struct, a face:

```cpp
namespace qmap
{
    // One brush face: a plane given as three points (winding defines the outward
    // normal, computed downstream) plus its texture name and UV placement.
    struct MapFace
    {
        glm::vec3   points[3]{glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)};
        std::string texture;          // texture name (no path/extension)
        glm::vec2   offset{0.0f};     // texture offset  (u, v)
        float       rotation = 0.0f;  // texture rotation (degrees)
        glm::vec2   scale{1.0f};      // texture scale    (u, v)
    };
```

The face is a direct transcription of a face line from Chapter 26: three points, a texture name, and
the `offset/rotation/scale` trailer. Note what it *doesn't* store — a normal. The winding of the three
points implies the outward normal, but computing it is downstream work; the IR just holds what the file
says. Then a brush and an entity:

```cpp
    // A convex brush = the intersection of its face half-spaces. Solid world
    // geometry and brush entities (doors/lifts/triggers) are made of these.
    struct MapBrush
    {
        std::vector<MapFace> faces;
    };

    // One `.map` entity: a bag of string key/values plus any brushes it owns.
    // Point entities (lights, spawns, items) carry no brushes; brush entities
    // (worldspawn, func_door, trigger_*) carry one or more.
    struct MapEntity
    {
        std::unordered_map<std::string, std::string> props;
        std::vector<MapBrush>                        brushes;

        bool has(const std::string& key) const { return props.find(key) != props.end(); }

        std::string getString(const std::string& key, std::string fallback = {}) const
        {
            auto it = props.find(key);
            return it == props.end() ? std::move(fallback) : it->second;
        }

        // Convenience: an entity's classname (empty if unset — a malformed entity).
        std::string classname() const { return getString("classname"); }

        bool isPointEntity() const { return brushes.empty(); }
    };
```

`MapEntity` carries the two things an entity can hold — a `props` map of string key/values and a
`brushes` vector — and a few conveniences the rest of the engine will lean on: `has`/`getString` for
property lookups, `classname()` (the property everything dispatches on), and `isPointEntity()` (an
entity with no brushes is a point entity like a light or a spawn). Finally the whole map:

```cpp
    // A whole parsed map file.
    struct MapData
    {
        std::vector<MapEntity> entities;
    };
}
```

A `MapData` is just a list of entities — the top of the grammar. That's the entire IR: four structs
nesting exactly as the file does.

> **Why does the IR store raw map-space values and refuse to do any coordinate conversion here?** The
> header comment states the rule, and it's the single most important design decision in the whole
> loader: *the Z-up→Y-up axis swap and the Quake-unit→engine-unit scale happen in exactly one place,
> downstream.* If the IR converted on the way in, then every later stage — geometry, colliders, entity
> origins, direction vectors — would have to *remember* that the numbers were already converted, and the
> first time someone converted twice (or forgot to convert a new field) you'd get a silently mirrored,
> mis-scaled level with no error. By keeping the IR a faithful transcription of the file, "have these
> coordinates been converted yet?" has one answer everywhere in `MapData`: **no.** Conversion becomes a
> boundary you cross once, on the way *out* of the IR (Chapter 28's `map_transform.h`), instead of a
> property you have to track through the whole system. Parse means parse; transform is a separate verb.

---

## Step 2: Why Two Passes

With the IR defined, the parser's job is text → `MapData`. We do it in two passes, and the file comment
names them:

```cpp
// Standard-format `.map` parser. Two passes: a tokenizer that strips comments
// and splits the text into braces/parens/quoted-strings/bare-words, then a
// recursive descent that assembles entities → brushes → faces. Any malformed
// input aborts with a line-numbered message rather than producing partial data.
```

- **Pass one, the tokeniser**, works at the *character* level. It walks the raw string once, throws
  away whitespace and `//` comments, and emits a flat list of **tokens**: a `{`, a `}`, a `(`, a `)`, a
  quoted string, or a bare word. It knows nothing about grammar — it doesn't care whether a `{` opens an
  entity or a brush. Its only structural job is tracking the current line number so errors can point at
  it.
- **Pass two, the recursive-descent parser**, works at the *token* level. It consumes the token list
  according to the grammar — a map is entities, an entity is key/values and brushes, a brush is faces —
  building the `MapData` tree as it goes.

> **Why separate tokenising from parsing rather than parse straight from characters?** Because the two
> jobs reason about completely different things, and mixing them makes both harder. The tokeniser cares
> about *characters*: is this whitespace, is this the start of a `//` comment, where does this quoted
> string end. The parser cares about *structure*: does a `{` here mean a new entity or a nested brush,
> is a texture name expected after these three points. If the parser had to also skip whitespace and
> comments between every symbol, that low-level bookkeeping would be smeared through every grammar
> rule. Splitting them means the tokeniser handles "what are the meaningful lexemes and what line is
> each on" *once*, and the parser gets a clean stream where whitespace and comments simply don't exist.
> It's the same separation every real compiler uses — lexer then parser — for the same reason: each pass
> does one kind of thinking.

---

## Step 3: The Tokeniser

Create `src/engine/level/map_loader.cpp`. First the token type. A token is a kind, its text, and the
line it appeared on:

```cpp
namespace qmap
{
    namespace
    {
        enum class Tok { LBrace, RBrace, LParen, RParen, Str, Word, End };

        struct Token
        {
            Tok         type;
            std::string text;   // unquoted contents for Str; raw lexeme for Word
            int         line;
        };
```

Seven kinds cover the whole format: the four structural brackets, a quoted `Str`, a bare `Word`
(numbers, texture names, keywords — anything else), and a synthetic `End` marker so the parser always
has a terminator to stop on. Every token records its `line` — that field is the entire reason errors can
say "line 42."

Now the tokeniser itself. It's one loop over the characters:

```cpp
        // ─── Tokenizer ───────────────────────────────────────────────
        std::vector<Token> tokenize(const std::string& src)
        {
            std::vector<Token> out;
            int line = 1;
            const std::size_t n = src.size();

            for (std::size_t i = 0; i < n; )
            {
                char c = src[i];

                if (c == '\n') { ++line; ++i; continue; }
                if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

                // `//` line comment
                if (c == '/' && i + 1 < n && src[i + 1] == '/')
                {
                    while (i < n && src[i] != '\n') ++i;
                    continue;
                }
```

The loop's top handles the three things that produce *no* token: a newline bumps the line counter,
other whitespace is skipped, and a `//` runs the cursor to end of line (dropping the comment). Note the
loop deliberately does **not** auto-increment `i` — each branch advances it by exactly the right amount,
because a bare word and a quoted string consume very different numbers of characters.

Next the structural characters and strings:

```cpp
                switch (c)
                {
                    case '{': out.push_back({Tok::LBrace, "{", line}); ++i; continue;
                    case '}': out.push_back({Tok::RBrace, "}", line}); ++i; continue;
                    case '(': out.push_back({Tok::LParen, "(", line}); ++i; continue;
                    case ')': out.push_back({Tok::RParen, ")", line}); ++i; continue;
                    default: break;
                }

                // Quoted string (no escape sequences in the .map format).
                if (c == '"')
                {
                    const int startLine = line;
                    std::string s;
                    ++i;  // opening quote
                    while (i < n && src[i] != '"')
                    {
                        if (src[i] == '\n') ++line;
                        s.push_back(src[i]);
                        ++i;
                    }
                    if (i < n) ++i;  // closing quote
                    out.push_back({Tok::Str, std::move(s), startLine});
                    continue;
                }
```

The four brackets each emit a one-character token. A `"` starts a quoted string: skip the opening quote,
copy characters until the closing quote (bumping `line` if the string spans newlines, which map property
values shouldn't but the tokeniser stays honest), skip the closing quote, and emit a `Str` token holding
the *unquoted* contents. There are no escape sequences in the `.map` format, so this is as simple as it
looks. Finally, anything else is a bare word:

```cpp
                // Bare word (numbers, texture names, keywords). Runs until
                // whitespace or a structural character.
                std::string w;
                while (i < n)
                {
                    char d = src[i];
                    if (std::isspace(static_cast<unsigned char>(d)) ||
                        d == '{' || d == '}' || d == '(' || d == ')' || d == '"')
                        break;
                    w.push_back(d);
                    ++i;
                }
                out.push_back({Tok::Word, std::move(w), line});
            }

            out.push_back({Tok::End, "", line});
            return out;
        }
```

A word runs until it hits whitespace or a structural character — so `grid_orange`, `-192`, `1.5`, and
`worldspawn` are all single `Word` tokens. After the loop, one `End` token is appended so the parser
always has an unambiguous stopping point. The tokeniser has now flattened the whole file into a vector
of `Token`, comments and whitespace gone, line numbers preserved.

> **Why keep numbers as untyped `Word` tokens instead of parsing them to floats in the tokeniser?**
> Because the tokeniser's job is to split text into lexemes, not to interpret them, and interpreting too
> early throws away information the parser needs. A `Word` might be a number (`-192`), a texture name
> (`grid_orange`), or a keyword (`worldspawn`) — the tokeniser can't tell which without grammar context
> it deliberately doesn't have. If it eagerly parsed `-192` to a float it would need a separate token
> kind for "failed to parse," and a texture literally named `12` would be misclassified. Leaving every
> non-bracket, non-string lexeme as a `Word` and letting the *parser* decide "a number is expected
> here, convert this word" (Step 4's `number()`) keeps each pass's responsibility clean: the tokeniser
> says *what the lexemes are*, the parser says *what they mean*.

---

## Step 4: The Recursive-Descent Parser

Now pass two: tokens → `MapData`. **Recursive descent** means one function per grammar rule, each
calling the functions for the rules nested inside it — the call graph mirrors the grammar tree exactly.
Our grammar has five rules (map, entity, brush, face, point), so the parser has five parsing functions
plus a `number()` helper.

First, error handling. A malformed map should fail with a clear message, not crash or return garbage. We
use a small exception type thrown deep in the parse and caught once at the top:

```cpp
        // ─── Recursive-descent parser ────────────────────────────────
        // Throws ParseError on malformed input; parseMapString catches it.
        struct ParseError
        {
            std::string message;
        };
```

The parser is a class holding the token vector and a read cursor:

```cpp
        class Parser
        {
        public:
            explicit Parser(std::vector<Token> toks) : m_toks(std::move(toks)) {}

            MapData parse()
            {
                MapData data;
                while (peek().type != Tok::End)
                    data.entities.push_back(parseEntity());
                return data;
            }

        private:
            const Token& peek() const { return m_toks[m_pos]; }
            const Token& next() { return m_toks[m_pos++]; }
```

`parse()` is the top grammar rule, `map := entity*`: keep parsing entities until the `End` token. Two
cursor primitives drive everything — `peek()` looks at the current token without consuming it, `next()`
returns it and advances. The whole parser is built from these two plus the grammar functions below.

The error helpers and the number reader:

```cpp
            [[noreturn]] void fail(const std::string& what, const Token& at) const
            {
                throw ParseError{"line " + std::to_string(at.line) + ": " + what};
            }

            void expect(Tok type, const char* what)
            {
                if (peek().type != type) fail(std::string("expected ") + what, peek());
                ++m_pos;
            }

            float number()
            {
                const Token& t = peek();
                if (t.type != Tok::Word) fail("expected a number", t);
                try
                {
                    std::size_t used = 0;
                    float v = std::stof(t.text, &used);
                    if (used != t.text.size()) fail("malformed number '" + t.text + "'", t);
                    ++m_pos;
                    return v;
                }
                catch (const ParseError&) { throw; }
                catch (...) { fail("malformed number '" + t.text + "'", t); }
            }
```

`fail` is where the line number pays off — it builds `"line N: <reason>"` from the offending token's
`line` field and throws. `expect` asserts the current token is a given kind and consumes it, or fails
with a readable message. `number()` is the one place a `Word` becomes a `float`: it demands a `Word`,
runs `std::stof`, and — crucially — checks `used == t.text.size()` so that a half-numeric word like
`12abc` is rejected rather than silently read as `12`. The `catch (const ParseError&) { throw; }`
rethrows our own error unchanged while the bare `catch (...)` turns any `stof` exception into a
line-numbered failure.

Now the grammar functions, each a transcription of one line of the grammar comment. Entities first:

```cpp
            MapEntity parseEntity()
            {
                expect(Tok::LBrace, "'{' to open an entity");
                MapEntity ent;
                for (;;)
                {
                    const Token& t = peek();
                    if (t.type == Tok::RBrace) { ++m_pos; return ent; }
                    if (t.type == Tok::End)    fail("unterminated entity (missing '}')", t);
                    if (t.type == Tok::LBrace) { ent.brushes.push_back(parseBrush()); continue; }
                    if (t.type == Tok::Str)
                    {
                        std::string key = next().text;
                        if (peek().type != Tok::Str) fail("expected a value string after key '" + key + "'", peek());
                        ent.props[std::move(key)] = next().text;
                        continue;
                    }
                    fail("expected a \"key\" \"value\" pair or a brush", t);
                }
            }
```

`entity := '{' (keyval | brush)* '}'`, read literally. After the opening `{`, loop: a `}` closes the
entity; an `End` here means the `}` was missing (a truncated file) and fails; a nested `{` is a brush,
so recurse into `parseBrush`; a `Str` is the key of a `"key" "value"` pair, so read the key and then
demand a second `Str` for the value. Anything else is a syntax error. This one function contains the
entire "an entity holds properties *and* brushes, in any order" rule.

Brushes are simpler — `brush := '{' face+ '}'`:

```cpp
            MapBrush parseBrush()
            {
                expect(Tok::LBrace, "'{' to open a brush");
                MapBrush brush;
                for (;;)
                {
                    const Token& t = peek();
                    if (t.type == Tok::RBrace) { ++m_pos; return brush; }
                    if (t.type == Tok::End)    fail("unterminated brush (missing '}')", t);
                    if (t.type == Tok::LParen) { brush.faces.push_back(parseFace()); continue; }
                    fail("expected a face '( ... )' or '}'", t);
                }
            }
```

After the `{`, a `}` closes the brush, an `End` is a truncation error, and a `(` starts a face. A
brush is nothing but faces.

A plane point — `'(' x y z ')'`:

```cpp
            glm::vec3 parsePoint()
            {
                expect(Tok::LParen, "'(' to open a plane point");
                glm::vec3 p;
                p.x = number();
                p.y = number();
                p.z = number();
                expect(Tok::RParen, "')' to close a plane point");
                return p;
            }
```

And finally a face, the richest rule — three points, a texture word, then the five-number trailer:

```cpp
            MapFace parseFace()
            {
                MapFace f;
                f.points[0] = parsePoint();
                f.points[1] = parsePoint();
                f.points[2] = parsePoint();

                if (peek().type != Tok::Word) fail("expected a texture name after the plane points", peek());
                f.texture = next().text;

                // Standard format trailer: offsetX offsetY rotation scaleX scaleY
                f.offset.x = number();
                f.offset.y = number();
                f.rotation = number();
                f.scale.x  = number();
                f.scale.y  = number();
                return f;
            }
```

Put the face line from Chapter 26 beside this and the correspondence is exact:
`( 48 64 112 ) ( 48 -192 112 ) ( 48 64 -16 ) grid_orange 0 0 0 1 1` → three `parsePoint()` calls, a
texture `Word`, then `offset.x offset.y rotation scale.x scale.y`. The parser reads left to right,
consuming exactly what the grammar says is there and failing with a line number the instant it isn't.

> **Why recursive descent, and why one function per grammar rule?** Because the grammar of a `.map` is
> simple, nested, and unambiguous, and recursive descent maps onto it so directly that the code *is* the
> grammar — `parseEntity` calls `parseBrush` calls `parseFace` calls `parsePoint`, exactly as an entity
> contains a brush contains a face contains a point. There's no parser-generator, no table, no state
> machine to reason about: each function reads exactly the tokens its rule owns and hands off nested
> rules to their own functions. That makes it trivial to place a precise, line-numbered error at the
> exact rule that failed ("expected a texture name after the plane points"), and trivial for the next
> reader to see that the code is correct by checking it against the five-line grammar. For a format this
> shape, a hand-written recursive-descent parser is less code *and* more readable than any heavier tool.

---

## Step 5: The Public API and Line-Numbered Errors

The `Parser` class and the tokeniser are both in an anonymous namespace — implementation detail. The
public surface is two free functions, declared in `src/engine/level/map_loader.h`:

```cpp
namespace qmap
{
    // Parse `.map` text already in memory. On a syntax error, returns an empty
    // MapData and, if `error` is non-null, writes a human-readable reason (with a
    // 1-based line number). Comments (`//` to end of line) and whitespace are
    // ignored. Valve-220 face format (extra texture axes) is NOT parsed — the
    // QEngine game config emits Standard format.
    MapData parseMapString(const std::string& text, std::string* error = nullptr);

    // Read + parse a `.map` file from disk. Returns empty (and sets `error`) if
    // the file can't be opened or fails to parse.
    MapData loadMapFile(const std::string& path, std::string* error = nullptr);
}
```

`parseMapString` parses text already in memory; `loadMapFile` reads a file and delegates to it. Both
take an optional `error` out-parameter. The implementations are where the `ParseError` exception meets
the outside world — thrown deep in the parse, caught exactly once here and turned into the `error`
string:

```cpp
    MapData parseMapString(const std::string& text, std::string* error)
    {
        try
        {
            Parser parser(tokenize(text));
            MapData data = parser.parse();
            if (error) error->clear();
            return data;
        }
        catch (const ParseError& e)
        {
            if (error) *error = e.message;
            return {};
        }
    }

    MapData loadMapFile(const std::string& path, std::string* error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            if (error) *error = "could not open map file: " + path;
            return {};
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        return parseMapString(ss.str(), error);
    }
```

`parseMapString` tokenises, parses, and on success clears the error and returns the tree. On a
`ParseError` it writes the message (`"line 3: unterminated brush (missing '}')"`) and returns an
**empty** `MapData` — never a half-built one. `loadMapFile` adds the one failure the string form can't
have — the file not opening — with its own message, then slurps the whole file into a string and hands
it off. The whole exception dance is invisible to callers: they get a filled `MapData` and an empty
error, or an empty `MapData` and a message.

> **Why catch the `ParseError` at the API boundary and return empty-plus-message, rather than letting
> the exception propagate to the caller?** Because a malformed level file is an *expected*, recoverable
> condition, not a programming bug, and the caller shouldn't have to wrap every load in a `try`. Using
> an exception *internally* is the right tool — it lets any of the deeply-nested grammar functions abort
> the entire parse from wherever they are without threading an error return through a dozen call sites.
> But exposing that exception across the module boundary would push that burden onto every caller. So we
> convert at the seam: exceptions for the parser's own control flow, a plain empty-result-plus-error-
> string for the public API. The caller's contract is dead simple — "you get a `MapData`; if `error` is
> non-empty, it's empty and here's why" — which is exactly what Chapter 28's scene setup wants. And
> returning empty (never partial) means a downstream stage can never accidentally build half a level
> from a file that failed to parse.

---

## Step 6: Proving It Headless — `map_parse` and `map_file`

A parser you can't test is a parser you don't trust. We add two headless scenarios — but rather than
pile them into the already-large `headless_main.cpp`, they get their own translation unit,
`src/harness/map_scenarios.cpp`, declared in `src/harness/map_scenarios.h`:

```cpp
namespace mapscenarios
{
    // Parser check (step 2.1): parse an embedded Standard-format fixture and
    // assert the entities → brushes → faces structure + a malformed-input reject.
    bool scenarioMapParse();

    // Load an on-disk `.map` and report its parsed structure (entities, worldspawn
    // brushes, faces). Passes if it parses and holds a worldspawn + a player start.
    bool scenarioMapFile(const std::string& path);

    // Loader-conversion check (steps 2.2–2.3): parse smoke.map, build the Level
    // geometry + SpawnParams, and assert the MVP conversion (36 surfaces, player +
    // light descriptors). Pure data — no GL, registry, or physics.
    bool scenarioMapScene();
}
```

(The third, `scenarioMapScene`, exercises Chapter 28's conversion — we'll ignore it here and cover it
there.) The first scenario, `scenarioMapParse`, is the parser's core test. It parses an **embedded
fixture** — a small map written directly in the C++ source — chosen to exercise the tricky bits:
comments, negative coordinates, a decimal, a non-trivial texture trailer, and a malformed input. From
`src/harness/map_scenarios.cpp`:

```cpp
    bool scenarioMapParse()
    {
        // One worldspawn cube (6 faces), one point spawn, one light. Includes a
        // `//` comment, a negative coordinate, and a decimal to exercise the
        // tokenizer/number parser.
        const std::string src =
            "// sample map fixture\n"
            "{\n"
            "\"classname\" \"worldspawn\"\n"
            "{\n"
            "( -64 -64 -64 ) ( -64 -63 -64 ) ( -64 -64 -63 ) grid_grey 0 0 0 1 1\n"
            "(  64 -64 -64 ) (  64 -64 -63 ) (  64 -63 -64 ) grid_grey 0 0 0 1 1\n"
            "( -64 -64 -64 ) ( -63 -64 -64 ) ( -64 -64 -63 ) grid_grey 0 0 0 1 1\n"
            "( -64  64 -64 ) ( -64  64 -63 ) ( -63  64 -64 ) grid_grey 0 0 0 1 1\n"
            "( -64 -64 -64 ) ( -64 -63 -64 ) ( -63 -64 -64 ) floor 8 16 90 1.5 1\n"
            "( -64 -64  64 ) ( -63 -64  64 ) ( -64 -63  64 ) grid_grey 0 0 0 1 1\n"
            "}\n"
            "}\n"
            "{\n"
            "\"classname\" \"info_player_start\"\n"
            "\"origin\" \"16 16 32\"\n"
            "\"angle\" \"90\"\n"
            "}\n"
            "{\n"
            "\"classname\" \"light\"\n"
            "\"origin\" \"32 32 48\"\n"
            "}\n";

        std::string err;
        qmap::MapData map = qmap::parseMapString(src, &err);

        bool ok = err.empty() && map.entities.size() == 3;
```

The fixture is one worldspawn cube (six faces), a spawn, and a light — three entities. The first
assertion is the baseline: no error, three entities. Then it drills into the structure to prove the
parse was *correct*, not just non-empty:

```cpp
        // worldspawn: 1 brush, 6 faces; check a point, a texture, and UV parsing.
        if (ok)
        {
            const auto& world = map.entities[0];
            ok = world.classname() == "worldspawn"
              && world.brushes.size() == 1
              && world.brushes[0].faces.size() == 6
              && world.brushes[0].faces[0].points[0] == glm::vec3(-64, -64, -64)
              && world.brushes[0].faces[0].texture == "grid_grey"
              && world.brushes[0].faces[4].texture == "floor"
              && world.brushes[0].faces[4].rotation == 90.0f
              && world.brushes[0].faces[4].scale.x == 1.5f;
        }

        // Point entities: no brushes, props parsed.
        if (ok)
        {
            const auto& spawn = map.entities[1];
            const auto& light = map.entities[2];
            ok = spawn.classname() == "info_player_start"
              && spawn.isPointEntity()
              && spawn.getString("origin") == "16 16 32"
              && spawn.getString("angle") == "90"
              && light.classname() == "light"
              && light.isPointEntity();
        }
```

The worldspawn checks reach all the way down: one brush, six faces, the first face's first point equals
`(-64,-64,-64)` (negative coordinates parsed), its texture is `grid_grey`, and — the interesting one —
face 4's texture is `floor` with `rotation == 90` and `scale.x == 1.5`, proving the non-trivial trailer
`floor 8 16 90 1.5 1` parsed the *decimal* and the *rotation* correctly. The point-entity checks confirm
a spawn and a light with no brushes and their properties intact.

Then the negative test — malformed input must fail cleanly, not crash:

```cpp
        // Negative test: a truncated brush must fail with a message, not crash.
        std::string err2;
        qmap::MapData bad = qmap::parseMapString("{\n\"classname\" \"worldspawn\"\n{\n", &err2);
        bool rejectsBad = !err2.empty() && bad.entities.empty();

        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "entities=%zu (want 3), parse err=\"%s\"; malformed rejected=%d",
            map.entities.size(), err.c_str(), rejectsBad ? 1 : 0);
        return report("map_parse", ok && rejectsBad, buf);
    }
```

`"{\n\"classname\" \"worldspawn\"\n{\n"` is an entity whose brush is never closed — the file ends
mid-brush. `parseBrush` hits the `End` token and fails; the scenario asserts `err2` is non-empty *and*
the returned map is empty (the empty-not-partial guarantee from Step 5). The scenario passes only on
`ok && rejectsBad` — the good map parsed exactly right *and* the broken one was rejected with a message.

The second scenario, `scenarioMapFile`, loads a real file off disk (defaulting to `smoke.map`) and
reports its structure — entity count, worldspawn brush count, total faces — passing if it parses and
contains a worldspawn plus a player start. It's the on-disk companion to the embedded fixture: proof
that the parser reads what TrenchBroom *actually wrote*, not just what we hand-typed into the test.

### Wiring the harness

Two build changes. First, `map_scenarios.cpp` is a new translation unit for the headless executable, so
it joins `QEngineHeadless` in `CMakeLists.txt`:

```cmake
add_executable(QEngineHeadless src/harness/headless_main.cpp src/harness/map_scenarios.cpp)
```

(The parser and IR — `map_loader.cpp`, `map_data.h` — compile into the shared `qengine_lib` library
alongside the other level code; we'll list those additions in Chapter 28's summary, since the
`map_to_*` units land together. `map_loader.cpp` specifically is added to `qengine_lib` in the same
block.) Second, the new scenarios join `headless_main.cpp`'s dispatch, calling into the
`mapscenarios` namespace:

```cpp
    else if (scenario == "map_parse")        pass = mapscenarios::scenarioMapParse();
    else if (scenario == "map_file")         pass = mapscenarios::scenarioMapFile(mapArg);
    else if (scenario == "map_scene")        pass = mapscenarios::scenarioMapScene();
```

where `mapArg` is a second command-line argument (`argv[2]`) defaulting to `assets/maps/smoke.map`, so
`map_file` can be pointed at any map on disk.

> **Why split the map scenarios into their own `map_scenarios.cpp` instead of adding them to
> `headless_main.cpp` like every scenario before them?** Because `headless_main.cpp` was already large,
> and the codebase has a standing rule (Chapter 17) that files stay within a size budget. The map
> scenarios are a cohesive, self-contained group — they share the `qmap` includes and test one
> subsystem — so they're the natural thing to lift into a sibling translation unit. It keeps
> `headless_main.cpp` from growing without bound as features accrue, and it means the map tests can pull
> in the loader headers without dragging them into the file that holds every *other* scenario. The
> dispatch in `main` still routes to them by name, so from the command line nothing changed — `map_parse`
> runs exactly like `ride_lift_up` does.

---

## What Changed — Summary

| File | Change |
|------|--------|
| `engine/level/types/map_data.h` | **New file** — the parser's IR: `MapFace` (3 points + texture + placement), `MapBrush` (faces), `MapEntity` (props + brushes, with `classname()`/`isPointEntity()` helpers), `MapData` (entities). Holds raw map-space values; conversion is deliberately downstream. |
| `engine/level/map_loader.{h,cpp}` | **New files** — the Standard-format parser: a tokeniser (strips `//` comments, tracks lines, lexes braces/parens/strings/words) and a recursive-descent `Parser` (one function per grammar rule) throwing a line-numbered `ParseError`. Public `parseMapString` / `loadMapFile` catch it and return empty-plus-message. |
| `harness/map_scenarios.{h,cpp}` | **New files** — `scenarioMapParse` (embedded fixture: structure + malformed-reject) and `scenarioMapFile` (on-disk map: reports structure, passes on worldspawn + player start). `scenarioMapScene` is Chapter 28's. |
| `CMakeLists.txt` | Add `src/engine/level/map_loader.cpp` to `qengine_lib`; add `src/harness/map_scenarios.cpp` to the `QEngineHeadless` executable. |
| `harness/headless_main.cpp` | Include `map_scenarios.h`; add a second CLI arg (`argv[2]`, default `assets/maps/smoke.map`); register `map_parse`, `map_file`, `map_scene` in the dispatch. |

`map_data.h` and `map_loader.h` are headers that compile into their includers. The IR and parser are a
self-contained module — they touch no GL, no physics, no ECS.

---

## What You Should See

There's still nothing new to *play* — the parser builds structs, not geometry. But the harness proves
it works:

1. **`QEngineHeadless map_parse` passes** — the embedded fixture parses into exactly three entities with
   the right brush/face structure (negative coords, a decimal, a rotated texture trailer all correct),
   *and* a truncated brush is rejected with a non-empty error and an empty result — no crash.
2. **`QEngineHeadless map_file` passes** — `assets/maps/smoke.map` loads off disk, reporting its entity
   count, worldspawn brush count, and total faces, and confirming it holds a worldspawn and a player
   start. This is the parser reading TrenchBroom's real output, not a hand-typed fixture.
3. **`QEngineHeadless map_file <path>`** can be pointed at any `.map` on disk via the second argument —
   a quick way to sanity-check a freshly-authored level parses before wiring it into the game.
4. **Malformed maps fail loudly, not silently.** Feed the parser a truncated or garbled file and you get
   `"line N: <reason>"` and an empty `MapData`, never a half-built tree or a crash.

---

## What's Next

The engine can now read a `.map` into a faithful in-memory tree — but that tree is still just Quake-unit,
Z-up numbers sitting in structs. **Chapter 28 makes it playable.** It adds the one-place-only coordinate
transform (Z-up→Y-up plus the 32-units-per-engine-unit scale), turns worldspawn brushes into
axis-aligned box surfaces (the MVP fidelity decision, and why it's *lossless* for `smoke.map`), groups
those surfaces into per-texture render meshes owned by the `Level`, maps every non-world entity onto the
existing `SpawnParams`/`spawnScene` dispatch from Chapter 18, and finally wires `buildWorld` and
`main.cpp` so that launching the engine with a map path loads your authored room — with the hard-coded
showcase kept as a fallback. By the end of the next chapter you'll walk around `smoke.map`.

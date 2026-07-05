#include "engine/level/map_loader.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Standard-format `.map` parser. Two passes: a tokenizer that strips comments
// and splits the text into braces/parens/quoted-strings/bare-words, then a
// recursive descent that assembles entities → brushes → faces. Any malformed
// input aborts with a line-numbered message rather than producing partial data.

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

        // ─── Recursive-descent parser ────────────────────────────────
        // Throws ParseError on malformed input; parseMapString catches it.
        struct ParseError
        {
            std::string message;
        };

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

            std::vector<Token> m_toks;
            std::size_t        m_pos = 0;
        };
    }

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
}

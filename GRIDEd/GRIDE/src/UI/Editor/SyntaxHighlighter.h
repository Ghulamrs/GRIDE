// =============================================================================
//  SyntaxHighlighter.h - splits source text into coloured tokens
//
//  Driven entirely by LanguageSettings (keywords, types, comment markers) so
//  C90, C++11, Shalimar, assembly and linker files all use the same code.
//  BuildRtf() colours a whole file in one go (fast load); Tokenize() is used
//  for the visible region after edits.
// =============================================================================
#pragma once
#include "../Theme.h"
#include "../../Settings/GrideSettings.h"

namespace Gride { namespace UI {

    using namespace Gride::Core;
    using namespace Gride::Settings;

    public enum class TokenKind { Plain, Keyword, Type, String, Number, Comment, Preprocessor, Function };

    public value struct Token
    {
        int Start;
        int Length;
        TokenKind Kind;
        Token(int s, int l, TokenKind k) { Start = s; Length = l; Kind = k; }
    };

    public ref class SyntaxHighlighter
    {
    public:
        SyntaxHighlighter(LanguageSettings^ lang)
        {
            this->lang = lang;
            bool ci = lang != nullptr && (lang->Lang == Language::Assembly || lang->Lang == Language::LinkerCmd);
            StringComparer^ cmp = ci ? StringComparer::OrdinalIgnoreCase : StringComparer::Ordinal;
            keywords = gcnew HashSet<String^>(cmp);
            types = gcnew HashSet<String^>(cmp);
            if (lang != nullptr)
            {
                for each (String^ k in lang->Keywords) keywords->Add(k);
                for each (String^ k in lang->Types) types->Add(k);
            }
            dotIdents = lang != nullptr && (lang->Lang == Language::Assembly || lang->Lang == Language::LinkerCmd);
        }

        property bool IsPlain { bool get() { return lang == nullptr; } }

        // true when position pos lies inside a /* block comment */
        bool InBlockComment(String^ text, int pos)
        {
            if (lang == nullptr || String::IsNullOrEmpty(lang->BlockCommentStart)) return false;
            String^ bs = lang->BlockCommentStart; String^ be = lang->BlockCommentEnd;
            bool inBlock = false;
            int i = 0;
            int n = Math::Min(pos, text->Length);
            while (i < n)
            {
                if (inBlock)
                {
                    int e = text->IndexOf(be, i, StringComparison::Ordinal);
                    if (e < 0 || e >= n) return true;
                    i = e + be->Length; inBlock = false; continue;
                }
                wchar_t c = text[i];
                if (c == L'"' || c == L'\'')             // skip string literal
                {
                    int j = i + 1;
                    while (j < text->Length && text[j] != c && text[j] != L'\n') { if (text[j] == L'\\') j++; j++; }
                    i = j + 1; continue;
                }
                if (!String::IsNullOrEmpty(lang->LineComment) && String::CompareOrdinal(text, i, lang->LineComment, 0, lang->LineComment->Length) == 0)
                {
                    int e = text->IndexOf(L'\n', i);
                    i = e < 0 ? n : e + 1; continue;
                }
                if (String::CompareOrdinal(text, i, bs, 0, bs->Length) == 0) { inBlock = true; i += bs->Length; continue; }
                i++;
            }
            return inBlock;
        }

        // tokens in text[start, end) ; startInComment = state at 'start'
        List<Token>^ Tokenize(String^ text, int start, int end, bool startInComment)
        {
            List<Token>^ r = gcnew List<Token>();
            if (lang == nullptr) return r;
            if (end > text->Length) end = text->Length;
            String^ lc = lang->LineComment;
            String^ bs = lang->BlockCommentStart;
            String^ be = lang->BlockCommentEnd;
            bool hasBlock = !String::IsNullOrEmpty(bs);
            int i = start;

            if (startInComment && hasBlock)
            {
                int e = text->IndexOf(be, i, StringComparison::Ordinal);
                int stop = e < 0 ? end : Math::Min(end, e + be->Length);
                r->Add(Token(i, stop - i, TokenKind::Comment));
                i = stop;
            }

            bool lineStart = i == 0 || text[i - 1] == L'\n';
            while (i < end)
            {
                wchar_t c = text[i];
                if (c == L'\n') { lineStart = true; i++; continue; }
                if (c == L' ' || c == L'\t' || c == L'\r') { i++; continue; }

                // block comment
                if (hasBlock && String::CompareOrdinal(text, i, bs, 0, bs->Length) == 0)
                {
                    int e = text->IndexOf(be, i + bs->Length, StringComparison::Ordinal);
                    int stop = e < 0 ? end : Math::Min(end, e + be->Length);
                    r->Add(Token(i, stop - i, TokenKind::Comment));
                    i = stop; lineStart = false; continue;
                }
                // line comment
                if (!String::IsNullOrEmpty(lc) && String::CompareOrdinal(text, i, lc, 0, lc->Length) == 0)
                {
                    int e = text->IndexOf(L'\n', i);
                    int stop = (e < 0 || e > end) ? end : e;
                    r->Add(Token(i, stop - i, TokenKind::Comment));
                    i = stop; continue;
                }
                // preprocessor directive at the start of a line: #include, #define ...
                if (lang->Preprocessor && c == L'#' && lineStart)
                {
                    int j = i + 1;
                    while (j < end && (text[j] == L' ' || text[j] == L'\t')) j++;
                    while (j < end && Char::IsLetter(text[j])) j++;
                    r->Add(Token(i, j - i, TokenKind::Preprocessor));
                    // <file.h> after #include
                    int k = j;
                    while (k < end && text[k] == L' ') k++;
                    if (k < end && text[k] == L'<')
                    {
                        int e = text->IndexOf(L'>', k);
                        int nl = text->IndexOf(L'\n', k);
                        if (e > 0 && (nl < 0 || e < nl) && e < end) { r->Add(Token(k, e - k + 1, TokenKind::String)); j = e + 1; }
                    }
                    i = j; lineStart = false; continue;
                }
                lineStart = false;

                // string / char literal
                if (c == L'"' || (c == L'\'' && !dotIdents))
                {
                    int j = i + 1;
                    while (j < end && text[j] != c && text[j] != L'\n') { if (text[j] == L'\\') j++; j++; }
                    if (j < end && text[j] == c) j++;
                    if (j > end) j = end;
                    r->Add(Token(i, j - i, TokenKind::String));
                    i = j; continue;
                }
                // number: 42, 0x1F, 3.14f, 1e-3, 10UL
                if (Char::IsDigit(c) || (c == L'.' && i + 1 < end && Char::IsDigit(text[i + 1])))
                {
                    int j = i + 1;
                    while (j < end && (Char::IsLetterOrDigit(text[j]) || text[j] == L'.' ||
                           ((text[j] == L'+' || text[j] == L'-') && (text[j - 1] == L'e' || text[j - 1] == L'E')))) j++;
                    r->Add(Token(i, j - i, TokenKind::Number));
                    i = j; continue;
                }
                // identifier / keyword
                if (Char::IsLetter(c) || c == L'_' || (dotIdents && (c == L'.' || c == L'-')))
                {
                    int j = i + 1;
                    while (j < end && (Char::IsLetterOrDigit(text[j]) || text[j] == L'_' || (dotIdents && text[j] == L'.'))) j++;
                    String^ word = text->Substring(i, j - i);
                    if (keywords->Contains(word)) r->Add(Token(i, j - i, TokenKind::Keyword));
                    else if (types->Contains(word)) r->Add(Token(i, j - i, TokenKind::Type));
                    else if (!dotIdents)
                    {
                        int k = j;
                        while (k < end && (text[k] == L' ' || text[k] == L'\t')) k++;
                        if (k < end && text[k] == L'(') r->Add(Token(i, j - i, TokenKind::Function));
                    }
                    i = j; continue;
                }
                i++;
            }
            return r;
        }

        static Color ColorOf(TokenKind k, Theme^ t)
        {
            switch (k)
            {
            case TokenKind::Keyword:      return t->Keyword;
            case TokenKind::Type:         return t->TypeName;
            case TokenKind::String:       return t->StringLit;
            case TokenKind::Number:       return t->Number;
            case TokenKind::Comment:      return t->Comment;
            case TokenKind::Preprocessor: return t->Preprocessor;
            case TokenKind::Function:     return t->Function;
            default:                      return t->EditorText;
            }
        }

        // whole text -> RTF with colours (index 1 = plain text, 2.. = token kinds)
        String^ BuildRtf(String^ text, Theme^ t, String^ fontName, float fontSize)
        {
            StringBuilder^ sb = gcnew StringBuilder(text->Length * 2 + 512);
            sb->Append("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fmodern ");
            sb->Append(fontName);
            sb->Append(";}}{\\colortbl ;");
            for (int k = 0; k <= (int)TokenKind::Function; k++)
            {
                Color c = ColorOf((TokenKind)k, t);
                sb->AppendFormat("\\red{0}\\green{1}\\blue{2};", c.R, c.G, c.B);
            }
            sb->Append("}\\viewkind4\\uc1\\pard\\f0");
            sb->AppendFormat("\\fs{0}\\cf1 ", (int)Math::Round(fontSize * 2));

            List<Token>^ tokens = Tokenize(text, 0, text->Length, false);
            int pos = 0;
            for each (Token tk in tokens)
            {
                if (tk.Start > pos) AppendEscaped(sb, text, pos, tk.Start - pos);
                sb->AppendFormat("\\cf{0} ", (int)tk.Kind + 1);
                AppendEscaped(sb, text, tk.Start, tk.Length);
                sb->Append("\\cf1 ");
                pos = tk.Start + tk.Length;
            }
            if (pos < text->Length) AppendEscaped(sb, text, pos, text->Length - pos);
            sb->Append("}");
            return sb->ToString();
        }

    private:
        LanguageSettings^ lang;
        HashSet<String^>^ keywords;
        HashSet<String^>^ types;
        bool dotIdents;

        static void AppendEscaped(StringBuilder^ sb, String^ text, int start, int len)
        {
            int end = start + len;
            for (int i = start; i < end; i++)
            {
                wchar_t c = text[i];
                switch (c)
                {
                case L'\\': sb->Append("\\\\"); break;
                case L'{':  sb->Append("\\{"); break;
                case L'}':  sb->Append("\\}"); break;
                case L'\t': sb->Append("\\tab "); break;
                case L'\r': break;
                case L'\n': sb->Append("\\par\r\n"); break;
                default:
                    if (c > 127) sb->AppendFormat("\\u{0}?", (int)(short)c);
                    else sb->Append(c);
                }
            }
        }
    };
}}

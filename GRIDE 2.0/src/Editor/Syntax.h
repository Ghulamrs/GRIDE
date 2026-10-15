#pragma once
// ============================================================================
//  GRIDE 2.0 - Language definitions + syntax tokenizer (Phase 5)
// ============================================================================
#include "../Project/GrideProject.h"

namespace Gride
{
    public enum class EditorMode { PlainText, C90, Cpp11, Shalimar, AsmMasm, AsmC6x, AsmGnu, Json };

    // Order matters: it indexes the colour palette in CodeEditor.
    public enum class TokenKind { Text = 0, Keyword, Type, Comment, StringLit, Number, Preprocessor };

    public value struct TokenSpan
    {
        int Start;
        int Length;
        TokenKind Kind;
    };

    public ref class LanguageDefs abstract sealed
    {
    public:
        // project may be nullptr. The project decides ".h" (C90 or C++11) and
        // the assembly dialect (.asm/.s): TMS6747 -> C6x, Linux/macOS -> GNU as, Windows -> MASM.
        static EditorMode ModeForFile(String^ path, IdeSettings^ s, GrideProject^ project)
        {
            if (String::IsNullOrEmpty(path)) return EditorMode::PlainText;
            String^ ext = Path::GetExtension(path)->ToLowerInvariant();
            if (ext->Length == 0) return EditorMode::PlainText;
            TargetKind target = project != nullptr ? project->TargetValue : TargetKind::Windows;
            bool cppProject = project != nullptr && project->LanguageValue == LanguageKind::Cpp11;
            if (s != nullptr && s->IsShalimarExtension(ext)) return EditorMode::Shalimar;
            if (String::Equals(ext, ".c")) return EditorMode::C90;
            if (String::Equals(ext, ".h")) return cppProject ? EditorMode::Cpp11 : EditorMode::C90;
            if (String::Equals(ext, ".cpp") || String::Equals(ext, ".cc") || String::Equals(ext, ".cxx") ||
                String::Equals(ext, ".hpp") || String::Equals(ext, ".hh") || String::Equals(ext, ".hxx") ||
                String::Equals(ext, ".inl")) return EditorMode::Cpp11;
            if (String::Equals(ext, ".asm") || String::Equals(ext, ".inc") || String::Equals(ext, ".s") || String::Equals(ext, ".sa"))
            {
                if (target == TargetKind::TMS6747 || String::Equals(ext, ".sa")) return EditorMode::AsmC6x;
                if (target == TargetKind::Linux || target == TargetKind::macOS || String::Equals(ext, ".s")) return EditorMode::AsmGnu;
                return EditorMode::AsmMasm;
            }
            if (String::Equals(ext, ".pro") || String::Equals(ext, ".json")) return EditorMode::Json;
            return EditorMode::PlainText;
        }

        static String^ ModeName(EditorMode m)
        {
            switch (m)
            {
            case EditorMode::C90:      return "C90";
            case EditorMode::Cpp11:    return "C++11";
            case EditorMode::Shalimar: return "Shalimar";
            case EditorMode::AsmMasm:  return "MASM";
            case EditorMode::AsmC6x:   return "C6x Assembly";
            case EditorMode::AsmGnu:   return "GNU Assembly";
            case EditorMode::Json:     return "JSON";
            default:                   return "Plain Text";
            }
        }

        static bool IsCLike(EditorMode m)
        {
            return m == EditorMode::C90 || m == EditorMode::Cpp11 || m == EditorMode::Shalimar;
        }

        static array<String^>^ C90Keywords()
        {
            return gcnew array<String^>{
                "auto", "break", "case", "const", "continue", "default", "do", "else", "enum", "extern",
                "for", "goto", "if", "register", "return", "sizeof", "static", "struct", "switch",
                "typedef", "union", "volatile", "while" };
        }

        static array<String^>^ C90Types()
        {
            return gcnew array<String^>{ "char", "double", "float", "int", "long", "short", "signed", "unsigned", "void" };
        }

        static array<String^>^ Cpp11Keywords()
        {
            return gcnew array<String^>{
                "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor", "catch", "class", "compl",
                "constexpr", "const_cast", "decltype", "delete", "dynamic_cast", "explicit", "export",
                "false", "final", "friend", "inline", "mutable", "namespace", "new", "noexcept", "not",
                "not_eq", "nullptr", "operator", "or", "or_eq", "override", "private", "protected", "public",
                "reinterpret_cast", "static_assert", "static_cast", "template", "this", "thread_local",
                "throw", "true", "try", "typeid", "typename", "using", "virtual", "xor", "xor_eq" };
        }

        static array<String^>^ Cpp11Types()
        {
            return gcnew array<String^>{ "bool", "char16_t", "char32_t", "wchar_t", "auto" };
        }

        static array<String^>^ MasmKeywords()
        {
            return gcnew array<String^>{
                ".model", ".code", ".data", ".data?", ".const", ".stack", ".386", ".486", ".586", ".686", ".x64",
                "proc", "endp", "segment", "ends", "end", "proto", "invoke", "include", "includelib", "public",
                "extern", "externdef", "offset", "ptr", "addr", "local", "uses", "macro", "endm", "equ", "if",
                "else", "endif", "mov", "movzx", "movsx", "lea", "push", "pop", "call", "ret", "jmp", "je", "jne",
                "jz", "jnz", "jg", "jge", "jl", "jle", "ja", "jae", "jb", "jbe", "cmp", "test", "add", "sub",
                "mul", "imul", "div", "idiv", "inc", "dec", "and", "or", "xor", "not", "neg", "shl", "shr",
                "sar", "sal", "nop", "int", "loop", "xchg", "cdq", "cqo" };
        }

        static array<String^>^ MasmTypes()
        {
            return gcnew array<String^>{
                "byte", "word", "dword", "qword", "sbyte", "sword", "sdword", "real4", "real8", "db", "dw", "dd", "dq",
                "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp", "ax", "bx", "cx", "dx", "al", "ah", "bl",
                "bh", "cl", "ch", "dl", "dh", "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp", "r8", "r9",
                "r10", "r11", "r12", "r13", "r14", "r15" };
        }

        static array<String^>^ C6xKeywords()
        {
            return gcnew array<String^>{
                ".text", ".data", ".bss", ".sect", ".usect", ".global", ".def", ".ref", ".word", ".half", ".byte",
                ".float", ".double", ".string", ".align", ".space", ".end", ".asg", ".set", ".equ", ".include",
                ".copy", ".if", ".else", ".endif", ".macro", ".endm", ".cproc", ".endproc",
                "add", "addk", "addu", "sub", "subu", "mpy", "mpyh", "mpyu", "mv", "mvk", "mvkl", "mvkh", "mvc",
                "ldw", "ldh", "ldhu", "ldb", "ldbu", "lddw", "stw", "sth", "stb", "stdw", "b", "bnop", "nop",
                "idle", "cmpeq", "cmpgt", "cmpgtu", "cmplt", "cmpltu", "and", "or", "xor", "not", "shl", "shr",
                "shru", "sshl", "zero", "neg", "abs", "ext", "extu", "clr", "set", "norm", "lmbd" };
        }

        static List<String^>^ C6xRegisters()
        {
            List<String^>^ r = gcnew List<String^>();
            for (int i = 0; i < 32; i++)
            {
                r->Add(String::Format("a{0}", i));
                r->Add(String::Format("b{0}", i));
            }
            r->AddRange(gcnew array<String^>{ "amr", "csr", "ifr", "isr", "icr", "ier", "istp", "irp", "nrp", "pce1" });
            return r;
        }
    };

    // Regex tokenizer. Works on a chunk of text that begins at a line start.
    public ref class SyntaxHighlighter
    {
    public:
        SyntaxHighlighter(EditorMode mode, IdeSettings^ s)
        {
            this->mode = mode;
            hasBlockComments = false;
            bool ignoreCase = (mode == EditorMode::AsmMasm || mode == EditorMode::AsmC6x || mode == EditorMode::AsmGnu);
            StringComparer^ cmp = ignoreCase ? StringComparer::OrdinalIgnoreCase : StringComparer::Ordinal;
            keywords = gcnew HashSet<String^>(cmp);
            types = gcnew HashSet<String^>(cmp);
            String^ pattern = nullptr;

            String^ cPattern =
                R"RX((?<comment>//[^\n]*|/\*[\s\S]*?(?:\*/|\z))|(?<string>"(?:\\.|[^"\\\n])*"?|'(?:\\.|[^'\\\n])*'?)|(?<pre>^[ \t]*\#[ \t]*[A-Za-z_]+)|(?<num>\b(?:0[xX][0-9A-Fa-f]+|[0-9]+(?:\.[0-9]*)?(?:[eE][+-]?[0-9]+)?)[uUlLfF]*\b)|(?<id>\b[A-Za-z_][A-Za-z0-9_]*\b))RX";

            switch (mode)
            {
            case EditorMode::C90:
                AddAll(keywords, LanguageDefs::C90Keywords());
                AddAll(types, LanguageDefs::C90Types());
                pattern = cPattern; hasBlockComments = true;
                break;
            case EditorMode::Cpp11:
                AddAll(keywords, LanguageDefs::C90Keywords());
                AddAll(keywords, LanguageDefs::Cpp11Keywords());
                AddAll(types, LanguageDefs::C90Types());
                AddAll(types, LanguageDefs::Cpp11Types());
                pattern = cPattern; hasBlockComments = true;
                break;
            case EditorMode::Shalimar:
                if (s != nullptr && s->ShalimarKeywords != nullptr)
                    for each (String^ k in s->ShalimarKeywords) if (!String::IsNullOrWhiteSpace(k)) keywords->Add(k->Trim());
                pattern = cPattern; hasBlockComments = true;
                break;
            case EditorMode::AsmMasm:
                AddAll(keywords, LanguageDefs::MasmKeywords());
                AddAll(types, LanguageDefs::MasmTypes());
                pattern = R"RX((?<comment>;[^\n]*)|(?<string>"[^"\n]*"?|'[^'\n]*'?)|(?<num>\b(?:0[xX][0-9A-Fa-f]+|[0-9][0-9A-Fa-f]*[hH]|[0-9]+)\b)|(?<id>(?<![\w.?@$])\.?[A-Za-z_@$?][\w@$?]*))RX";
                break;
            case EditorMode::AsmC6x:
                AddAll(keywords, LanguageDefs::C6xKeywords());
                for each (String^ r in LanguageDefs::C6xRegisters()) types->Add(r);
                pattern = R"RX((?<comment>;[^\n]*|^\*[^\n]*)|(?<string>"[^"\n]*"?)|(?<num>\b(?:0[xX][0-9A-Fa-f]+|[0-9]+)\b)|(?<id>(?<![\w.$])\.?[A-Za-z_$][\w$]*))RX";
                break;
            case EditorMode::AsmGnu:
                for each (String^ k in LanguageDefs::C6xKeywords()) if (k->StartsWith(".")) keywords->Add(k);
                AddAll(keywords, gcnew array<String^>{ ".globl", ".section", ".type", ".size", ".long", ".quad", ".ascii", ".asciz", ".intel_syntax", ".att_syntax" });
                AddAll(keywords, LanguageDefs::MasmKeywords());
                AddAll(types, LanguageDefs::MasmTypes());
                pattern = R"RX((?<comment>\#[^\n]*|//[^\n]*|/\*[\s\S]*?(?:\*/|\z))|(?<string>"(?:\\.|[^"\\\n])*"?)|(?<num>\$?\b(?:0[xX][0-9A-Fa-f]+|[0-9]+)\b)|(?<id>(?<![\w.%$])[.%]?[A-Za-z_][\w$]*))RX";
                hasBlockComments = true;
                break;
            case EditorMode::Json:
                keywords->Add("true"); keywords->Add("false"); keywords->Add("null");
                pattern = R"RX((?<string>"(?:\\.|[^"\\\n])*"?)|(?<num>-?\b[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?\b)|(?<id>\b[a-z]+\b))RX";
                break;
            default:
                break;
            }
            if (pattern != nullptr)
                regex = gcnew Regex(pattern, RegexOptions::Multiline | RegexOptions::CultureInvariant);
        }

        property bool Enabled { bool get() { return regex != nullptr; } }
        property bool HasBlockComments { bool get() { return hasBlockComments; } }

        // chunk = text starting at a line start; offset = its index in the document
        List<TokenSpan>^ Tokenize(String^ chunk, int offset)
        {
            List<TokenSpan>^ spans = gcnew List<TokenSpan>();
            if (regex == nullptr || String::IsNullOrEmpty(chunk)) return spans;
            for each (Match^ m in regex->Matches(chunk))
            {
                TokenKind k;
                if (m->Groups["comment"]->Success)      k = TokenKind::Comment;
                else if (m->Groups["string"]->Success)  k = TokenKind::StringLit;
                else if (m->Groups["pre"]->Success)     k = TokenKind::Preprocessor;
                else if (m->Groups["num"]->Success)     k = TokenKind::Number;
                else if (m->Groups["id"]->Success)
                {
                    String^ w = m->Value;
                    if (keywords->Contains(w))   k = TokenKind::Keyword;
                    else if (types->Contains(w)) k = TokenKind::Type;
                    else continue;
                }
                else continue;

                TokenSpan sp;
                sp.Start = offset + m->Index;
                sp.Length = m->Length;
                sp.Kind = k;
                spans->Add(sp);
            }
            return spans;
        }

    private:
        EditorMode mode;
        Regex^ regex;
        HashSet<String^>^ keywords;
        HashSet<String^>^ types;
        bool hasBlockComments;

        static void AddAll(HashSet<String^>^ set, array<String^>^ items)
        {
            for each (String^ s in items) set->Add(s);
        }
    };
}

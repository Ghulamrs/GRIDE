// =============================================================================
//  MacroExpander.h - replaces $(Name) macros in command templates
//     "$(Assembler)" "$(AsmFile)"  ->  "C:\GRIDE\bin\c6x\asm6x.exe" "obj\..\main.asm"
//  Unknown macros are left untouched so mistakes are visible in the Output pane.
// =============================================================================
#pragma once
#include "../pch.h"

namespace Gride { namespace Services {

    public ref class MacroExpander
    {
    public:
        MacroExpander() { values = gcnew Dictionary<String^, String^>(StringComparer::OrdinalIgnoreCase); }

        void Set(String^ name, String^ value)
        {
            if (value == nullptr) value = String::Empty;
            values[name] = value;
        }

        String^ Get(String^ name)
        {
            String^ v;
            if (values->TryGetValue(name, v)) return v;
            return String::Empty;
        }

        String^ Expand(String^ text)
        {
            if (String::IsNullOrEmpty(text)) return String::Empty;
            String^ r = Regex::Replace(text, "\\$\\((\\w+)\\)", gcnew MatchEvaluator(this, &MacroExpander::Evaluate));
            // collapse runs of spaces left by empty macros
            return Regex::Replace(r, "  +", " ")->Trim();
        }

        // "-I" + list -> -I"a" -I"b", each item quoted as the C runtime splits it (Quoted): a folder ending in a
        // backslash, -I"inc\", would otherwise end in \" - a quote of the text, not the closing one - and run on into
        // the next items, whose spaces then split them into stray arguments (cpp11: "-o names a single output, but 3
        // inputs were given")
        static String^ JoinFlags(String^ flag, IEnumerable<String^>^ items)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            if (items == nullptr) return String::Empty;
            for each (String^ s in items)
            {
                if (String::IsNullOrEmpty(s)) continue;
                if (sb->Length > 0) sb->Append(L' ');
                sb->Append(flag)->Append(Quoted(s));
            }
            return sb->ToString();
        }

        // "-D" + list -> -DNAME -DNAME=1 -D"GREETING=hi there": as RIDE's options.cpp quotes them, only a define with a
        // space, a tab or a quote in it, which then stays one argument (QuoteEntry)
        static String^ JoinDefines(String^ flag, IEnumerable<String^>^ items)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            if (items == nullptr) return String::Empty;
            for each (String^ s in items)
            {
                if (String::IsNullOrEmpty(s)) continue;
                if (sb->Length > 0) sb->Append(L' ');
                sb->Append(flag)->Append(QuoteEntry(s));
            }
            return sb->ToString();
        }

        // a define (any one entry of a list) as one argument: as it is, unless it holds a space, a tab or a quote -
        // then Quoted: MSG="hi there" -> "MSG=\"hi there\""
        static String^ QuoteEntry(String^ s)
        {
            if (s->IndexOfAny(gcnew array<wchar_t>{ L' ', L'\t', L'"' }) < 0) return s;
            return Quoted(s);
        }

        // s in quotes as one argument, as the C runtime splits a command line: a quote inside escaped as \", and the
        // backslashes in front of a quote, or of the closing one, doubled - C:\inc\ -> "C:\inc\\"
        static String^ Quoted(String^ s)
        {
            StringBuilder^ sb = gcnew StringBuilder("\"");
            int slashes = 0;
            for each (wchar_t c in s)
            {
                if (c == L'\\') { slashes++; continue; }
                sb->Append(L'\\', c == L'"' ? 2 * slashes + 1 : slashes)->Append(c);
                slashes = 0;
            }
            return sb->Append(L'\\', 2 * slashes)->Append(L'"')->ToString();
        }

        // plain list of quoted items: "a.obj" "b.obj"
        static String^ JoinQuoted(IEnumerable<String^>^ items)
        {
            return JoinFlags(String::Empty, items);
        }

        property Dictionary<String^, String^>^ Values { Dictionary<String^, String^>^ get() { return values; } }

    private:
        Dictionary<String^, String^>^ values;

        String^ Evaluate(Match^ m)
        {
            String^ v;
            if (values->TryGetValue(m->Groups[1]->Value, v)) return v;
            return m->Value;
        }
    };
}}

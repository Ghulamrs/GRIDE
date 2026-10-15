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

        // "-I" + list -> -I"a" -I"b"
        static String^ JoinFlags(String^ flag, IEnumerable<String^>^ items)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            if (items == nullptr) return String::Empty;
            for each (String^ s in items)
            {
                if (String::IsNullOrEmpty(s)) continue;
                if (sb->Length > 0) sb->Append(L' ');
                sb->Append(flag);
                sb->Append(L'"');
                sb->Append(s);
                sb->Append(L'"');
            }
            return sb->ToString();
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

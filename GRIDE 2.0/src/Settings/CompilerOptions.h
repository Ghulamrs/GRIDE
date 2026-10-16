#pragma once
// ============================================================================
//  GRIDE 2.0 - RIDE's Compiler Options (options.cpp), ported from GRIDEd's
//  CompilerOptions.h and RideToolchain's OptionFlags
//  CompilerOptionStore : one value per option id and configuration, kept only
//                        where it differs from the definition's default, so an
//                        untouched project's file is unchanged; a .pro carries
//                        it as RIDE writes it - "options": { "debug": {...},
//                        "release": {...} } - and the settings carry the store a
//                        new project starts with
//  CompilerOptionRules : what a store becomes for a compiler and a target - the
//                        flags in the order the tool's options list them, the
//                        availability with RIDE's sentences, the environment
//  The table itself (ids, tabs, controls, defaults, flags) is toolchain.json's
//  compilerOptions section; no flag of it is in the code.
// ============================================================================
#include "../Toolchain/VendorTools.h"

namespace Gride
{
    public ref class CompilerOptionStore
    {
    public:
        CompilerOptionStore() { values = gcnew Dictionary<String^, Dictionary<String^, String^>^>(); }

        static array<String^>^ Configurations() { return gcnew array<String^>{ "Debug", "Release" }; }

        // RIDE's two configurations, however a caller spells them (a name saying Release is Release)
        static String^ Canon(String^ config)
        {
            return config != nullptr && config->IndexOf("release", StringComparison::OrdinalIgnoreCase) >= 0 ? Str("Release") : Str("Debug");
        }
        static String^ RideName(String^ config) { return Canon(config)->ToLowerInvariant(); }

        // the definition's default for a configuration; "" for an option the file does not know
        static String^ DefaultValue(String^ config, String^ id)
        {
            CompilerOptionEntry^ d = ToolchainFile::Current->CompilerOption(id);
            return d == nullptr ? String::Empty : d->DefaultFor(Canon(config));
        }

        String^ Value(String^ config, String^ id)
        {
            Dictionary<String^, String^>^ one;
            String^ v;
            if (values->TryGetValue(Canon(config), one) && one->TryGetValue(id, v)) return v;
            return DefaultValue(config, id);
        }

        // a value equal to the default is not kept, as RIDE's set
        void Set(String^ config, String^ id, String^ value)
        {
            String^ c = Canon(config);
            Dictionary<String^, String^>^ one;
            if (!values->TryGetValue(c, one)) { one = gcnew Dictionary<String^, String^>(); values[c] = one; }
            if (value == nullptr || String::Equals(value, DefaultValue(config, id))) one->Remove(id);
            else one[id] = value;
        }

        void Reset(String^ config)
        {
            Dictionary<String^, String^>^ one;
            if (values->TryGetValue(Canon(config), one)) one->Clear();
        }

        property bool Empty
        {
            bool get()
            {
                for each (KeyValuePair<String^, Dictionary<String^, String^>^> kv in values) if (kv.Value->Count > 0) return false;
                return true;
            }
        }

        // the form a .pro and the settings keep: RIDE's lowercase configuration names, the differences
        // only; nullptr when there are none (so nothing is written)
        Dictionary<String^, Dictionary<String^, String^>^>^ ToData()
        {
            if (Empty) return nullptr;
            Dictionary<String^, Dictionary<String^, String^>^>^ r = gcnew Dictionary<String^, Dictionary<String^, String^>^>();
            for each (String^ c in Configurations())
            {
                Dictionary<String^, String^>^ one;
                if (!values->TryGetValue(c, one) || one->Count == 0) continue;
                r[RideName(c)] = gcnew Dictionary<String^, String^>(one);
            }
            return r;
        }

        // either spelling of a configuration reads; an id the definitions do not know is left out (RIDE's fromJson)
        static CompilerOptionStore^ FromData(Dictionary<String^, Dictionary<String^, String^>^>^ data)
        {
            CompilerOptionStore^ s = gcnew CompilerOptionStore();
            if (data == nullptr) return s;
            for each (KeyValuePair<String^, Dictionary<String^, String^>^> kv in data)
            {
                if (kv.Value == nullptr) continue;
                for each (KeyValuePair<String^, String^> o in kv.Value)
                    if (ToolchainFile::Current->CompilerOption(o.Key) != nullptr) s->Set(kv.Key, o.Key, o.Value == nullptr ? String::Empty : o.Value);
            }
            return s;
        }

        CompilerOptionStore^ Clone() { return FromData(ToData()); }

        void CopyFrom(CompilerOptionStore^ other)
        {
            values->Clear();
            if (other == nullptr) return;
            CompilerOptionStore^ c = FromData(other->ToData());
            for each (KeyValuePair<String^, Dictionary<String^, String^>^> kv in c->values) values[kv.Key] = kv.Value;
        }

        // the entries of a text option: ';' between them, blanks trimmed, empty ones dropped (RIDE's entries)
        static List<String^>^ Entries(String^ list)
        {
            List<String^>^ r = gcnew List<String^>();
            if (list == nullptr) return r;
            for each (String^ s in list->Split(gcnew array<wchar_t>{ L';' }))
            {
                String^ t = s->Trim(gcnew array<wchar_t>{ L' ', L'\t' });
                if (t->Length > 0) r->Add(t);
            }
            return r;
        }

    private:
        Dictionary<String^, Dictionary<String^, String^>^>^ values;
    };

    // RIDE's options::flags, available, environment, on the file's table and a target of the file
    public ref class CompilerOptionRules abstract sealed
    {
    public:
        // whether an option means anything for a target, and why not when it does not: the option's needs
        // (lineTable: the compiler writes debug lines there; emulated / notEmulated) and its sentence
        static bool Available(String^ id, TargetEntry^ target, String^% why)
        {
            why = String::Empty;
            CompilerOptionEntry^ o = ToolchainFile::Current->CompilerOption(id);
            if (o == nullptr || String::IsNullOrEmpty(o->Needs)) return true;
            bool emulated = target != nullptr && target->Emulated;
            bool ok;
            if (String::Compare(o->Needs, "lineTable", true) == 0)
            {
                ToolEntry^ tool = ToolchainFile::Current->ToolOfOption(id);
                ok = (tool != nullptr && !tool->TakesArch()) || (target != nullptr && !String::IsNullOrEmpty(target->DebugInfo));
            }
            else if (String::Compare(o->Needs, "emulated", true) == 0) ok = emulated;
            else if (String::Compare(o->Needs, "notEmulated", true) == 0) ok = !emulated;
            else ok = true;
            if (!ok) why = o->Unavailable->Replace("$(Arch)", target == nullptr ? String::Empty : target->Arch);
            return ok;
        }

        // the flags a compiler is given for a configuration and target, in the order the tool's options list
        // them: a check's flagOn / flagOff, a choice's flag unless it is the silent one, a text's flagEach per
        // entry (quoted when it holds a space); an option that does not apply to the target writes nothing
        static String^ Flags(CompilerOptionStore^ store, ToolEntry^ tool, String^ config, TargetEntry^ target)
        {
            if (store == nullptr || tool == nullptr) return String::Empty;
            List<String^>^ out = gcnew List<String^>();
            String^ why;
            for each (String^ id in tool->Options)
            {
                CompilerOptionEntry^ o = ToolchainFile::Current->CompilerOption(id);
                if (o == nullptr || o->EnvironmentVariable->Length > 0) continue;
                String^ value = store->Value(config, id);
                if (o->IsCheck())
                {
                    if (value == "1" && o->FlagOn->Length > 0 && Available(id, target, why)) out->Add(o->FlagOn);
                    else if (value == "0" && o->FlagOff->Length > 0 && Available(id, target, why)) out->Add(o->FlagOff);
                }
                else if (o->IsChoice())
                {
                    if (value != o->SilentChoice() && value->Length > 0 && o->Flag->Length > 0 && Available(id, target, why))
                        out->Add(o->Flag->Replace("$(Value)", value));
                }
                else if (o->FlagEach->Length > 0 && Available(id, target, why))
                {
                    for each (String^ item in CompilerOptionStore::Entries(value))
                        out->Add(o->FlagEach->Replace("$(Item)", item->IndexOfAny(gcnew array<wchar_t>{ L' ', L'\t', L'"' }) < 0 ? item : String::Concat("\"", item, "\"")));
                }
            }
            return String::Join(" ", out);
        }

        // NAME=value for every option of the tool that names a variable (RIDE's CPP11_DECLINES: 1 when asked for, empty otherwise)
        static List<String^>^ Environment(CompilerOptionStore^ store, ToolEntry^ tool, String^ config)
        {
            List<String^>^ r = gcnew List<String^>();
            if (tool == nullptr) return r;
            for each (String^ id in tool->Options)
            {
                CompilerOptionEntry^ o = ToolchainFile::Current->CompilerOption(id);
                if (o == nullptr || o->EnvironmentVariable->Length == 0) continue;
                String^ value = store == nullptr ? o->DefaultFor(CompilerOptionStore::Canon(config)) : store->Value(config, id);
                r->Add(String::Concat(o->EnvironmentVariable, "=", o->IsCheck() ? (value == "1" ? Str("1") : String::Empty) : value));
            }
            return r;
        }

        // whether a compiler takes its configuration flags from Compiler Options (it lists options, and the file has the table)
        static bool Governs(ToolEntry^ tool)
        {
            return tool != nullptr && tool->Options->Count > 0 && ToolchainFile::Current->CompilerOptions->Count > 0;
        }
    };
}

// =============================================================================
//  CompilerOptions.h - RIDE's Compiler Options store (options.cpp's Store): one value per
//  option id and configuration, kept only where it differs from the definition's default,
//  so an untouched project's file is unchanged. The table itself (ids, controls, defaults,
//  flags) is toolchain.json's compilerOptions section; what a store becomes on a command
//  line is RideToolchain::OptionFlags. A project carries its store in the .pro under
//  "options" as RIDE does ({ "debug": {...}, "release": {...} }); the settings carry one
//  under "compilerOptions" - the options a new project starts with.
// =============================================================================
#pragma once
#include "../Core/Json.h"
#include "ToolchainDefinitions.h"

namespace Gride { namespace Settings {

    using namespace Gride::Core;

    public ref class CompilerOptionStore
    {
    public:
        CompilerOptionStore() { values = gcnew Dictionary<String^, Dictionary<String^, String^>^>(); }

        static array<String^>^ Configurations() { return gcnew array<String^>{ "Debug", "Release" }; }

        // RIDE's two configurations, however a caller spells them
        static String^ Canon(String^ config)
        {
            return config != nullptr && String::Compare(config->Trim(), "Release", true) == 0 ? "Release" : "Debug";
        }
        static String^ RideName(String^ config) { return Canon(config)->ToLowerInvariant(); }

        // the definition's default for a configuration; "" for an option the file does not know
        static String^ DefaultValue(String^ config, String^ id)
        {
            CompilerOptionDefinition^ d = ToolchainDefinitions::Current->CompilerOption(id);
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

        // only what differs from the defaults, under RIDE's lowercase configuration names
        JsonValue^ ToJson()
        {
            JsonValue^ root = JsonValue::MakeObject();
            for each (String^ c in Configurations())
            {
                Dictionary<String^, String^>^ one;
                if (!values->TryGetValue(c, one) || one->Count == 0) continue;
                JsonValue^ o = JsonValue::MakeObject();
                for each (KeyValuePair<String^, String^> kv in one) o->SetString(kv.Key, kv.Value);
                root->SetValue(RideName(c), o);
            }
            return root;
        }

        // an id the definitions do not know is left out, as RIDE's fromJson; either spelling of the configuration reads
        void FromJson(JsonValue^ json)
        {
            values->Clear();
            if (json == nullptr || !json->IsObject) return;
            for each (String^ key in json->KeyNames)
            {
                JsonValue^ one = json->GetObject(key);
                if (one == nullptr) continue;
                String^ c = Canon(key);
                for each (String^ id in one->KeyNames)
                    if (ToolchainDefinitions::Current->CompilerOption(id) != nullptr) Set(c, id, one->GetString(id, String::Empty));
            }
        }

        CompilerOptionStore^ Clone()
        {
            CompilerOptionStore^ c = gcnew CompilerOptionStore();
            c->FromJson(ToJson());
            return c;
        }

        void CopyFrom(CompilerOptionStore^ other)
        {
            values->Clear();
            if (other != nullptr) FromJson(other->ToJson());
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
}}

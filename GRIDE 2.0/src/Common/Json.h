#pragma once
// ============================================================================
//  GRIDE 2.0 - JSON with keys in order (toolchain.json)
//  DataContractJsonSerializer (Common.h) reads the IDE's own files; the
//  toolchain definition file is a document the IDE merges and writes back
//  field by field, so it is read into a tree: JsonValue is one node, JsonParser
//  makes the tree from text, JsonWriter prints it with 2-space indent and the
//  keys in the order they were read.
// ============================================================================
#include "Common.h"

using namespace System::Globalization;

namespace Gride
{
    public enum class JsonKind { Null, Bool, Number, String, Array, Object };

    public ref class JsonValue
    {
    public:
        JsonValue()
        {
            kind = JsonKind::Null;
            str = nullptr; num = 0.0; flag = false;
            items = nullptr; map = nullptr; order = nullptr;
        }

        static JsonValue^ MakeObject()
        {
            JsonValue^ v = gcnew JsonValue();
            v->kind = JsonKind::Object;
            v->map = gcnew Dictionary<String^, JsonValue^>();
            v->order = gcnew List<String^>();
            return v;
        }
        static JsonValue^ MakeArray()
        {
            JsonValue^ v = gcnew JsonValue();
            v->kind = JsonKind::Array;
            v->items = gcnew List<JsonValue^>();
            return v;
        }
        static JsonValue^ FromString(String^ s)
        {
            JsonValue^ v = gcnew JsonValue();
            if (s == nullptr) return v;
            v->kind = JsonKind::String; v->str = s;
            return v;
        }
        static JsonValue^ FromNumber(double d)
        {
            JsonValue^ v = gcnew JsonValue();
            v->kind = JsonKind::Number; v->num = d;
            return v;
        }
        static JsonValue^ FromBool(bool b)
        {
            JsonValue^ v = gcnew JsonValue();
            v->kind = JsonKind::Bool; v->flag = b;
            return v;
        }
        static JsonValue^ FromList(IEnumerable<String^>^ list)
        {
            JsonValue^ a = MakeArray();
            if (list != nullptr)
                for each (String^ s in list) a->Add(FromString(s));
            return a;
        }

        property JsonKind Kind { JsonKind get() { return kind; } }
        property bool IsObject { bool get() { return kind == JsonKind::Object; } }
        property bool IsArray  { bool get() { return kind == JsonKind::Array; } }

        String^ AsString()
        {
            switch (kind)
            {
            case JsonKind::String: return str;
            case JsonKind::Number: return num.ToString(CultureInfo::InvariantCulture);
            case JsonKind::Bool:   return flag ? Str("true") : Str("false");
            default:               return nullptr;
            }
        }
        double AsNumber()
        {
            if (kind == JsonKind::Number) return num;
            if (kind == JsonKind::String)
            {
                double d;
                if (Double::TryParse(str, NumberStyles::Float, CultureInfo::InvariantCulture, d)) return d;
            }
            if (kind == JsonKind::Bool) return flag ? 1.0 : 0.0;
            return 0.0;
        }
        bool AsBool()
        {
            if (kind == JsonKind::Bool) return flag;
            if (kind == JsonKind::Number) return num != 0.0;
            if (kind == JsonKind::String) return String::Compare(str, "true", true) == 0;
            return false;
        }

        property int Count
        {
            int get()
            {
                if (kind == JsonKind::Array)  return items->Count;
                if (kind == JsonKind::Object) return order->Count;
                return 0;
            }
        }
        JsonValue^ At(int i) { if (kind == JsonKind::Array && i >= 0 && i < items->Count) return items[i]; return nullptr; }
        void Add(JsonValue^ v)
        {
            if (kind != JsonKind::Array) return;
            items->Add(v == nullptr ? gcnew JsonValue() : v);
        }
        void AddString(String^ s) { Add(FromString(s)); }
        List<String^>^ ToStringList()
        {
            List<String^>^ r = gcnew List<String^>();
            if (kind == JsonKind::Array)
                for each (JsonValue^ v in items)
                {
                    String^ s = v->AsString();
                    if (s != nullptr) r->Add(s);
                }
            return r;
        }

        property List<String^>^ KeyNames
        {
            List<String^>^ get() { return kind == JsonKind::Object ? gcnew List<String^>(order) : gcnew List<String^>(); }
        }
        bool Has(String^ key) { return kind == JsonKind::Object && map->ContainsKey(key); }
        JsonValue^ Get(String^ key)
        {
            if (kind != JsonKind::Object) return nullptr;
            JsonValue^ v;
            if (map->TryGetValue(key, v)) return v;
            return nullptr;
        }
        void SetValue(String^ key, JsonValue^ v)
        {
            if (kind != JsonKind::Object) return;
            if (v == nullptr) v = gcnew JsonValue();
            if (!map->ContainsKey(key)) order->Add(key);
            map[key] = v;
        }
        void Remove(String^ key)
        {
            if (kind != JsonKind::Object || !map->ContainsKey(key)) return;
            map->Remove(key); order->Remove(key);
        }
        // distinct names: an overloaded Set("k", "text") would bind the literal to bool
        void SetString(String^ key, String^ s)   { SetValue(key, FromString(s)); }
        void SetNumber(String^ key, double d)    { SetValue(key, FromNumber(d)); }
        void SetBool(String^ key, bool b)        { SetValue(key, FromBool(b)); }
        void SetList(String^ key, IEnumerable<String^>^ l) { SetValue(key, FromList(l)); }

        String^ GetString(String^ key, String^ def)
        {
            JsonValue^ v = Get(key);
            if (v == nullptr || v->kind == JsonKind::Null) return def;
            String^ s = v->AsString();
            return s == nullptr ? def : s;
        }
        double GetNumber(String^ key, double def)
        {
            JsonValue^ v = Get(key);
            return (v == nullptr || v->kind == JsonKind::Null) ? def : v->AsNumber();
        }
        int GetInt(String^ key, int def) { return (int)Math::Round(GetNumber(key, (double)def)); }
        bool GetBool(String^ key, bool def)
        {
            JsonValue^ v = Get(key);
            return (v == nullptr || v->kind == JsonKind::Null) ? def : v->AsBool();
        }
        List<String^>^ GetStringList(String^ key)
        {
            JsonValue^ v = Get(key);
            return v == nullptr ? gcnew List<String^>() : v->ToStringList();
        }
        JsonValue^ GetObject(String^ key)
        {
            JsonValue^ v = Get(key);
            if (v != nullptr && v->IsObject) return v;
            return nullptr;
        }

    internal:
        JsonKind kind;
        String^ str;
        double num;
        bool flag;
        List<JsonValue^>^ items;
        Dictionary<String^, JsonValue^>^ map;
        List<String^>^ order;
    };

    public ref class JsonException : public Exception
    {
    public:
        JsonException(String^ msg, int line, int col)
            : Exception(String::Format("JSON error at line {0}, column {1}: {2}", line, col, msg)) {}
    };

    public ref class JsonParser
    {
    public:
        static JsonValue^ Parse(String^ text)
        {
            if (text == nullptr) text = String::Empty;
            JsonParser^ p = gcnew JsonParser(text);
            p->SkipWs();
            JsonValue^ v = p->ParseValue();
            p->SkipWs();
            if (p->pos < p->src->Length) p->Fail("unexpected text after the end of the document");
            return v;
        }
        static JsonValue^ ParseFile(String^ path)
        {
            return Parse(File::ReadAllText(path, Encoding::UTF8));
        }

    private:
        String^ src;
        int pos;

        JsonParser(String^ s) { src = s; pos = 0; }

        void Fail(String^ msg)
        {
            int line = 1, col = 1;
            for (int i = 0; i < pos && i < src->Length; i++)
            {
                if (src[i] == L'\n') { line++; col = 1; } else col++;
            }
            throw gcnew JsonException(msg, line, col);
        }

        // // line comments are taken: the shipped file's header is one
        void SkipWs()
        {
            while (pos < src->Length)
            {
                wchar_t c = src[pos];
                if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') { pos++; continue; }
                if (c == L'/' && pos + 1 < src->Length && src[pos + 1] == L'/')
                {
                    while (pos < src->Length && src[pos] != L'\n') pos++;
                    continue;
                }
                break;
            }
        }

        JsonValue^ ParseValue()
        {
            if (pos >= src->Length) Fail("unexpected end of document");
            wchar_t c = src[pos];
            if (c == L'{') return ParseObject();
            if (c == L'[') return ParseArray();
            if (c == L'"') return JsonValue::FromString(ParseString());
            if (c == L't') { Expect("true");  return JsonValue::FromBool(true); }
            if (c == L'f') { Expect("false"); return JsonValue::FromBool(false); }
            if (c == L'n') { Expect("null");  return gcnew JsonValue(); }
            if (c == L'-' || (c >= L'0' && c <= L'9')) return ParseNumber();
            Fail(String::Format("unexpected character '{0}'", c));
            return nullptr;
        }

        void Expect(String^ word)
        {
            if (String::CompareOrdinal(src, pos, word, 0, word->Length) != 0) Fail("expected '" + word + "'");
            pos += word->Length;
        }

        JsonValue^ ParseObject()
        {
            JsonValue^ obj = JsonValue::MakeObject();
            pos++;
            SkipWs();
            if (pos < src->Length && src[pos] == L'}') { pos++; return obj; }
            while (true)
            {
                SkipWs();
                if (pos >= src->Length || src[pos] != L'"') Fail("expected a quoted key");
                String^ key = ParseString();
                SkipWs();
                if (pos >= src->Length || src[pos] != L':') Fail("expected ':'");
                pos++;
                SkipWs();
                obj->SetValue(key, ParseValue());
                SkipWs();
                if (pos >= src->Length) Fail("unterminated object");
                if (src[pos] == L',') { pos++; SkipWs(); if (pos < src->Length && src[pos] == L'}') { pos++; return obj; } continue; }
                if (src[pos] == L'}') { pos++; return obj; }
                Fail("expected ',' or '}'");
            }
        }

        JsonValue^ ParseArray()
        {
            JsonValue^ arr = JsonValue::MakeArray();
            pos++;
            SkipWs();
            if (pos < src->Length && src[pos] == L']') { pos++; return arr; }
            while (true)
            {
                SkipWs();
                arr->Add(ParseValue());
                SkipWs();
                if (pos >= src->Length) Fail("unterminated array");
                if (src[pos] == L',') { pos++; SkipWs(); if (pos < src->Length && src[pos] == L']') { pos++; return arr; } continue; }
                if (src[pos] == L']') { pos++; return arr; }
                Fail("expected ',' or ']'");
            }
        }

        String^ ParseString()
        {
            StringBuilder^ sb = gcnew StringBuilder();
            pos++;
            while (pos < src->Length)
            {
                wchar_t c = src[pos++];
                if (c == L'"') return sb->ToString();
                if (c != L'\\') { sb->Append(c); continue; }
                if (pos >= src->Length) break;
                wchar_t e = src[pos++];
                switch (e)
                {
                case L'"':  sb->Append(L'"');  break;
                case L'\\': sb->Append(L'\\'); break;
                case L'/':  sb->Append(L'/');  break;
                case L'b':  sb->Append(L'\b'); break;
                case L'f':  sb->Append(L'\f'); break;
                case L'n':  sb->Append(L'\n'); break;
                case L'r':  sb->Append(L'\r'); break;
                case L't':  sb->Append(L'\t'); break;
                case L'u':
                {
                    if (pos + 4 > src->Length) Fail("bad \\u escape");
                    int code = 0;
                    if (!Int32::TryParse(src->Substring(pos, 4), NumberStyles::HexNumber, CultureInfo::InvariantCulture, code))
                        Fail("bad \\u escape");
                    sb->Append((wchar_t)code);
                    pos += 4;
                    break;
                }
                default: Fail("bad escape sequence");
                }
            }
            Fail("unterminated string");
            return nullptr;
        }

        JsonValue^ ParseNumber()
        {
            int start = pos;
            if (src[pos] == L'-') pos++;
            while (pos < src->Length)
            {
                wchar_t c = src[pos];
                if ((c >= L'0' && c <= L'9') || c == L'.' || c == L'e' || c == L'E' || c == L'+' || c == L'-') pos++;
                else break;
            }
            double d;
            if (!Double::TryParse(src->Substring(start, pos - start), NumberStyles::Float, CultureInfo::InvariantCulture, d))
                Fail("bad number");
            return JsonValue::FromNumber(d);
        }
    };

    public ref class JsonWriter
    {
    public:
        static String^ Write(JsonValue^ v)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            WriteValue(sb, v, 0);
            sb->Append("\r\n");
            return sb->ToString();
        }
        // through a temp file, so a crash never leaves a half-written file
        static void WriteFile(String^ path, JsonValue^ v)
        {
            String^ full = Path::GetFullPath(path);
            String^ dir = Path::GetDirectoryName(full);
            if (!String::IsNullOrEmpty(dir) && !Directory::Exists(dir)) Directory::CreateDirectory(dir);
            String^ tmp = String::Concat(full, ".tmp");
            File::WriteAllText(tmp, Write(v), gcnew UTF8Encoding(false));
            try
            {
                if (!File::Exists(full)) { File::Move(tmp, full); return; }
                try { File::Replace(tmp, full, nullptr); }
                catch (IOException^) { File::Copy(tmp, full, true); File::Delete(tmp); }
                catch (PlatformNotSupportedException^) { File::Copy(tmp, full, true); File::Delete(tmp); }
            }
            catch (Exception^)
            {
                try { File::Delete(tmp); } catch (Exception^) { }
                throw;
            }
        }
        static String^ Quote(String^ s)
        {
            StringBuilder^ sb = gcnew StringBuilder();
            sb->Append(L'"');
            for each (wchar_t c in s)
            {
                switch (c)
                {
                case L'"':  sb->Append("\\\""); break;
                case L'\\': sb->Append("\\\\"); break;
                case L'\n': sb->Append("\\n");  break;
                case L'\r': sb->Append("\\r");  break;
                case L'\t': sb->Append("\\t");  break;
                default:
                    if (c < 0x20) sb->AppendFormat("\\u{0:x4}", (int)c);
                    else sb->Append(c);
                }
            }
            sb->Append(L'"');
            return sb->ToString();
        }

    private:
        static void Indent(StringBuilder^ sb, int level) { sb->Append(L' ', level * 2); }

        static bool IsScalarArray(JsonValue^ v)
        {
            for each (JsonValue^ i in v->items)
                if (i->kind == JsonKind::Object || i->kind == JsonKind::Array) return false;
            return true;
        }

        static void WriteValue(StringBuilder^ sb, JsonValue^ v, int level)
        {
            if (v == nullptr) { sb->Append("null"); return; }
            switch (v->kind)
            {
            case JsonKind::Null:   sb->Append("null"); break;
            case JsonKind::Bool:   sb->Append(v->flag ? "true" : "false"); break;
            case JsonKind::Number: sb->Append(v->num.ToString("R", CultureInfo::InvariantCulture)); break;
            case JsonKind::String: sb->Append(Quote(v->str)); break;
            case JsonKind::Array:
            {
                if (v->items->Count == 0) { sb->Append("[]"); break; }
                if (IsScalarArray(v))
                {
                    // a short list of scalars stays on one line
                    StringBuilder^ line = gcnew StringBuilder();
                    line->Append(L'[');
                    for (int i = 0; i < v->items->Count; i++)
                    {
                        if (i > 0) line->Append(", ");
                        WriteValue(line, v->items[i], 0);
                    }
                    line->Append(L']');
                    if (line->Length <= 90) { sb->Append(line->ToString()); break; }
                }
                sb->Append("[\r\n");
                for (int i = 0; i < v->items->Count; i++)
                {
                    Indent(sb, level + 1);
                    WriteValue(sb, v->items[i], level + 1);
                    if (i < v->items->Count - 1) sb->Append(L',');
                    sb->Append("\r\n");
                }
                Indent(sb, level);
                sb->Append(L']');
                break;
            }
            case JsonKind::Object:
            {
                if (v->order->Count == 0) { sb->Append("{}"); break; }
                sb->Append("{\r\n");
                for (int i = 0; i < v->order->Count; i++)
                {
                    String^ k = v->order[i];
                    Indent(sb, level + 1);
                    sb->Append(Quote(k));
                    sb->Append(": ");
                    WriteValue(sb, v->map[k], level + 1);
                    if (i < v->order->Count - 1) sb->Append(L',');
                    sb->Append("\r\n");
                }
                Indent(sb, level);
                sb->Append(L'}');
                break;
            }
            }
        }
    };
}

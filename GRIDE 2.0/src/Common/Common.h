#pragma once
// ============================================================================
//  GRIDE 2.0 - Common helpers
//  JSON (DataContractJsonSerializer, built into .NET 4.8), paths, text files,
//  explicit Win32 P/Invoke (no windows.h, avoids WinForms name clashes).
// ============================================================================

using namespace System;
using namespace System::IO;
using namespace System::Text;
using namespace System::Text::RegularExpressions;
using namespace System::Collections::Generic;
using namespace System::Runtime::Serialization;
using namespace System::Runtime::Serialization::Json;
using namespace System::Runtime::InteropServices;

namespace Gride
{
    // ---------------------------------------------------------------- Win32
    // Explicit P/Invoke in a ref class (documented C++/CLI pattern).
    public ref class NativeMethods abstract sealed
    {
    public:
        [DllImport("user32.dll", EntryPoint = "SendMessageW")]
        static IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        literal int WM_SETREDRAW           = 0x000B;
        literal int WM_HSCROLL             = 0x0114;
        literal int WM_VSCROLL             = 0x0115;
        literal int WM_MOUSEWHEEL          = 0x020A;
        literal int EM_LINESCROLL          = 0x00B6;
        literal int EM_GETFIRSTVISIBLELINE = 0x00CE;
        literal int EM_SETUNDOLIMIT        = 0x0452;
        literal int EM_GETSCROLLPOS        = 0x04DD;
        literal int EM_SETSCROLLPOS        = 0x04DE;
    };

    public ref class AppInfo abstract sealed
    {
    public:
        literal String^ Name    = "GRIDE 2.0";
        literal String^ Version = "2.0.0";
    };

    // Makes a literal a String^ (needed inside ?: where literals would decay to char*).
    inline String^ Str(String^ s) { return s; }

    // ---------------------------------------------------------------- JSON
    // Load an object of DataContract type T from a JSON file.
    template<typename T>
    T^ JsonLoad(String^ path)
    {
        DataContractJsonSerializer^ ser = gcnew DataContractJsonSerializer(T::typeid);
        T^ result = nullptr;
        FileStream^ fs = File::OpenRead(path);
        try
        {
            result = safe_cast<T^>(ser->ReadObject(fs));
        }
        finally
        {
            fs->Close();
        }
        return result;
    }

    // Save an object as indented JSON. Writes to a temp file first so a crash
    // never leaves a half-written project/settings file.
    template<typename T>
    void JsonSave(String^ path, T^ obj)
    {
        DataContractJsonSerializer^ ser = gcnew DataContractJsonSerializer(T::typeid);
        MemoryStream^ ms = gcnew MemoryStream();
        System::Xml::XmlDictionaryWriter^ w =
            JsonReaderWriterFactory::CreateJsonWriter(ms, Encoding::UTF8, false, true, "  ");
        ser->WriteObject(w, obj);
        w->Flush();
        w->Close();

        String^ full = Path::GetFullPath(path);
        String^ dir = Path::GetDirectoryName(full);
        if (!String::IsNullOrEmpty(dir)) Directory::CreateDirectory(dir);
        String^ tmp = String::Concat(full, ".tmp");
        File::WriteAllBytes(tmp, ms->ToArray());
        if (File::Exists(full))
        {
            try { File::Replace(tmp, full, nullptr); }
            catch (IOException^) { File::Copy(tmp, full, true); File::Delete(tmp); }   // e.g. network shares
        }
        else File::Move(tmp, full);
    }

    // Deep copy through JSON (used to edit settings/projects in dialogs).
    template<typename T>
    T^ JsonClone(T^ obj)
    {
        DataContractJsonSerializer^ ser = gcnew DataContractJsonSerializer(T::typeid);
        MemoryStream^ ms = gcnew MemoryStream();
        ser->WriteObject(ms, obj);
        ms->Position = 0;
        return safe_cast<T^>(ser->ReadObject(ms));
    }

    // ---------------------------------------------------------------- Paths
    public ref class PathUtil abstract sealed
    {
    public:
        static String^ Resolve(String^ baseDir, String^ p)
        {
            if (String::IsNullOrWhiteSpace(p)) return String::Empty;
            String^ x = Environment::ExpandEnvironmentVariables(p->Trim()->Trim(L'"'));
            if (Path::IsPathRooted(x)) return Path::GetFullPath(x);
            return Path::GetFullPath(Path::Combine(baseDir, x));
        }

        // Manual implementation (System::Uri mishandles '#' and '%' in folder names).
        static String^ MakeRelative(String^ baseDir, String^ fullPath)
        {
            String^ bFull = Path::GetFullPath(baseDir);
            String^ f = Path::GetFullPath(fullPath);
            if (!String::Equals(Path::GetPathRoot(bFull), Path::GetPathRoot(f), StringComparison::OrdinalIgnoreCase))
                return f;                                   // other drive: keep absolute
            String^ b = bFull->TrimEnd(L'\\');            // compare roots before trimming (C:\ project)
            array<String^>^ bp = b->Split(L'\\');
            array<String^>^ fp = f->Split(L'\\');
            int common = 0;
            while (common < bp->Length && common < fp->Length &&
                   String::Equals(bp[common], fp[common], StringComparison::OrdinalIgnoreCase)) common++;
            StringBuilder^ sb = gcnew StringBuilder();
            for (int i = common; i < bp->Length; i++)
                if (bp[i]->Length > 0) sb->Append("..\\");
            for (int i = common; i < fp->Length; i++)
            {
                sb->Append(fp[i]);
                if (i < fp->Length - 1) sb->Append(L'\\');
            }
            return sb->Length == 0 ? Str(".") : sb->ToString();
        }

        static bool SamePath(String^ a, String^ b)
        {
            if (a == nullptr || b == nullptr) return false;
            try
            {
                return String::Equals(Path::GetFullPath(a), Path::GetFullPath(b), StringComparison::OrdinalIgnoreCase);
            }
            catch (Exception^) { return false; }
        }

        // true if child is inside parent (strictly below it)
        static bool IsStrictlyUnder(String^ parent, String^ child)
        {
            String^ p = Path::GetFullPath(parent)->TrimEnd(L'\\');
            String^ c = Path::GetFullPath(child)->TrimEnd(L'\\');
            if (c->Length <= p->Length + 1) return false;
            return c->StartsWith(String::Concat(p, "\\"), StringComparison::OrdinalIgnoreCase);
        }

        // A trailing backslash would escape the closing quote ("C:\\inc\\" -> C:\\inc\"),
        // so it is doubled, which Windows command-line parsing turns back into one.
        static String^ Quote(String^ s)
        {
            if (s == nullptr) return "\"\"";
            if (s->EndsWith("\\")) s = String::Concat(s, "\\");
            return String::Concat("\"", s, "\"");
        }

        // Finds an executable either by full path or on the PATH. nullptr if not found.
        static String^ FindExecutable(String^ p)
        {
            if (String::IsNullOrWhiteSpace(p)) return nullptr;
            String^ x = Environment::ExpandEnvironmentVariables(p->Trim()->Trim(L'"'));
            try
            {
                if (Path::IsPathRooted(x) || x->IndexOf(L'\\') >= 0 || x->IndexOf(L'/') >= 0)
                    return File::Exists(x) ? Path::GetFullPath(x) : nullptr;
            }
            catch (ArgumentException^) { return nullptr; }

            String^ pathEnv = Environment::GetEnvironmentVariable("PATH");
            if (pathEnv == nullptr) return nullptr;
            array<String^>^ exts = Path::HasExtension(x)
                ? gcnew array<String^>{ "" }
                : gcnew array<String^>{ ".exe", ".com", ".cmd", ".bat" };
            for each (String^ dir in pathEnv->Split(L';'))
            {
                if (String::IsNullOrWhiteSpace(dir)) continue;
                for each (String^ e in exts)
                {
                    try
                    {
                        String^ c = Path::Combine(dir->Trim()->Trim(L'"'), String::Concat(x, e));
                        if (File::Exists(c)) return c;
                    }
                    catch (ArgumentException^) {}
                }
            }
            return nullptr;
        }

        static bool IsValidFileName(String^ n)
        {
            if (String::IsNullOrWhiteSpace(n)) return false;
            if (n->IndexOfAny(Path::GetInvalidFileNameChars()) >= 0) return false;
            if (String::Equals(n, ".") || String::Equals(n, "..")) return false;
            return true;
        }

        static List<String^>^ SplitList(String^ s)
        {
            List<String^>^ r = gcnew List<String^>();
            if (s == nullptr) return r;
            for each (String^ part in s->Split(gcnew array<wchar_t>{ L';', L'\n', L'\r' }, StringSplitOptions::RemoveEmptyEntries))
            {
                String^ t = part->Trim();
                if (t->Length > 0) r->Add(t);
            }
            return r;
        }

        static String^ JoinList(List<String^>^ l)
        {
            if (l == nullptr) return String::Empty;
            return String::Join(";", l->ToArray());
        }
    };

    // ---------------------------------------------------------------- Text files
    // Reads text while remembering encoding (UTF-8 / UTF-8 BOM / UTF-16 / ANSI)
    // and line-ending style, so saving writes the file back the same way.
    public ref class TextFile abstract sealed
    {
    public:
        static String^ Read(String^ path, Encoding^% enc, bool% crlf)
        {
            array<Byte>^ bytes = File::ReadAllBytes(path);
            String^ text;
            if (bytes->Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
            {
                enc = gcnew UTF8Encoding(true);
                text = enc->GetString(bytes, 3, bytes->Length - 3);
            }
            else if (bytes->Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
            {
                enc = gcnew UnicodeEncoding(false, true);
                text = enc->GetString(bytes, 2, bytes->Length - 2);
            }
            else if (bytes->Length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
            {
                enc = gcnew UnicodeEncoding(true, true);
                text = enc->GetString(bytes, 2, bytes->Length - 2);
            }
            else
            {
                try
                {
                    UTF8Encoding^ strict = gcnew UTF8Encoding(false, true);
                    text = strict->GetString(bytes);
                    enc = gcnew UTF8Encoding(false);
                }
                catch (ArgumentException^)       // invalid UTF-8 -> system ANSI code page
                {
                    enc = Encoding::Default;
                    text = enc->GetString(bytes);
                }
            }
            crlf = text->Contains("\r\n") || !text->Contains("\n");
            return text;
        }

        // textLf uses "\n" line ends (RichTextBox style)
        static void Write(String^ path, String^ textLf, Encoding^ enc, bool crlf)
        {
            String^ t = textLf->Replace("\r\n", "\n");
            if (crlf) t = t->Replace("\n", "\r\n");
            File::WriteAllText(path, t, enc == nullptr ? gcnew UTF8Encoding(false) : enc);
        }

        static String^ EncodingLabel(Encoding^ enc)
        {
            if (enc == nullptr) return "UTF-8";
            if (dynamic_cast<UTF8Encoding^>(enc) != nullptr)
                return enc->GetPreamble()->Length > 0 ? Str("UTF-8 BOM") : Str("UTF-8");
            if (dynamic_cast<UnicodeEncoding^>(enc) != nullptr) return "UTF-16";
            return enc->WebName->ToUpperInvariant();
        }
    };
}

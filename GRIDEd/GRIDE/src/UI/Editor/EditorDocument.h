// =============================================================================
//  EditorDocument.h - one open file: path, encoding, line endings, project,
//  and the CodeEditor that shows it.
// =============================================================================
#pragma once
#include "CodeEditor.h"
#include "../Controls/TabStrip.h"
#include "../../Model/GrideProject.h"
#include "../../Core/TextFile.h"

namespace Gride { namespace UI {

    using namespace Gride::Model;

    public ref class EditorDocument
    {
    public:
        EditorDocument()
        {
            Editor = gcnew CodeEditor();
            Editor->Dock = DockStyle::Fill;
            FileEncoding = gcnew UTF8Encoding(false);
            LineEnding = "CRLF";
            FilePath = nullptr;
            untitledName = String::Format("Untitled{0}", ++untitledCounter);
            Lang = Language::Other;
            Project = nullptr;
            Tab = nullptr;
            lastWrite = DateTime::MinValue;
        }

        property String^ FilePath;
        property Encoding^ FileEncoding;
        property String^ LineEnding;
        property CodeEditor^ Editor;
        property GrideProject^ Project;
        property Language Lang;
        property TabItem^ Tab;

        property bool IsUntitled { bool get() { return String::IsNullOrEmpty(FilePath); } }
        property bool IsDirty { bool get() { return Editor->Modified; } }
        property String^ Title
        {
            String^ get()
            {
                if (IsUntitled) return untitledName + DefaultExtension();
                return Path::GetFileName(FilePath);
            }
        }
        property String^ EncodingName { String^ get() { return TextFile::EncodingLabel(FileEncoding); } }

        String^ DefaultExtension()
        {
            switch (Lang)
            {
            case Language::Cpp11:    return ".cpp";
            case Language::Shalimar: return ".shl";
            case Language::Assembly: return ".asm";
            default:                 return ".c";
            }
        }

        void Load(String^ path)
        {
            Encoding^ enc = nullptr;
            String^ text = TextFile::Read(path, enc);
            FileEncoding = enc;
            if (text->Contains("\r\n") || !text->Contains("\n")) LineEnding = "CRLF";
            else LineEnding = "LF";
            FilePath = Path::GetFullPath(path);
            Editor->LoadText(text);
            lastWrite = File::GetLastWriteTime(FilePath);
        }

        void Reload()
        {
            if (IsUntitled || !File::Exists(FilePath)) return;
            int line = Editor->CurrentLine;
            Load(FilePath);
            Editor->GoToLine(Math::Min(line, Editor->LineCount));
        }

        void Save()
        {
            if (IsUntitled) throw gcnew InvalidOperationException("document has no file name");
            String^ text = Editor->GetText();
            if (LineEnding == "CRLF") text = text->Replace("\n", "\r\n");
            String^ dir = Path::GetDirectoryName(FilePath);
            if (!Directory::Exists(dir)) Directory::CreateDirectory(dir);
            TextFile::Write(FilePath, text, FileEncoding);
            Editor->MarkSaved();
            lastWrite = File::GetLastWriteTime(FilePath);
        }

        void SaveAs(String^ path)
        {
            FilePath = Path::GetFullPath(path);
            Save();
        }

        // true once per external modification (e.g. a converter rewrote the file)
        bool ChangedOnDisk()
        {
            if (IsUntitled || !File::Exists(FilePath)) return false;
            DateTime w = File::GetLastWriteTime(FilePath);
            if (w > lastWrite.AddSeconds(1))
            {
                lastWrite = w;
                return true;
            }
            return false;
        }

    private:
        String^ untitledName;
        DateTime lastWrite;
        static int untitledCounter = 0;
    };
}}

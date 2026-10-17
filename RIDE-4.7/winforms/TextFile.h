#pragma once

// Reading and writing a source file the way it was found: its encoding, its byte-order mark and
// its line ending are noticed on the way in and reproduced on the way out, and a write goes to a
// file beside it first, so a failed save leaves the original as it was. README.md says why.

namespace ridegui {

using namespace System;

ref class TextFile {
public:
    System::Text::Encoding^ encoding;   // with the preamble it had, or none
    String^ eol;                        // "\n" or "\r\n"

    // A file made here: UTF-8 without a mark, and the line ending every save used to write.
    TextFile() : encoding(gcnew System::Text::UTF8Encoding(false)), eol("\n") {}

    // The text, or nullptr with why set: a NUL means it is not text the box can hold unchanged.
    static String^ Read(String^ path, TextFile^% how, String^% why) {
        array<Byte>^ bytes = System::IO::File::ReadAllBytes(path);
        how = gcnew TextFile();
        int skip = 0;
        if (Starts(bytes, 0xEF, 0xBB, 0xBF)) {
            how->encoding = gcnew System::Text::UTF8Encoding(true);
            skip = 3;
        } else if (bytes->Length >= 4 && Starts(bytes, 0xFF, 0xFE) && bytes[2] == 0 && bytes[3] == 0) {
            how->encoding = gcnew System::Text::UTF32Encoding(false, true);
            skip = 4;
        } else if (Starts(bytes, 0xFF, 0xFE)) {
            how->encoding = gcnew System::Text::UnicodeEncoding(false, true);
            skip = 2;
        } else if (Starts(bytes, 0xFE, 0xFF)) {
            how->encoding = gcnew System::Text::UnicodeEncoding(true, true);
            skip = 2;
        } else {
            // No mark: UTF-8 when every byte of it is, and otherwise this machine's ANSI page.
            try {
                (gcnew System::Text::UTF8Encoding(false, true))->GetString(bytes);
            } catch (System::Text::DecoderFallbackException^) {
                how->encoding = System::Text::Encoding::Default;
            }
        }

        String^ text = how->encoding->GetString(bytes, skip, bytes->Length - skip);
        if (text->IndexOf(L'\0') >= 0) {
            why = System::IO::Path::GetFileName(path) + " has NUL bytes in it - not text this editor can keep";
            return nullptr;
        }
        int newline = text->IndexOf(L'\n');
        if (newline > 0 && text[newline - 1] == L'\r') how->eol = "\r\n";
        return text;
    }

    // Written as it was read. A character the file's own code page has no place for makes the
    // file UTF-8 instead, and note says so; why is set when nothing could be written.
    static bool Write(String^ path, String^ text, TextFile^ how, String^% note, String^% why) {
        String^ lines = text->Replace("\r\n", "\n");
        if (how->eol == "\r\n") lines = lines->Replace("\n", "\r\n");

        array<Byte>^ body = how->encoding->GetBytes(lines);
        if (how->encoding->CodePage != 65001 && how->encoding->CodePage != 1200 &&
            how->encoding->CodePage != 1201 && how->encoding->CodePage != 12000 &&
            !String::Equals(how->encoding->GetString(body), lines, StringComparison::Ordinal)) {
            note = "written as UTF-8 - " + how->encoding->WebName + " has no place for some of its characters";
            how->encoding = gcnew System::Text::UTF8Encoding(false);
            body = how->encoding->GetBytes(lines);
        }
        array<Byte>^ mark = how->encoding->GetPreamble();

        String^ aside = path + ".ride-save";
        try {
            System::IO::FileStream^ out = gcnew System::IO::FileStream(
                aside, System::IO::FileMode::Create, System::IO::FileAccess::Write);
            try {
                out->Write(mark, 0, mark->Length);
                out->Write(body, 0, body->Length);
            } finally {
                out->Close();
            }
            if (!System::IO::File::Exists(path)) {
                System::IO::File::Move(aside, path);
                return true;
            }
            try {
                System::IO::File::Replace(aside, path, nullptr);
            } catch (System::IO::IOException^) {
                // A file system with no replace (a share, FAT): the bytes are safe beside it already.
                System::IO::File::Copy(aside, path, true);
                System::IO::File::Delete(aside);
            }
            return true;
        } catch (Exception^ problem) {
            why = problem->Message;
            try { System::IO::File::Delete(aside); } catch (Exception^) { }
            return false;
        }
    }

private:
    static bool Starts(array<Byte>^ bytes, int a, int b) {
        return bytes->Length >= 2 && bytes[0] == a && bytes[1] == b;
    }
    static bool Starts(array<Byte>^ bytes, int a, int b, int c) {
        return bytes->Length >= 3 && bytes[0] == a && bytes[1] == b && bytes[2] == c;
    }
};

}

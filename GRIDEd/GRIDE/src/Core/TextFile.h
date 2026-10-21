// TextFile.h - reads text files while remembering their encoding, so that
// saving (or Replace All on a file that is not open) writes them back the
// same way: UTF-8, UTF-8 with BOM, UTF-16 LE/BE (BOM) or the ANSI code page.
#pragma once
#include "../pch.h"

namespace Gride { namespace Core {

    public ref class TextFile abstract sealed
    {
    public:
        static String^ Decode(array<Byte>^ bytes, Encoding^% enc)
        {
            if (bytes->Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
            {
                enc = gcnew UTF8Encoding(true);
                return enc->GetString(bytes, 3, bytes->Length - 3);
            }
            if (bytes->Length >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
            {
                enc = gcnew UnicodeEncoding(false, true);
                return enc->GetString(bytes, 2, bytes->Length - 2);
            }
            if (bytes->Length >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
            {
                enc = gcnew UnicodeEncoding(true, true);
                return enc->GetString(bytes, 2, bytes->Length - 2);
            }
            // valid UTF-8 without BOM, else the system ANSI code page
            try
            {
                UTF8Encoding^ strict = gcnew UTF8Encoding(false, true);
                String^ s = strict->GetString(bytes);
                enc = gcnew UTF8Encoding(false);
                return s;
            }
            catch (ArgumentException^)      // DecoderFallbackException derives from it
            {
                enc = Encoding::Default;
                return enc->GetString(bytes);
            }
        }

        static String^ Read(String^ path, Encoding^% enc)
        {
            return Decode(File::ReadAllBytes(path), enc);
        }

        // File::WriteAllText writes the preamble of enc, so a BOM is kept exactly
        // when the file had one (UTF8Encoding(false) has an empty preamble)
        static void Write(String^ path, String^ text, Encoding^ enc)
        {
            File::WriteAllText(path, text, enc == nullptr ? gcnew UTF8Encoding(false) : enc);
        }

        static String^ EncodingLabel(Encoding^ enc)
        {
            if (enc == nullptr) return "UTF-8";
            if (dynamic_cast<UTF8Encoding^>(enc) != nullptr)
            {
                if (enc->GetPreamble()->Length > 0) return "UTF-8 BOM";
                return "UTF-8";
            }
            if (dynamic_cast<UnicodeEncoding^>(enc) != nullptr)
            {
                if (enc->CodePage == 1201) return "UTF-16 BE";
                return "UTF-16 LE";
            }
            return enc->WebName->ToUpperInvariant();
        }
    };
}}

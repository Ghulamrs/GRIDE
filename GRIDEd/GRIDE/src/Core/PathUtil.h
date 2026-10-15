// =============================================================================
//  PathUtil.h - relative/absolute path helpers
//  Project files (.pro / .grws) always store paths relative to themselves with
//  forward slashes, e.g. "src/kalman.c", so a project can be moved or zipped.
// =============================================================================
#pragma once
#include "../pch.h"

namespace Gride { namespace Core {

    public ref class PathUtil abstract sealed
    {
    public:
        static String^ WithTrailingSlash(String^ dir)
        {
            if (String::IsNullOrEmpty(dir)) return dir;
            if (dir->EndsWith("\\") || dir->EndsWith("/")) return dir;
            return dir + "\\";
        }

        // absolute -> "src/file.c" (relative to baseDir). Different drive => absolute.
        static String^ MakeRelative(String^ baseDir, String^ fullPath)
        {
            if (String::IsNullOrEmpty(baseDir) || String::IsNullOrEmpty(fullPath)) return fullPath;
            try
            {
                Uri^ b = gcnew Uri(WithTrailingSlash(Path::GetFullPath(baseDir)));
                Uri^ t = gcnew Uri(Path::GetFullPath(fullPath));
                if (b->Scheme != t->Scheme) return fullPath;
                if (String::Compare(Path::GetPathRoot(b->LocalPath), Path::GetPathRoot(t->LocalPath), true) != 0)
                    return fullPath;
                String^ rel = Uri::UnescapeDataString(b->MakeRelativeUri(t)->ToString());
                return rel->Replace(L'\\', L'/');
            }
            catch (Exception^) { return fullPath; }
        }

        // "src/file.c" -> D:\...\src\file.c ; absolute paths pass through.
        static String^ MakeAbsolute(String^ baseDir, String^ relPath)
        {
            if (String::IsNullOrEmpty(relPath)) return relPath;
            String^ p = relPath->Replace(L'/', L'\\');
            try
            {
                if (Path::IsPathRooted(p)) return Path::GetFullPath(p);
                return Path::GetFullPath(Path::Combine(baseDir, p));
            }
            catch (Exception^) { return p; }
        }

        static bool SamePath(String^ a, String^ b)
        {
            if (a == nullptr || b == nullptr) return false;
            try { return String::Compare(Path::GetFullPath(a), Path::GetFullPath(b), true) == 0; }
            catch (Exception^) { return String::Compare(a, b, true) == 0; }
        }

        // case-insensitive extension check, ext given with the dot (".c")
        static bool HasExtension(String^ path, String^ ext)
        {
            return String::Compare(Path::GetExtension(path), ext, true) == 0;
        }

        static String^ Quote(String^ s)
        {
            if (String::IsNullOrEmpty(s)) return "\"\"";
            if (s->StartsWith("\"")) return s;
            return "\"" + s + "\"";
        }

        static void EnsureDirectory(String^ dir)
        {
            if (!String::IsNullOrEmpty(dir) && !Directory::Exists(dir)) Directory::CreateDirectory(dir);
        }

        // folder that holds GRIDE.exe
        static String^ AppDirectory()
        {
            return Path::GetDirectoryName(Application::ExecutablePath);
        }
    };
}}

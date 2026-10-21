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
        // Done by hand: System::Uri treats '#' and "%xx" in folder names as URI syntax.
        static String^ MakeRelative(String^ baseDir, String^ fullPath)
        {
            if (String::IsNullOrEmpty(baseDir) || String::IsNullOrEmpty(fullPath)) return fullPath;
            try
            {
                String^ b = Path::GetFullPath(baseDir);
                String^ f = Path::GetFullPath(fullPath);
                if (String::Compare(Path::GetPathRoot(b), Path::GetPathRoot(f), true) != 0) return fullPath;
                array<String^>^ bp = b->TrimEnd(L'\\')->Split(L'\\');
                array<String^>^ fp = f->TrimEnd(L'\\')->Split(L'\\');
                int common = 0;
                while (common < bp->Length && common < fp->Length &&
                       String::Compare(bp[common], fp[common], true) == 0) common++;
                StringBuilder^ sb = gcnew StringBuilder();
                for (int i = common; i < bp->Length; i++)
                    if (bp[i]->Length > 0) sb->Append("../");
                for (int i = common; i < fp->Length; i++)
                {
                    sb->Append(fp[i]);
                    if (i < fp->Length - 1) sb->Append(L'/');
                }
                if (sb->Length == 0) return ".";
                return sb->ToString();
            }
            catch (Exception^) { return fullPath; }
        }

        // true when child lies inside parent (never parent itself)
        static bool IsStrictlyUnder(String^ parent, String^ child)
        {
            if (String::IsNullOrEmpty(parent) || String::IsNullOrEmpty(child)) return false;
            try
            {
                String^ p = Path::GetFullPath(parent)->TrimEnd(L'\\');
                String^ c = Path::GetFullPath(child)->TrimEnd(L'\\');
                if (c->Length <= p->Length + 1) return false;
                return c->StartsWith(p + "\\", StringComparison::OrdinalIgnoreCase);
            }
            catch (Exception^) { return false; }
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

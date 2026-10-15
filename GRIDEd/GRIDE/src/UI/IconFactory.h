// =============================================================================
//  IconFactory.h - draws all 16x16 toolbar / tree icons in code
//  (no image resources to ship). Icons are cached per theme; call Clear()
//  after a theme change so line icons pick up the new text colour.
// =============================================================================
#pragma once
#include "Theme.h"

namespace Gride { namespace UI {

    using namespace System::Drawing::Drawing2D;

    public ref class IconFactory abstract sealed
    {
    public:
        static Image^ Get(String^ name)
        {
            if (cache == nullptr) cache = gcnew Dictionary<String^, Image^>();
            Image^ img;
            if (cache->TryGetValue(name, img)) return img;
            img = Draw(name);
            cache[name] = img;
            return img;
        }

        static void Clear() { cache = nullptr; }

        // image list for TreeViews; keys are the icon names
        static ImageList^ CreateImageList(array<String^>^ names)
        {
            ImageList^ il = gcnew ImageList();
            il->ColorDepth = ColorDepth::Depth32Bit;
            il->ImageSize = SD::Size(16, 16);
            for each (String^ n in names) il->Images->Add(n, Get(n));
            return il;
        }

        static Icon^ AppIcon()
        {
            Bitmap^ b = gcnew Bitmap(32, 32);
            Graphics^ g = Graphics::FromImage(b);
            g->SmoothingMode = SmoothingMode::AntiAlias;
            g->TextRenderingHint = System::Drawing::Text::TextRenderingHint::AntiAliasGridFit;
            SolidBrush^ bg = gcnew SolidBrush(Theme::Hex("#E0A23B"));
            g->FillPath(bg, Rounded(SD::RectangleF(1, 1, 30, 30), 7));
            SD::Font^ f = gcnew SD::Font("Segoe UI", 17.0f, FontStyle::Bold, GraphicsUnit::Pixel);
            StringFormat^ sf = gcnew StringFormat();
            sf->Alignment = StringAlignment::Center; sf->LineAlignment = StringAlignment::Center;
            g->DrawString("G", f, gcnew SolidBrush(Theme::Hex("#15171A")), SD::RectangleF(0, 1, 32, 32), sf);
            delete g;
            return Icon::FromHandle(b->GetHicon());
        }

    private:
        static Dictionary<String^, Image^>^ cache = nullptr;

        static GraphicsPath^ Rounded(SD::RectangleF r, float rad)
        {
            GraphicsPath^ p = gcnew GraphicsPath();
            float d = rad * 2;
            p->AddArc(r.X, r.Y, d, d, 180, 90);
            p->AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p->AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            p->AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p->CloseFigure();
            return p;
        }

        // page with folded corner and a coloured badge letter
        static void FileIcon(Graphics^ g, Color badge, String^ letters, Color ink)
        {
            Pen^ p = gcnew Pen(ink, 1.0f);
            array<SD::PointF>^ page = { SD::PointF(3.5f, 1.5f), SD::PointF(10.5f, 1.5f), SD::PointF(13.5f, 4.5f),
                                        SD::PointF(13.5f, 14.5f), SD::PointF(3.5f, 14.5f) };
            g->DrawPolygon(p, page);
            g->DrawLine(p, 10.5f, 1.5f, 10.5f, 4.5f);
            g->DrawLine(p, 10.5f, 4.5f, 13.5f, 4.5f);
            if (letters != nullptr)
            {
                g->FillRectangle(gcnew SolidBrush(badge), 0, 7, letters->Length > 1 ? 13 : 10, 8);
                SD::Font^ f = gcnew SD::Font("Segoe UI", 7.0f, FontStyle::Bold, GraphicsUnit::Pixel);
                g->DrawString(letters, f, Brushes::White, -0.5f, 6.5f);
            }
        }

        static Image^ Draw(String^ n)
        {
            Theme^ t = ThemeManager::Current;
            Color ink = t->Text;
            Color blue = Theme::Hex("#5B9BE6"), green = Theme::Hex("#4CB782"), red = Theme::Hex("#E06C5B");
            Color amber = Theme::Hex("#E0A23B"), purple = Theme::Hex("#A77BDB"), teal = Theme::Hex("#3FA9B5");
            Color gray = Theme::Hex("#8A9099");

            Bitmap^ b = gcnew Bitmap(16, 16);
            Graphics^ g = Graphics::FromImage(b);
            g->SmoothingMode = SmoothingMode::AntiAlias;
            g->TextRenderingHint = System::Drawing::Text::TextRenderingHint::AntiAliasGridFit;
            Pen^ p = gcnew Pen(ink, 1.4f);
            p->StartCap = LineCap::Round; p->EndCap = LineCap::Round; p->LineJoin = LineJoin::Round;

            if (n == "new")            { FileIcon(g, green, "+", ink); }
            else if (n == "open" || n == "folderopen")
            {
                Pen^ fp = gcnew Pen(amber, 1.4f);
                g->DrawPolygon(fp, gcnew array<SD::PointF>{ SD::PointF(1.5f, 3.5f), SD::PointF(6, 3.5f), SD::PointF(7.5f, 5.5f),
                    SD::PointF(13.5f, 5.5f), SD::PointF(13.5f, 7.5f) });
                g->DrawPolygon(fp, gcnew array<SD::PointF>{ SD::PointF(1.5f, 13.5f), SD::PointF(3.5f, 7.5f), SD::PointF(15, 7.5f), SD::PointF(13, 13.5f) });
            }
            else if (n == "folder")
            {
                g->FillPolygon(gcnew SolidBrush(amber), gcnew array<SD::PointF>{ SD::PointF(1, 3), SD::PointF(6, 3), SD::PointF(7.5f, 5),
                    SD::PointF(15, 5), SD::PointF(15, 13.5f), SD::PointF(1, 13.5f) });
            }
            else if (n == "save" || n == "saveall")
            {
                Pen^ sp = gcnew Pen(blue, 1.4f);
                int o = n == "saveall" ? 2 : 0;
                if (o > 0) g->DrawRectangle(sp, 1.5f, 1.5f, 10.0f, 10.0f);
                g->DrawRectangle(sp, 2.5f + o, 2.5f + o, 11.0f - o, 11.0f - o);
                g->FillRectangle(gcnew SolidBrush(blue), 5.0f + o, 2.5f + o, 5.0f - o / 2, 4.0f);
            }
            else if (n == "undo" || n == "redo")
            {
                bool redo = n == "redo";
                GraphicsPath^ path = gcnew GraphicsPath();
                if (!redo) path->AddArc(3.0f, 4.0f, 10.0f, 9.0f, 180, 230); else path->AddArc(3.0f, 4.0f, 10.0f, 9.0f, 0, -230);
                g->DrawPath(p, path);
                float x = redo ? 13.0f : 3.0f;
                g->DrawLine(p, x, 8.5f, x, 4.5f);
                g->DrawLine(p, x, 8.5f, redo ? x - 3.5f : x + 3.5f, 8.5f);
            }
            else if (n == "cut")       { g->DrawEllipse(p, 2, 10, 4, 4); g->DrawEllipse(p, 10, 10, 4, 4); g->DrawLine(p, 5, 10, 11, 2); g->DrawLine(p, 11, 10, 5, 2); }
            else if (n == "copy")      { g->DrawRectangle(p, 2.5f, 2.5f, 8.0f, 9.0f); g->DrawRectangle(p, 5.5f, 5.5f, 8.0f, 9.0f); }
            else if (n == "paste")     { g->DrawRectangle(p, 2.5f, 3.5f, 10.0f, 11.0f); g->FillRectangle(gcnew SolidBrush(amber), 5, 2, 5, 3); }
            else if (n == "find")      { g->DrawEllipse(p, 2.5f, 2.5f, 7.5f, 7.5f); Pen^ hp = gcnew Pen(ink, 2.2f); hp->StartCap = LineCap::Round; hp->EndCap = LineCap::Round; g->DrawLine(hp, 9.5f, 9.5f, 13.5f, 13.5f); }
            else if (n == "build")
            {
                Pen^ bp = gcnew Pen(amber, 2.0f); bp->EndCap = LineCap::Round; bp->StartCap = LineCap::Round;
                g->DrawLine(bp, 3, 13, 9, 7);
                g->FillPolygon(gcnew SolidBrush(ink), gcnew array<SD::PointF>{ SD::PointF(8, 3), SD::PointF(13, 3), SD::PointF(14, 6), SD::PointF(10, 9), SD::PointF(7, 6) });
            }
            else if (n == "rebuild")   { g->DrawArc(p, 2.5f, 2.5f, 11.0f, 11.0f, 30, 300); g->FillPolygon(gcnew SolidBrush(amber), gcnew array<SD::PointF>{ SD::PointF(10, 1), SD::PointF(15, 3), SD::PointF(11, 6.5f) }); }
            else if (n == "clean")     { Pen^ cp = gcnew Pen(teal, 1.6f); g->DrawLine(cp, 11, 2, 6, 9); g->FillPolygon(gcnew SolidBrush(teal), gcnew array<SD::PointF>{ SD::PointF(3, 9), SD::PointF(9, 9), SD::PointF(10, 14), SD::PointF(2, 14) }); }
            else if (n == "run" || n == "debug")
            {
                g->FillPolygon(gcnew SolidBrush(n == "run" ? green : amber), gcnew array<SD::PointF>{ SD::PointF(4, 2), SD::PointF(14, 8), SD::PointF(4, 14) });
            }
            else if (n == "stop")      { g->FillRectangle(gcnew SolidBrush(red), 3, 3, 10, 10); }
            else if (n == "pause")     { g->FillRectangle(gcnew SolidBrush(blue), 3, 3, 3, 10); g->FillRectangle(gcnew SolidBrush(blue), 10, 3, 3, 10); }
            else if (n == "restart")   { Pen^ rp = gcnew Pen(green, 1.6f); g->DrawArc(rp, 2.5f, 2.5f, 11.0f, 11.0f, 300, 300); g->FillPolygon(gcnew SolidBrush(green), gcnew array<SD::PointF>{ SD::PointF(9, 0.5f), SD::PointF(14, 2), SD::PointF(10, 6) }); }
            else if (n == "stepinto" || n == "stepover" || n == "stepout")
            {
                Pen^ sp = gcnew Pen(blue, 1.6f); sp->EndCap = LineCap::ArrowAnchor;
                if (n == "stepover") { g->DrawArc(sp, 2.0f, 3.0f, 11.0f, 9.0f, 200, 160); }
                else if (n == "stepinto") { g->DrawLine(sp, 8, 1, 8, 10); }
                else { g->DrawLine(sp, 8, 10, 8, 1); }
                g->FillEllipse(gcnew SolidBrush(ink), 6, 12, 4, 4);
            }
            else if (n == "breakpoint") { g->FillEllipse(gcnew SolidBrush(red), 2, 2, 12, 12); }
            else if (n == "convert")
            {
                Pen^ cp = gcnew Pen(teal, 1.5f); cp->EndCap = LineCap::ArrowAnchor;
                g->DrawLine(cp, 2, 5, 13, 5);
                Pen^ cp2 = gcnew Pen(amber, 1.5f); cp2->EndCap = LineCap::ArrowAnchor;
                g->DrawLine(cp2, 14, 11, 3, 11);
            }
            else if (n == "comment" || n == "uncomment")
            {
                SD::Font^ f = gcnew SD::Font("Consolas", 9.0f, FontStyle::Bold, GraphicsUnit::Pixel);
                g->DrawString("//", f, gcnew SolidBrush(n == "comment" ? green : gray), 1, 2);
                if (n == "uncomment") g->DrawLine(gcnew Pen(red, 1.4f), 2, 14, 14, 2);
            }
            else if (n == "indent" || n == "outdent")
            {
                for (int i = 0; i < 4; i++) g->DrawLine(p, 7.0f, 2.5f + i * 3.5f, 14.0f, 2.5f + i * 3.5f);
                g->FillPolygon(gcnew SolidBrush(blue), n == "indent"
                    ? gcnew array<SD::PointF>{ SD::PointF(1, 5), SD::PointF(5, 8), SD::PointF(1, 11) }
                    : gcnew array<SD::PointF>{ SD::PointF(5, 5), SD::PointF(1, 8), SD::PointF(5, 11) });
            }
            else if (n == "bookmark" || n == "nextbm" || n == "prevbm")
            {
                g->FillPolygon(gcnew SolidBrush(teal), gcnew array<SD::PointF>{ SD::PointF(4, 2), SD::PointF(12, 2), SD::PointF(12, 14), SD::PointF(8, 10.5f), SD::PointF(4, 14) });
            }
            else if (n == "workspace")
            {
                g->FillRectangle(gcnew SolidBrush(purple), 1, 3, 6, 5); g->FillRectangle(gcnew SolidBrush(blue), 9, 3, 6, 5);
                g->FillRectangle(gcnew SolidBrush(amber), 5, 9, 6, 5);
            }
            else if (n == "project" || n == "projectccs" || n == "projectshl" || n == "projectcpp")
            {
                Color c = blue;
                if (n == "projectccs") c = purple; else if (n == "projectshl") c = green; else if (n == "projectcpp") c = Theme::Hex("#8C7BE0");
                g->FillPath(gcnew SolidBrush(c), Rounded(SD::RectangleF(1, 2, 14, 12), 2));
                g->FillRectangle(gcnew SolidBrush(Color::FromArgb(220, 255, 255, 255)), 4, 5, 8, 2);
                g->FillRectangle(gcnew SolidBrush(Color::FromArgb(220, 255, 255, 255)), 4, 9, 5, 2);
            }
            else if (n == "filec")     FileIcon(g, blue, "C", ink);
            else if (n == "filecpp")   FileIcon(g, Theme::Hex("#8C7BE0"), "C+", ink);
            else if (n == "fileshl")   FileIcon(g, green, "S", ink);
            else if (n == "fileh")     FileIcon(g, gray, "H", ink);
            else if (n == "fileasm")   FileIcon(g, amber, "A", ink);
            else if (n == "filecmd")   FileIcon(g, purple, "L", ink);
            else if (n == "file" || n == "fileother") FileIcon(g, gray, nullptr, ink);
            else if (n == "error")     { g->FillEllipse(gcnew SolidBrush(red), 1, 1, 14, 14); Pen^ w = gcnew Pen(Color::White, 1.8f); g->DrawLine(w, 5, 5, 11, 11); g->DrawLine(w, 11, 5, 5, 11); }
            else if (n == "warning")   { g->FillPolygon(gcnew SolidBrush(amber), gcnew array<SD::PointF>{ SD::PointF(8, 1), SD::PointF(15, 14), SD::PointF(1, 14) }); g->FillRectangle(Brushes::Black, 7, 5, 2, 5); g->FillRectangle(Brushes::Black, 7, 11, 2, 2); }
            else if (n == "info")      { g->FillEllipse(gcnew SolidBrush(blue), 1, 1, 14, 14); g->FillRectangle(Brushes::White, 7, 7, 2, 6); g->FillRectangle(Brushes::White, 7, 3, 2, 2); }
            else if (n == "settings")  { Pen^ sp = gcnew Pen(ink, 2.0f); g->DrawEllipse(sp, 4, 4, 8, 8); for (int i = 0; i < 8; i++) { double a = i * Math::PI / 4; g->DrawLine(sp, (float)(8 + 5 * Math::Cos(a)), (float)(8 + 5 * Math::Sin(a)), (float)(8 + 7 * Math::Cos(a)), (float)(8 + 7 * Math::Sin(a))); } }
            else if (n == "help")      { g->DrawEllipse(p, 1.5f, 1.5f, 13.0f, 13.0f); SD::Font^ f = gcnew SD::Font("Segoe UI", 10.0f, FontStyle::Bold, GraphicsUnit::Pixel); g->DrawString("?", f, gcnew SolidBrush(ink), 4, 1); }
            else if (n == "terminal")  { g->DrawRectangle(p, 1.5f, 2.5f, 13.0f, 11.0f); g->DrawLine(p, 4, 6, 6, 8); g->DrawLine(p, 6, 8, 4, 10); g->DrawLine(p, 8, 10, 11, 10); }
            else if (n == "refresh")   { g->DrawArc(p, 2.5f, 2.5f, 11.0f, 11.0f, 20, 300); g->FillPolygon(gcnew SolidBrush(ink), gcnew array<SD::PointF>{ SD::PointF(10, 1), SD::PointF(15, 4), SD::PointF(10, 7) }); }
            else if (n == "class" || n == "struct")
            {
                g->FillPolygon(gcnew SolidBrush(n == "class" ? amber : teal), gcnew array<SD::PointF>{ SD::PointF(8, 1), SD::PointF(15, 5), SD::PointF(15, 11), SD::PointF(8, 15), SD::PointF(1, 11), SD::PointF(1, 5) });
            }
            else if (n == "function")  { g->FillPolygon(gcnew SolidBrush(purple), gcnew array<SD::PointF>{ SD::PointF(8, 1), SD::PointF(15, 8), SD::PointF(8, 15), SD::PointF(1, 8) }); }
            else if (n == "variable")  { g->FillRectangle(gcnew SolidBrush(blue), 3, 5, 10, 6); }
            else if (n == "properties"){ for (int i = 0; i < 3; i++) { g->DrawLine(p, 6.0f, 3.5f + i * 4.5f, 14.0f, 3.5f + i * 4.5f); g->FillRectangle(gcnew SolidBrush(blue), 1.5f, 2.0f + i * 4.5f, 3.0f, 3.0f); } }
            else if (n == "output")    { g->DrawRectangle(p, 1.5f, 1.5f, 13.0f, 13.0f); for (int i = 0; i < 3; i++) g->DrawLine(p, 4.0f, 5.0f + i * 3.0f, 12.0f - i * 2.0f, 5.0f + i * 3.0f); }
            else if (n == "float")     { g->DrawRectangle(p, 1.5f, 4.5f, 9.0f, 9.0f); g->DrawRectangle(p, 5.5f, 1.5f, 9.0f, 9.0f); }
            else if (n == "close")     { g->DrawLine(p, 4, 4, 12, 12); g->DrawLine(p, 12, 4, 4, 12); }
            else                       { g->DrawRectangle(p, 2.5f, 2.5f, 11.0f, 11.0f); }

            delete g;
            return b;
        }
    };
}}

// =============================================================================
//  Commands.h - every menu item / toolbar button is a Command
//
//  Command          id, text, icon, shortcut, handler, enabled/checked tests
//  CommandRegistry  creates menu items and toolbar buttons bound to commands,
//                   refreshes their enabled/checked state, handles
//                   two-key chords such as "Ctrl+K,C" (Comment Selection)
//  KeyText          "Ctrl+Shift+B" <-> Keys
// =============================================================================
#pragma once
#include "IconFactory.h"

namespace Gride { namespace UI {

    // -------------------------------------------------------------------------
    public ref class KeyText abstract sealed
    {
    public:
        // parses one key combination such as "Ctrl+Shift+F5" or "Ctrl++"
        static Keys Parse(String^ text)
        {
            if (String::IsNullOrEmpty(text)) return Keys::None;
            Keys result = Keys::None;
            String^ s = text->Trim();
            // a trailing "+" key, e.g. "Ctrl++"
            if (s->EndsWith("++")) { result = result | Keys::Oemplus; s = s->Substring(0, s->Length - 2); }
            for each (String^ part in s->Split(L'+'))
            {
                String^ p = part->Trim();
                if (p->Length == 0) continue;
                String^ u = p->ToUpperInvariant();
                if (u == "CTRL" || u == "CONTROL") result = result | Keys::Control;
                else if (u == "SHIFT") result = result | Keys::Shift;
                else if (u == "ALT") result = result | Keys::Alt;
                else result = result | KeyFromName(p);
            }
            return result;
        }

        static Keys KeyFromName(String^ p)
        {
            String^ u = p->ToUpperInvariant();
            if (u == "DEL" || u == "DELETE") return Keys::Delete;
            if (u == "INS" || u == "INSERT") return Keys::Insert;
            if (u == "ENTER" || u == "RETURN") return Keys::Return;
            if (u == "BREAK" || u == "PAUSE") return Keys::Cancel;
            if (u == "ESC" || u == "ESCAPE") return Keys::Escape;
            if (u == "TAB") return Keys::Tab;
            if (u == "SPACE") return Keys::Space;
            if (u == "HOME") return Keys::Home;
            if (u == "END") return Keys::End;
            if (u == "PGUP") return Keys::PageUp;
            if (u == "PGDN") return Keys::PageDown;
            if (u == "`") return Keys::Oemtilde;
            if (u == "\\") return Keys::Oem5;
            if (u == "-") return Keys::OemMinus;
            if (u == "=") return Keys::Oemplus;
            if (u == ",") return Keys::Oemcomma;
            if (u == ".") return Keys::OemPeriod;
            if (u->Length == 1 && Char::IsLetterOrDigit(u[0]))
            {
                if (Char::IsDigit(u[0])) return (Keys)((int)Keys::D0 + (u[0] - L'0'));
                return (Keys)((int)Keys::A + (u[0] - L'A'));
            }
            if (u->StartsWith("F") && u->Length <= 3)
            {
                int n;
                if (Int32::TryParse(u->Substring(1), n) && n >= 1 && n <= 24) return (Keys)((int)Keys::F1 + n - 1);
            }
            return Keys::None;
        }

        // "Ctrl+K,C" -> first = Ctrl+K, second = C ; returns false when not a chord
        static bool ParseChord(String^ text, Keys% first, Keys% second)
        {
            first = Keys::None; second = Keys::None;
            if (String::IsNullOrEmpty(text) || !text->Contains(",") || text->EndsWith(",")) return false;
            int i = text->LastIndexOf(L',');
            first = Parse(text->Substring(0, i));
            second = Parse(text->Substring(i + 1));
            return first != Keys::None && second != Keys::None;
        }

        // Keys -> "Ctrl+Shift+B"
        static String^ Format(Keys k)
        {
            if (k == Keys::None) return String::Empty;
            StringBuilder^ sb = gcnew StringBuilder();
            if ((k & Keys::Control) == Keys::Control) sb->Append("Ctrl+");
            if ((k & Keys::Shift) == Keys::Shift) sb->Append("Shift+");
            if ((k & Keys::Alt) == Keys::Alt) sb->Append("Alt+");
            Keys code = k & Keys::KeyCode;
            if (code >= Keys::D0 && code <= Keys::D9) sb->Append((wchar_t)(L'0' + ((int)code - (int)Keys::D0)));
            else if (code == Keys::Oemplus) sb->Append("+");
            else if (code == Keys::OemMinus) sb->Append("-");
            else if (code == Keys::Oemtilde) sb->Append("`");
            else if (code == Keys::Oem5) sb->Append("\\");
            else if (code == Keys::Delete) sb->Append("Del");
            else if (code == Keys::Return) sb->Append("Enter");
            else if (code == Keys::Cancel) sb->Append("Break");
            else if (code != Keys::ControlKey && code != Keys::ShiftKey && code != Keys::Menu) sb->Append(code.ToString());
            return sb->ToString();
        }
    };

    // -------------------------------------------------------------------------
    //  the two key sets: Visual Studio's (the commands' own defaults) and RIDE's (its help, 10-keys.md:
    //  F8 debugs or carries on, F7 / F6 step over / into, F9 sets a breakpoint, Ctrl-Up / Ctrl-Down walk
    //  the stack, F4 builds, F5 runs, Ctrl-B compiles this file). A set goes under the user's own
    //  bindings; a key it takes from another command is given back as nothing.
    public ref class KeySets abstract sealed
    {
    public:
        literal String^ VisualStudio = "visualstudio";
        literal String^ Ride = "ride";

        static bool IsRide(String^ name) { return String::Compare(name, Ride, true) == 0; }

        static Dictionary<String^, String^>^ For(String^ name)
        {
            Dictionary<String^, String^>^ d = gcnew Dictionary<String^, String^>();
            if (!IsRide(name)) return d;
            d["Debug.Start"] = "F8";
            d["Debug.StepOver"] = "F7";
            d["Debug.StepInto"] = "F6";
            d["Debug.ToggleBreakpoint"] = "F9";
            d["Debug.UpStack"] = "Ctrl+Up";
            d["Debug.DownStack"] = "Ctrl+Down";
            d["Debug.StartNoDebug"] = "F5";
            d["Build.BuildWorkspace"] = "F4";
            d["Build.Compile"] = "Ctrl+B";
            d["Build.BuildProject"] = String::Empty;
            d["View.Properties"] = String::Empty;
            return d;
        }

        // what the Options page shows for each set
        static String^ Describe(String^ name)
        {
            return IsRide(name) ? "RIDE (F8 debugs or carries on, F7 / F6 step over / into, F9 breakpoint, Ctrl+Up / Ctrl+Down the stack, F4 builds, F5 runs, Ctrl+B compiles)"
                                : "Visual Studio (F5 debugs, F10 / F11 step over / into, Shift+F11 steps out, F9 breakpoint, F7 builds, Ctrl+F5 runs)";
        }
    };

    // -------------------------------------------------------------------------
    public ref class Command
    {
    public:
        Command(String^ id, String^ text, String^ shortcut, String^ icon, Action^ handler)
        {
            Id = id; Text = text; Icon = icon; Handler = handler;
            CanExecute = nullptr; IsChecked = nullptr; DisplayOnly = false; DefaultDisplayOnly = false;
            ShortcutText = shortcut == nullptr ? String::Empty : shortcut;
            DefaultShortcut = ShortcutText;
            Category = String::Empty;
        }

        property String^ Id;
        property String^ Text;          // menu text with & accelerator
        property String^ Icon;          // IconFactory name (may be null)
        property String^ Category;      // top menu name, for the shortcuts list
        property String^ ShortcutText;  // "Ctrl+S" or chord "Ctrl+K,C"
        property bool DisplayOnly;      // show the shortcut but let the focused control handle it
        property String^ DefaultShortcut;
        property bool DefaultDisplayOnly;

        // shortcut shown in the menu only; the focused control handles the keys
        Command^ SetDisplayOnly() { DisplayOnly = true; DefaultDisplayOnly = true; return this; }
        property Action^ Handler;
        property Func<bool>^ CanExecute;
        property Func<bool>^ IsChecked;

        property String^ PlainText { String^ get() { return Text->Replace("&", ""); } }

        property Keys ShortcutKeys
        {
            Keys get()
            {
                if (DisplayOnly || ShortcutText->Contains(",")) return Keys::None;
                Keys k = KeyText::Parse(ShortcutText);
                return ToolStripManager::IsValidShortcut(k) ? k : Keys::None;
            }
        }

        bool Enabled()
        {
            if (Handler == nullptr) return false;
            if (CanExecute == nullptr) return true;
            try { return CanExecute->Invoke(); } catch (Exception^) { return false; }
        }

        bool Checked()
        {
            if (IsChecked == nullptr) return false;
            try { return IsChecked->Invoke(); } catch (Exception^) { return false; }
        }

        void Execute()
        {
            if (!Enabled()) return;
            try { Handler->Invoke(); }
            catch (Exception^ ex)
            {
                MessageBox::Show(ex->Message, "GRIDE - " + PlainText, MessageBoxButtons::OK, MessageBoxIcon::Error);
            }
        }
    };

    // -------------------------------------------------------------------------
    public ref class CommandRegistry
    {
    public:
        CommandRegistry()
        {
            map = gcnew Dictionary<String^, Command^>(StringComparer::OrdinalIgnoreCase);
            ordered = gcnew List<Command^>();
            currentCategory = String::Empty;
        }

        property List<Command^>^ All { List<Command^>^ get() { return ordered; } }

        // commands added after this call are listed under that category
        void BeginCategory(String^ name) { currentCategory = name; }

        Command^ Add(String^ id, String^ text, String^ shortcut, String^ icon, Action^ handler)
        {
            Command^ c = gcnew Command(id, text, shortcut, icon, handler);
            c->Category = currentCategory;
            map[id] = c;
            ordered->Add(c);
            return c;
        }

        Command^ Get(String^ id)
        {
            Command^ c;
            if (map->TryGetValue(id, c)) return c;
            return nullptr;
        }

        void Execute(String^ id)
        {
            Command^ c = Get(id);
            if (c != nullptr) c->Execute();
        }

        // user key bindings from settings: id -> "Ctrl+Shift+X"
        void ApplyBindings(Dictionary<String^, String^>^ bindings)
        {
            for each (Command^ c in ordered)
            {
                c->ShortcutText = c->DefaultShortcut;
                c->DisplayOnly = c->DefaultDisplayOnly;
            }
            if (bindings == nullptr) return;
            for each (KeyValuePair<String^, String^> kv in bindings)
            {
                Command^ c = Get(kv.Key);
                if (c != nullptr) { c->ShortcutText = kv.Value; c->DisplayOnly = false; }
            }
        }

        ToolStripMenuItem^ MenuItem(String^ id)
        {
            Command^ c = Get(id);
            if (c == nullptr) throw gcnew ArgumentException("unknown command " + id);
            ToolStripMenuItem^ mi = gcnew ToolStripMenuItem(c->Text);
            mi->Tag = c;
            mi->Name = "mi_" + id;
            if (!String::IsNullOrEmpty(c->Icon)) mi->Image = IconFactory::Get(c->Icon);
            SetShortcut(mi, c);
            mi->Click += gcnew EventHandler(this, &CommandRegistry::OnItemClick);
            return mi;
        }

        ToolStripButton^ Button(String^ id, bool showText)
        {
            Command^ c = Get(id);
            if (c == nullptr) throw gcnew ArgumentException("unknown command " + id);
            ToolStripButton^ b = gcnew ToolStripButton();
            b->Tag = c;
            b->Name = "tb_" + id;
            b->Text = c->PlainText;
            b->ToolTipText = String::IsNullOrEmpty(c->ShortcutText) ? c->PlainText : c->PlainText + " (" + c->ShortcutText + ")";
            if (!String::IsNullOrEmpty(c->Icon)) b->Image = IconFactory::Get(c->Icon);
            b->DisplayStyle = showText ? ToolStripItemDisplayStyle::ImageAndText : ToolStripItemDisplayStyle::Image;
            b->Click += gcnew EventHandler(this, &CommandRegistry::OnItemClick);
            return b;
        }

        // refreshes Enabled/Checked of every command-bound item (recursively)
        void Refresh(ToolStripItemCollection^ items)
        {
            for each (ToolStripItem^ it in items)
            {
                Command^ c = dynamic_cast<Command^>(it->Tag);
                if (c != nullptr)
                {
                    bool en = c->Enabled();
                    if (it->Enabled != en) it->Enabled = en;
                    ToolStripMenuItem^ mi = dynamic_cast<ToolStripMenuItem^>(it);
                    if (mi != nullptr && c->IsChecked != nullptr) mi->Checked = c->Checked();
                    ToolStripButton^ tb = dynamic_cast<ToolStripButton^>(it);
                    if (tb != nullptr && c->IsChecked != nullptr) tb->Checked = c->Checked();
                }
            }
        }

        // re-applies shortcuts and icons after key bindings or theme change
        void UpdateItems(ToolStripItemCollection^ items)
        {
            for each (ToolStripItem^ it in items)
            {
                Command^ c = dynamic_cast<Command^>(it->Tag);
                if (c != nullptr && !String::IsNullOrEmpty(c->Icon)) it->Image = IconFactory::Get(c->Icon);
                ToolStripMenuItem^ mi = dynamic_cast<ToolStripMenuItem^>(it);
                if (mi != nullptr)
                {
                    if (c != nullptr) SetShortcut(mi, c);
                    UpdateItems(mi->DropDownItems);
                }
            }
        }

        // two-key chords: returns the command for (first, second) or null
        Command^ FindChord(Keys first, Keys second)
        {
            Keys secondPlain = second & Keys::KeyCode;
            for each (Command^ c in ordered)
            {
                Keys f, s;
                if (!KeyText::ParseChord(c->ShortcutText, f, s)) continue;
                if (f != first) continue;
                if (s == second || s == secondPlain || (s & Keys::KeyCode) == secondPlain) return c;
            }
            return nullptr;
        }

        bool IsChordStart(Keys k)
        {
            for each (Command^ c in ordered)
            {
                Keys f, s;
                if (KeyText::ParseChord(c->ShortcutText, f, s) && f == k) return true;
            }
            return false;
        }

    private:
        Dictionary<String^, Command^>^ map;
        List<Command^>^ ordered;
        String^ currentCategory;

        void SetShortcut(ToolStripMenuItem^ mi, Command^ c)
        {
            mi->ShortcutKeys = Keys::None;
            Keys k = c->ShortcutKeys;
            if (k != Keys::None)
            {
                try { mi->ShortcutKeys = k; } catch (Exception^) { }
            }
            mi->ShortcutKeyDisplayString = c->ShortcutText;
            mi->ShowShortcutKeys = true;
        }

        void OnItemClick(Object^ sender, EventArgs^ e)
        {
            ToolStripItem^ it = dynamic_cast<ToolStripItem^>(sender);
            if (it == nullptr) return;
            Command^ c = dynamic_cast<Command^>(it->Tag);
            if (c != nullptr) c->Execute();
        }
    };
}}

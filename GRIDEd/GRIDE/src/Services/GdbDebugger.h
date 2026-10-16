// =============================================================================
//  GdbDebugger.h - RIDE's debugger core (src/debugger.cpp) for GRIDEd
//
//    DebuggerKind     lldb, gdb, cdb, and the C6x kind: vm6747 --debug and sim6747
//                     --run --debug, which speak gdb's words and answer in gdb's shapes
//    DebugTranscript  what a debugger said, read: where it stopped, the variables, the
//                     frames, a value, a type, an address, the program's own output
//    ElfImage         whether a linked C6747 image carries a .debug_line with rows in
//                     the program's code (what sim6747 --run --debug stops on)
//    DebuggerChoice   which engine debugs a build, which backend, and the command
//                     line - from toolchain.json's debuggers and sessions
//    ProcessDebugger  the engine: a debugger on a pipe, asked one command at a time
//                     with a marker after it; the moves on a worker thread
//
//  The spellings of the commands come from the tool's session in toolchain.json;
//  the shapes read are the dialect's (gdb / lldb / cdb). The sentences are RIDE's.
// =============================================================================
#pragma once
#include "Debugger.h"
#include "DebugProcess.h"
#include "RideToolchain.h"
#include "../Settings/GrideSettings.h"
#include "../Model/GrideProject.h"

namespace Gride { namespace Services {

    using namespace System::Threading;
    using namespace Gride::Core;
    using namespace Gride::Model;
    using namespace Gride::Settings;

    // the engines; Shalimar is no debugger but the program itself, which stops itself (ShalimarSession)
    public enum class DebuggerKind { None, Lldb, Gdb, Cdb, C6x, Shalimar };

    // RIDE's Stop: what a move ended in
    public ref class DebugStop
    {
    public:
        DebugStop() { Stopped = false; Exited = false; Status = 0; File = String::Empty; Line = 0; Function = String::Empty; Said = String::Empty; }
        property bool Stopped;
        property bool Exited;
        property int Status;
        property String^ File;
        property int Line;
        property String^ Function;
        property String^ Said;
    };

    // ---- reading what a debugger said -----------------------------------------------------------
    public ref class DebugTranscript abstract sealed
    {
    public:
        static bool GdbLike(DebuggerKind k) { return k == DebuggerKind::Gdb || k == DebuggerKind::C6x; }

        static DebuggerKind DialectOf(String^ dialect)
        {
            if (String::Compare(dialect, "lldb", true) == 0) return DebuggerKind::Lldb;
            if (String::Compare(dialect, "cdb", true) == 0) return DebuggerKind::Cdb;
            if (String::Compare(dialect, "gdb", true) == 0) return DebuggerKind::Gdb;
            return DebuggerKind::None;
        }

        static array<String^>^ Lines(String^ text)
        {
            if (String::IsNullOrEmpty(text)) return gcnew array<String^>(0);
            List<String^>^ r = gcnew List<String^>();
            for each (String^ l in text->Split(L'\n')) r->Add(l->TrimEnd(L'\r'));
            if (r->Count > 0 && r[r->Count - 1]->Length == 0) r->RemoveAt(r->Count - 1);
            return r->ToArray();
        }

        // the prompts off the front of a line: "(gdb) ", "(lldb) ", cdb's "0:000> " (and several in a row)
        static String^ WithoutPrompt(String^ line)
        {
            String^ out = line;
            for (;;)
            {
                int at = 0;
                if (out->StartsWith("(gdb) ")) at = 6;
                else if (out->StartsWith("(lldb) ")) at = 7;
                else
                {
                    int i = 0;
                    while (i < out->Length && Char::IsDigit(out[i])) i++;
                    if (i == 0 || i >= out->Length || out[i] != L':') break;
                    int j = i + 1;
                    while (j < out->Length && IsHex(out[j])) j++;
                    if (j == i + 1 || j >= out->Length || out[j] != L'>') break;
                    at = j + 1;
                    if (at < out->Length && out[at] == L' ') at++;
                }
                out = out->Substring(at);
            }
            return out;
        }

        static DebuggerKind PromptOn(String^ line)
        {
            if (line->StartsWith("(gdb)")) return DebuggerKind::Gdb;
            if (line->StartsWith("(lldb)")) return DebuggerKind::Lldb;
            int i = 0;
            while (i < line->Length && Char::IsDigit(line[i])) i++;
            if (i == 0 || i >= line->Length || line[i] != L':') return DebuggerKind::None;
            int j = i + 1;
            while (j < line->Length && IsHex(line[j])) j++;
            if (j == i + 1 || j >= line->Length || line[j] != L'>') return DebuggerKind::None;
            return DebuggerKind::Cdb;
        }

        // ---- where it stopped (dbg_readStop) ----
        static DebugStop^ ReadStop(DebuggerKind kind, String^ said)
        {
            if (kind == DebuggerKind::Cdb) return ReadCdbStop(said);
            DebugStop^ stop = gcnew DebugStop();
            stop->Said = said;
            array<String^>^ all = Lines(said);
            for each (String^ raw in all)
            {
                String^ line = WithoutPrompt(raw);
                if (line->Contains("exited with status") || line->Contains("exited with code") || line->Contains("exited normally"))
                {
                    stop->Exited = true;
                    stop->Status = 0;
                    int at = line->IndexOf("status = ");
                    int base = 10;
                    if (at >= 0) at += 9;
                    else { at = line->IndexOf("code "); if (at >= 0) { at += 5; base = 8; } }
                    if (at >= 0) stop->Status = ParseInt(line->Substring(at), base);
                    continue;
                }
                if (line->StartsWith("Run till exit from")) continue;
                bool interesting = kind == DebuggerKind::Lldb ? line->Contains("frame #0:") : line->Contains(" at ");
                if (!interesting || stop->Stopped) continue;
                String^ file; int ln; String^ fn;
                if (!PlaceIn(line, file, ln, fn)) continue;
                stop->File = file; stop->Line = ln; stop->Function = fn; stop->Stopped = true;
            }
            if (!stop->Stopped && !stop->Exited)
                for each (String^ raw in all)
                {
                    // a step within the function answers the source echo alone: "19\t    printf(...)"
                    String^ line = WithoutPrompt(raw);
                    int tab = line->IndexOf(L'\t');
                    if (tab <= 0 || !Digits(line->Substring(0, tab))) continue;
                    stop->Line = ParseInt(line->Substring(0, tab), 10);
                    stop->Stopped = true;
                    break;
                }
            return stop;
        }

        static DebugStop^ ReadCdbStop(String^ said)
        {
            DebugStop^ stop = gcnew DebugStop();
            stop->Said = said;
            array<String^>^ all = Lines(said);
            for (int i = 0; i < all->Length; i++)
            {
                String^ line = WithoutPrompt(all[i])->Trim();
                int ended = line->IndexOf("Exit process");
                if (ended >= 0)
                {
                    stop->Exited = true; stop->Stopped = false;
                    int code = line->IndexOf("code ", ended);
                    if (code >= 0) stop->Status = ParseInt(line->Substring(code + 5), 16);
                    continue;
                }
                if (line->Contains("No runnable debuggees")) { stop->Exited = true; continue; }
                if (stop->Stopped || stop->Exited) continue;
                int open = line->LastIndexOf(L'(');
                if (open <= 0) continue;
                int close = line->IndexOf(L')', open);
                if (close < 0) continue;
                String^ number = line->Substring(open + 1, close - open - 1);
                if (!Digits(number)) continue;
                // ln says "path\file.c(9)" or "path\file.c(9)+0x2c"; the program's own "greet(4) is 10" is not a place
                String^ after = line->Substring(close + 1);
                if (after->Length > 0 && !after->StartsWith("+0x")) continue;
                if (!Path::HasExtension(line->Substring(0, open))) continue;
                stop->File = line->Substring(0, open);
                stop->Line = ParseInt(number, 10);
                stop->Stopped = true;
                for (int j = i + 1; j < all->Length && stop->Function->Length == 0; j++)
                {
                    String^ under = WithoutPrompt(all[j]);
                    int bang = under->IndexOf(L'!');
                    if (bang < 0) continue;
                    String^ rest = under->Substring(bang + 1);
                    int end = rest->IndexOfAny(gcnew array<wchar_t>{ L'+', L' ', L'\t', L'|', L'(' });
                    stop->Function = (end < 0 ? rest : rest->Substring(0, end))->Trim();
                }
            }
            return stop;
        }

        // "... in main () at file.c:11[:col]" -> file, line (never the column), function (RIDE's placeIn)
        static bool PlaceIn(String^ line, String^% file, int% ln, String^% function)
        {
            file = String::Empty; ln = 0; function = String::Empty;
            int at = line->LastIndexOf(" at ");
            if (at < 0) return false;
            String^ where = line->Substring(at + 4)->Trim();
            int colon = where->LastIndexOf(L':');
            if (colon < 0) return false;
            String^ tail = where->Substring(colon + 1);
            String^ head = where->Substring(0, colon);
            if (!Digits(tail)) return false;
            int second = head->LastIndexOf(L':');
            if (second >= 0 && Digits(head->Substring(second + 1)))
            {
                tail = head->Substring(second + 1);
                head = head->Substring(0, second);
            }
            file = head;
            ln = ParseInt(tail, 10);
            String^ front = line->Substring(0, at);
            int tick = front->LastIndexOf(L'`');
            if (tick >= 0) front = front->Substring(tick + 1);
            int bracket = front->IndexOf(L'(');
            if (bracket >= 0) front = front->Substring(0, bracket);
            int comma = front->LastIndexOf(L',');
            if (comma >= 0) front = front->Substring(comma + 1);
            int in = front->LastIndexOf(" in ");
            if (in >= 0) front = front->Substring(in + 4);
            function = front->Trim();
            return true;
        }

        // a step that did not go anywhere (lldb and cdb): the same place at a higher address, not a breakpoint
        static bool WentNowhere(DebuggerKind kind, DebugStop^ before, DebugStop^ after)
        {
            if (kind != DebuggerKind::Lldb && kind != DebuggerKind::Cdb) return false;
            if (before == nullptr || after == nullptr || !before->Stopped || !after->Stopped || after->Exited) return false;
            if (after->File->Length == 0 || after->Function->Length == 0 || after->Line == 0) return false;
            if (after->File != before->File || after->Line != before->Line) return false;
            if (after->Function != before->Function) return false;
            if (after->Said->Contains("stop reason = breakpoint")) return false;
            if (kind == DebuggerKind::Cdb)
            {
                if (after->Said->Contains("Breakpoint ") && after->Said->Contains(" hit")) return false;
                UInt64 was = CdbStopAddress(before->Said), now = CdbStopAddress(after->Said);
                return was != 0 && now > was;
            }
            UInt64 w = LldbFrameAddress(before->Said), n = LldbFrameAddress(after->Said);
            return w != 0 && n > w;
        }

        // ---- the variables (dbg_readVariables) ----
        static List<DebugVariable^>^ ReadVariables(DebuggerKind kind, String^ said, String^ marker)
        {
            List<DebugVariable^>^ found = gcnew List<DebugVariable^>();
            for each (String^ raw in Lines(said))
            {
                String^ line = WithoutPrompt(raw)->Trim();
                if (line->Length == 0 || line == marker) continue;
                String^ type = String::Empty;
                String^ address = String::Empty;
                if (kind == DebuggerKind::Lldb && line->StartsWith("0x"))
                {
                    int colon = line->IndexOf(": ");
                    if (colon < 0) continue;
                    address = line->Substring(0, colon);
                    line = line->Substring(colon + 2)->Trim();
                }
                if (kind == DebuggerKind::Lldb)
                {
                    if (line->Length == 0 || line[0] != L'(') continue;
                    int close = line->IndexOf(L')');
                    if (close < 0) continue;
                    type = line->Substring(1, close - 1);
                    line = line->Substring(close + 1)->Trim();
                }
                int equals = line->IndexOf(" = ");
                if (equals < 0) continue;
                String^ name = line->Substring(0, equals)->Trim();
                String^ value = line->Substring(equals + 3)->Trim();
                if (kind == DebuggerKind::Cdb && name->Contains(" "))
                {
                    // dv /t /V: "<address> <where> <type> <name> = value", the type free to hold spaces
                    String^ left = name;
                    int space = left->LastIndexOfAny(gcnew array<wchar_t>{ L' ', L'\t' });
                    name = left->Substring(space + 1);
                    String^ rest = left->Substring(0, space)->Trim();
                    int first = rest->IndexOfAny(gcnew array<wchar_t>{ L' ', L'\t' });
                    String^ at = first < 0 ? rest : rest->Substring(0, first);
                    rest = first < 0 ? String::Empty : rest->Substring(first)->Trim();
                    int second = rest->IndexOfAny(gcnew array<wchar_t>{ L' ', L'\t' });
                    type = second < 0 ? String::Empty : rest->Substring(second)->Trim();
                    address = at->Contains("@") ? String::Empty : AddressIn("0x" + at);
                }
                if (kind == DebuggerKind::Cdb && value->StartsWith("0n")) value = value->Substring(2);
                if (name->Length == 0 || name->Contains(" ")) continue;
                found->Add(gcnew DebugVariable(name, value, type, address));
            }
            return found;
        }

        // ---- the frames (dbg_readFrames): reading stops at main ----
        static List<DebugFrame^>^ ReadFrames(DebuggerKind kind, String^ said, String^ marker)
        {
            List<DebugFrame^>^ found = gcnew List<DebugFrame^>();
            for each (String^ raw in Lines(said))
            {
                String^ line = WithoutPrompt(raw)->Trim();
                if (line->Length == 0 || line == marker) continue;
                String^ file; int ln; String^ fn;
                if (kind == DebuggerKind::Cdb)
                {
                    if (!CdbFrameIn(line, file, ln, fn)) continue;
                }
                else
                {
                    String^ rest = AfterFrameNumber(kind, line);
                    if (rest->Length == 0 || !PlaceIn(rest, file, ln, fn)) continue;
                }
                found->Add(gcnew DebugFrame(fn, file, ln));
                if (fn == "main") break;
            }
            return found;
        }

        // ---- a value, a type, an address ----
        static String^ ReadValue(DebuggerKind kind, String^ said, String^ marker)
        {
            for each (String^ raw in Lines(said))
            {
                String^ line = WithoutPrompt(raw)->Trim();
                if (line->Length == 0 || line == marker) continue;
                if (kind == DebuggerKind::Cdb)
                {
                    if (line[0] == L'?') continue;
                    if (CdbRefused(line)) return String::Empty;
                    int space = line->LastIndexOf(L' ');
                    if (space < 0 || space + 1 >= line->Length) continue;
                    String^ value = line->Substring(space + 1);
                    if (value->StartsWith("0n")) value = value->Substring(2);
                    if (!HasDigit(value)) continue;
                    return value;
                }
                int dollar = line->IndexOf(L'$');
                if (dollar < 0) continue;
                int equals = line->IndexOf(" = ", dollar);
                if (equals < 0) continue;
                return line->Substring(equals + 3)->Trim();
            }
            return String::Empty;
        }

        static String^ ReadType(DebuggerKind kind, String^ said, String^ marker)
        {
            for each (String^ raw in Lines(said))
            {
                String^ line = WithoutPrompt(raw)->Trim();
                if (line->Length == 0 || line == marker) continue;
                if (kind == DebuggerKind::Cdb)
                {
                    if (line[0] == L'?') continue;
                    if (CdbRefused(line)) return String::Empty;
                    int space = line->LastIndexOf(L' ');
                    if (space < 0) continue;
                    String^ value = line->Substring(space + 1);
                    if (!HasDigit(value)) continue;
                    return line->Substring(0, space)->Trim();
                }
                if (GdbLike(kind))
                {
                    if (line->StartsWith("type = ")) return line->Substring(7)->Trim();
                    continue;
                }
                if (line[0] == L'(')
                {
                    int close = line->IndexOf(L')');
                    if (close > 0 && line->IndexOf(" = ", close) >= 0) return line->Substring(1, close - 1);
                }
            }
            return String::Empty;
        }

        // the first "0x..." in an answer, cdb's backtick taken out, or ""
        static String^ AddressIn(String^ said)
        {
            if (said == nullptr) return String::Empty;
            int at = said->IndexOf("0x");
            if (at < 0) return String::Empty;
            StringBuilder^ found = gcnew StringBuilder("0x");
            for (int i = at + 2; i < said->Length; i++)
            {
                wchar_t c = said[i];
                if (c == L'`') continue;
                if (!IsHex(c)) break;
                found->Append(c);
            }
            return found->Length > 2 ? found->ToString() : String::Empty;
        }

        // the complaint in an answer, or "" (RIDE's complaintIn)
        static String^ ComplaintIn(String^ answer)
        {
            array<String^>^ words = gcnew array<String^>{ "error", "No symbol", "not an lvalue", "Couldn't", "cannot be", "Syntax error", "Type conflict", "Bad register error" };
            for each (String^ raw in Lines(answer))
            {
                String^ line = WithoutPrompt(raw)->Trim();
                for each (String^ w in words)
                {
                    if (!line->Contains(w)) continue;
                    int at = line->IndexOf("error: ");
                    return at >= 0 ? line->Substring(at + 7) : line;
                }
            }
            return String::Empty;
        }

        static bool StoppedWithNoSource(String^ said)
        {
            return said->Contains("stop reason") || said->Contains("frame #0") || said->Contains("#0  0x");
        }

        // ---- the program's own output (dbg_programOutput): the debugger's lines dropped, unknown lines kept ----
        static List<String^>^ ProgramOutput(DebuggerKind kind, String^ said, String^ markerStart)
        {
            List<String^>^ out = gcnew List<String^>();
            for each (String^ raw in Lines(said))
            {
                String^ line = raw;
                DebuggerKind prompt = PromptOn(line);
                if (prompt == DebuggerKind::Lldb) continue;
                if (prompt != DebuggerKind::None) line = WithoutPrompt(line);
                if (line->Trim()->Length == 0) continue;
                if (!String::IsNullOrEmpty(markerStart) && line->Contains(markerStart)) continue;
                if (SourceEcho(line)) continue;
                if (kind == DebuggerKind::Lldb && LldbOwn(line)) continue;
                if (GdbLike(kind) && GdbOwn(line)) continue;
                if (kind == DebuggerKind::Cdb && CdbOwn(line)) continue;
                out->Add(line);
            }
            return out;
        }

        // ---- the console's escapes and echoes (cdb on a pseudo-console) ----
        static String^ WithoutEscapes(String^ text)
        {
            StringBuilder^ out = gcnew StringBuilder();
            for (int i = 0; i < text->Length; i++)
            {
                if (text[i] != L'\x1b') { out->Append(text[i]); continue; }
                if (i + 1 >= text->Length) break;
                wchar_t kind = text[i + 1];
                if (kind == L'[')
                {
                    int at = i + 2;
                    while (at < text->Length && !(text[at] >= L'@' && text[at] <= L'~')) at++;
                    i = at;
                }
                else if (kind == L']')
                {
                    int at = i + 2;
                    while (at < text->Length && text[at] != L'\x07' && !(text[at] == L'\x1b' && at + 1 < text->Length && text[at + 1] == L'\\')) at++;
                    if (at < text->Length && text[at] == L'\x1b') at++;
                    i = at;
                }
                else i++;
            }
            return out->ToString();
        }
        // the console echoes what was typed after the prompt: "0:000> p" is the command, not an answer
        static String^ WithoutEcho(String^ said, String^ asked, String^ markerCommand)
        {
            bool droppedAsked = false, droppedMarker = false;
            StringBuilder^ out = gcnew StringBuilder();
            for each (String^ l in Lines(said))
            {
                String^ bare = WithoutPrompt(l)->Trim();
                if (!droppedAsked && bare == asked->Trim()) { droppedAsked = true; continue; }
                if (!droppedMarker && bare == markerCommand->Trim()) { droppedMarker = true; continue; }
                out->Append(l); out->Append("\n");
            }
            return out->ToString();
        }

        // ---- the registers and memory ----
        // gdb: "pc  0x1184  4484" / "A4 0x1 1"; lldb: "rax = 0x..."; cdb: "rax=... rbx=..." several to a line
        static List<RegisterValue^>^ ReadRegisters(DebuggerKind kind, String^ said, String^ marker)
        {
            List<RegisterValue^>^ r = gcnew List<RegisterValue^>();
            for each (String^ raw in Lines(said))
            {
                String^ line = WithoutPrompt(raw)->Trim();
                if (line->Length == 0 || line == marker) continue;
                if (kind == DebuggerKind::Cdb || line->Contains("="))
                {
                    for each (String^ part in line->Split(gcnew array<wchar_t>{ L' ', L'\t' }, StringSplitOptions::RemoveEmptyEntries))
                    {
                        int eq = part->IndexOf(L'=');
                        if (eq <= 0 || eq == part->Length - 1) continue;
                        r->Add(gcnew RegisterValue(part->Substring(0, eq), part->Substring(eq + 1)));
                    }
                    // lldb: "rax = 0x0000000000000001"
                    if (r->Count == 0 || kind == DebuggerKind::Lldb)
                    {
                        int eq = line->IndexOf(" = ");
                        if (eq > 0 && kind == DebuggerKind::Lldb) r->Add(gcnew RegisterValue(line->Substring(0, eq)->Trim(), line->Substring(eq + 3)->Trim()));
                    }
                    continue;
                }
                array<String^>^ parts = line->Split(gcnew array<wchar_t>{ L' ', L'\t' }, StringSplitOptions::RemoveEmptyEntries);
                if (parts->Length >= 2 && parts[1]->StartsWith("0x")) r->Add(gcnew RegisterValue(parts[0], parts[1]));
            }
            return r;
        }

        // gdb: "0x1000 <sym>:\t0x01\t0x02 ..."; lldb: "0x1000: 0x01 0x02"; cdb: "00000000`00001000  01 02 03 ... ascii"
        static array<Byte>^ ReadMemoryBytes(DebuggerKind kind, String^ said, String^ marker, int wanted)
        {
            List<Byte>^ bytes = gcnew List<Byte>();
            for each (String^ raw in Lines(said))
            {
                String^ line = WithoutPrompt(raw)->Trim();
                if (line->Length == 0 || line == marker || ComplaintIn(line)->Length > 0) continue;
                if (kind == DebuggerKind::Cdb)
                {
                    // "00000000`00001000  01 02 03 04 ff 00 ab cd-ef 00 ..." - cdb joins the eighth and ninth bytes with a dash
                    array<String^>^ parts = line->Split(gcnew array<wchar_t>{ L' ', L'-' }, StringSplitOptions::RemoveEmptyEntries);
                    for (int i = 1; i < parts->Length && bytes->Count < wanted; i++)
                    {
                        if (parts[i]->Length != 2 || !IsHex(parts[i][0]) || !IsHex(parts[i][1])) break;
                        bytes->Add((Byte)Convert::ToInt32(parts[i], 16));
                    }
                    continue;
                }
                int colon = line->IndexOf(L':');
                if (colon < 0) continue;
                for each (String^ tok in line->Substring(colon + 1)->Split(gcnew array<wchar_t>{ L' ', L'\t' }, StringSplitOptions::RemoveEmptyEntries))
                {
                    if (!tok->StartsWith("0x") || tok->Length > 4 || bytes->Count >= wanted) continue;
                    bytes->Add((Byte)Convert::ToInt32(tok->Substring(2), 16));
                }
            }
            return bytes->ToArray();
        }

        // ---- RIDE's lines ----
        static String^ StopLine(String^ file, int line, String^ function)
        {
            return "stopped at " + Path::GetFileName(file == nullptr ? String::Empty : file) + ":" + line + (String::IsNullOrEmpty(function) ? String::Empty : " in " + function);
        }
        static String^ LookingAt(DebugFrame^ frame)
        {
            String^ where = Path::GetFileName(frame->File == nullptr ? String::Empty : frame->File) + ":" + frame->Line;
            if (String::IsNullOrEmpty(frame->Function)) return "the variables are those of " + where;
            return "the variables are " + frame->Function + "'s, at " + where;
        }
        static String^ FrameLine(DebugFrame^ frame, bool looking)
        {
            return (looking ? "> " : "  ") + frame->Function + "   " + Path::GetFileName(frame->File == nullptr ? String::Empty : frame->File) + ":" + frame->Line;
        }
        static String^ VariableLine(DebugVariable^ v)
        {
            String^ said = "  " + v->Name + " = " + v->Value;
            if (!String::IsNullOrEmpty(v->Type)) said += "   [" + v->Type + "]";
            return said;
        }
        static String^ WatchLine(DebugWatch^ w) { return "  " + w->Expression + " = " + (w->Ok ? w->Value : "[" + w->Value + "]"); }

        static bool Digits(String^ s)
        {
            if (String::IsNullOrEmpty(s)) return false;
            for each (wchar_t c in s) if (c < L'0' || c > L'9') return false;
            return true;
        }
        static int ParseInt(String^ s, int base)
        {
            int i = 0, value = 0;
            String^ t = s->TrimStart();
            while (i < t->Length)
            {
                int d = HexDigit(t[i]);
                if (d < 0 || d >= base) break;
                value = value * base + d;
                i++;
            }
            return value;
        }

    private:
        static bool IsHex(wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'); }
        static bool HasDigit(String^ s)
        {
            for each (wchar_t c in s) if (c >= L'0' && c <= L'9') return true;
            return false;
        }
        static int HexDigit(wchar_t c)
        {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return 10 + c - L'a';
            if (c >= L'A' && c <= L'F') return 10 + c - L'A';
            return -1;
        }
        static bool SourceEcho(String^ line)
        {
            String^ rest = line->Trim();
            if (rest->StartsWith("-> ")) rest = rest->Substring(3)->Trim();
            int i = 0;
            while (i < rest->Length && Char::IsDigit(rest[i])) i++;
            if (i == 0) return false;
            while (i < rest->Length && rest[i] == L' ') i++;
            return i < rest->Length && rest[i] == L'\t';
        }
        static bool LldbOwn(String^ line)
        {
            String^ bare = line->Trim();
            return bare->StartsWith("* thread #") || bare->StartsWith("frame #") || bare == "^" || line->StartsWith("Process ") || line->StartsWith("Target ");
        }
        static bool GdbOwn(String^ line)
        {
            if (line->StartsWith("Starting program:") || line->StartsWith("Continuing.") || line->StartsWith("Breakpoint ") || line->StartsWith("[") ||
                line->StartsWith("Using host ") || line->StartsWith("Reading symbols")) return true;
            return line->StartsWith("Missing ") && (line->Contains("debuginfo") || line->Contains("try:"));
        }
        static bool OurCdbCommand(String^ bare)
        {
            for each (String^ s in gcnew array<String^>{ "g", "p", "t", "gu", "k", "q", "ln", "dv", "l+t", "l+s", "n 10", ".lines -e", ".lastevent", ".echo", ".printf" })
                if (bare == s) return true;
            return bare->StartsWith("bp ") || bare->StartsWith("bu ") || bare->StartsWith("bc ") || bare->StartsWith(".printf ") || bare->StartsWith(".step_filter ");
        }
        static bool CdbOwn(String^ line)
        {
            String^ bare = line->Trim();
            if (OurCdbCommand(bare)) return true;
            if (bare->StartsWith("Breakpoint ") || bare->StartsWith("Last event:") || bare->StartsWith("debugger time:") || bare->StartsWith("ModLoad:")) return true;
            if (bare->Contains("!") && bare->EndsWith(":")) return true;
            if (bare->StartsWith("(") && bare->Contains("!")) return true;
            if (bare->Contains("`") && bare->Length > 0 && IsHex(bare[0])) return true;
            if (bare->EndsWith(")") && (bare->Contains(".c(") || bare->Contains(".cpp(") || bare->Contains(".h("))) return true;
            return false;
        }
        static bool CdbRefused(String^ line) { return line->Contains("Couldn't resolve") || line->Contains("<<gride") || line->Contains("rror"); }
        static UInt64 LldbFrameAddress(String^ said)
        {
            for each (String^ raw in Lines(said))
            {
                String^ line = WithoutPrompt(raw);
                int frame = line->IndexOf("frame #0:");
                if (frame < 0) continue;
                int hex = line->IndexOf("0x", frame);
                if (hex < 0) continue;
                return ParseHex64(line->Substring(hex + 2));
            }
            return 0;
        }
        static UInt64 CdbStopAddress(String^ said)
        {
            array<String^>^ all = Lines(said);
            UInt64 found = 0;
            for (int i = 0; i + 1 < all->Length; i++)
            {
                String^ head = WithoutPrompt(all[i])->Trim();
                if (!head->Contains("!") || !head->EndsWith(":") || head->Contains(" ")) continue;
                String^ word = WithoutPrompt(all[i + 1])->Trim();
                int sp = word->IndexOfAny(gcnew array<wchar_t>{ L' ', L'\t' });
                if (sp >= 0) word = word->Substring(0, sp);
                String^ hex = word->Replace("`", String::Empty);
                if (hex->Length == 0 || !IsHex(hex[0])) continue;
                found = ParseHex64(hex);
            }
            return found;
        }
        static UInt64 ParseHex64(String^ s)
        {
            UInt64 v = 0;
            for each (wchar_t c in s)
            {
                int d = HexDigit(c);
                if (d < 0) break;
                v = v * 16 + (UInt64)d;
            }
            return v;
        }
        static String^ AfterFrameNumber(DebuggerKind kind, String^ line)
        {
            if (kind == DebuggerKind::Lldb)
            {
                int frame = line->IndexOf("frame #");
                if (frame < 0) return String::Empty;
                int colon = line->IndexOf(L':', frame);
                return colon < 0 ? String::Empty : line->Substring(colon + 1)->Trim();
            }
            if (line->Length == 0 || line[0] != L'#') return String::Empty;
            int i = 1;
            while (i < line->Length && Char::IsDigit(line[i])) i++;
            return i == 1 ? String::Empty : line->Substring(i)->Trim();
        }
        static bool CdbFrameIn(String^ line, String^% file, int% ln, String^% function)
        {
            file = String::Empty; ln = 0; function = String::Empty;
            int open = line->LastIndexOf(L'['), close = line->LastIndexOf(L']');
            if (open < 0 || close < 0 || close < open) return false;
            String^ where = line->Substring(open + 1, close - open - 1);
            int sign = where->LastIndexOf(" @ ");
            if (sign < 0) return false;
            String^ tail = where->Substring(sign + 3)->Trim();
            if (!Digits(tail)) return false;
            file = where->Substring(0, sign)->Trim();
            ln = ParseInt(tail, 10);
            String^ call = line->Substring(0, open)->Trim();
            int space = call->LastIndexOfAny(gcnew array<wchar_t>{ L' ', L'\t' });
            if (space >= 0) call = call->Substring(space + 1);
            int bang = call->IndexOf(L'!');
            if (bang < 0) return false;
            call = call->Substring(bang + 1);
            int end = call->IndexOfAny(gcnew array<wchar_t>{ L'+', L' ', L'\t', L'(' });
            function = (end < 0 ? call : call->Substring(0, end))->Trim();
            return true;
        }
    };

    // ---- a linked C6747 image's line table (dbg_imageHasLines) ------------------------------------------
    public ref class ElfImage abstract sealed
    {
    public:
        // a 32-bit little-endian ELF whose .debug_line has a unit whose first opcode sets an address inside an executable section
        static bool HasLines(String^ out, String^% why)
        {
            why = String::Empty;
            array<Byte>^ d;
            try { if (!File::Exists(out)) { why = "the image is not there"; return false; } d = File::ReadAllBytes(out); }
            catch (Exception^) { why = "the image is not there"; return false; }
            if (d->Length < 52 || d[0] != 0x7f || d[1] != 'E' || d[2] != 'L' || d[3] != 'F' || d[4] != 1) { why = "the image is not a 32-bit ELF"; return false; }
            UInt32 shoff = U32(d, 32); int shentsize = U16(d, 46), shnum = U16(d, 48), shstrndx = U16(d, 50);
            if (shentsize < 40 || (Int64)shoff + (Int64)shnum * shentsize > d->Length || shstrndx >= shnum) { why = "the image's section table is not readable"; return false; }
            UInt32 strOff = U32(d, (int)shoff + shstrndx * shentsize + 16);
            List<KeyValuePair<UInt32, UInt32>>^ code = gcnew List<KeyValuePair<UInt32, UInt32>>();
            UInt32 lineOff = 0, lineSize = 0;
            for (int i = 0; i < shnum; i++)
            {
                int h = (int)shoff + i * shentsize;
                int name = (int)(strOff + U32(d, h));
                String^ n = name < d->Length ? CString(d, name) : String::Empty;
                UInt32 flags = U32(d, h + 8), addr = U32(d, h + 12), off = U32(d, h + 16), size = U32(d, h + 20);
                if ((flags & 4) != 0) code->Add(KeyValuePair<UInt32, UInt32>(addr, addr + size));
                if (n == ".debug_line" && size != 0) { lineOff = off; lineSize = size; }
            }
            if (lineSize == 0) { why = "the image carries no .debug_line"; return false; }
            if ((Int64)lineOff + lineSize > d->Length) { why = "the image's .debug_line runs past its end"; return false; }
            for (Int64 at = lineOff; at + 10 <= (Int64)lineOff + lineSize;)
            {
                UInt32 unitLen = U32(d, (int)at);
                if (unitLen == 0xffffffffu || unitLen < 6) break;
                Int64 unitEnd = at + 4 + unitLen;
                Int64 prog = at + 10 + U32(d, (int)at + 6);
                if (prog + 7 <= unitEnd && prog + 7 <= d->Length && d[(int)prog] == 0 && d[(int)prog + 2] == 2)
                {
                    UInt32 a = U32(d, (int)prog + 3);
                    for each (KeyValuePair<UInt32, UInt32> k in code) if (a >= k.Key && a < k.Value) return true;
                }
                if (unitEnd <= at) break;
                at = unitEnd;
            }
            why = "the image's .debug_line has no row in the program's code";
            return false;
        }
    private:
        static UInt32 U32(array<Byte>^ d, int at) { return at + 4 > d->Length ? 0u : (UInt32)d[at] | ((UInt32)d[at + 1] << 8) | ((UInt32)d[at + 2] << 16) | ((UInt32)d[at + 3] << 24); }
        static int U16(array<Byte>^ d, int at) { return at + 2 > d->Length ? 0 : d[at] | (d[at + 1] << 8); }
        static String^ CString(array<Byte>^ d, int at)
        {
            int end = at;
            while (end < d->Length && d[end] != 0) end++;
            return System::Text::Encoding::ASCII->GetString(d, at, end - at);
        }
    };

    // RIDE's sentences about a Shalimar program (src/shalimar/session.cpp)
    public ref class ShalimarWords abstract sealed
    {
    public:
        literal String^ SaysWhereOnly = "a Shalimar program says where it is, not what is in it";
        literal String^ SaysHowDeepOnly = "a Shalimar program reports how deep it is, not what called it";
        literal String^ ReleaseHasNoSession = "release links a runtime with no debugger in it";
        literal String^ DidNotArm = "the program did not arm - it has no debugger in it";
        literal String^ StopsItself = "a Shalimar program stops itself - it needs no debugger at all";
    };

    // ---- which engine debugs a build (dbg_for, dbg_c6xBackend, dbg_whyNot, dbg_planFor) ------------------------
    public ref class DebugLaunch
    {
    public:
        DebugLaunch()
        {
            Kind = DebuggerKind::None; Tool = nullptr; Session = nullptr; Command = String::Empty; WorkDir = String::Empty;
            Backend = String::Empty; BackendWhy = String::Empty; WhyNot = String::Empty; Program = String::Empty; Blind = gcnew List<String^>();
            Environment = gcnew List<String^>();
        }
        property DebuggerKind Kind;
        property ToolDefinition^ Tool;          // the debugger's definition (its session)
        property SessionDefinition^ Session;
        property String^ Command;               // the command line that starts it
        property String^ WorkDir;
        property List<String^>^ Environment;    // NAME=value the child starts with (the session's)
        property String^ Backend;               // vm6747 / sim6747 / cdb / gdb / lldb
        property String^ BackendWhy;            // why vm6747 when sim6747 was not chosen
        property String^ WhyNot;                // RIDE's sentence when nothing can debug the build
        property String^ Program;               // what the debugger is given
        property List<String^>^ Blind;          // the parts (folders) carrying no debug information
        property bool Possible { bool get() { return Kind != DebuggerKind::None && Session != nullptr && Command->Length > 0; } }
    };

    public ref class DebuggerChoice abstract sealed
    {
    public:
        // program: the build's program (the .out on tms6747); assembly: what the emulator runs (a .s or a .vm
        // directory); shalimar: a Shalimar program, which stops itself where no debugger reads it (RIDE's
        // dbg_planFor: the session only where the engine would be none - cdb and the emulator read a Debug build)
        static DebugLaunch^ For(GrideProject^ p, BuildConfiguration^ cfg, ToolchainSettings^ tc, GrideSettings^ s, String^ program, String^ assembly, bool shalimar, String^ shalimarRuntimeDir)
        {
            DebugLaunch^ l = gcnew DebugLaunch();
            Target t = cfg->TargetKind;
            String^ arch = RideToolchain::Arch(t);
            ToolchainDefinitions^ defs = ToolchainDefinitions::Current;
            TargetDefinition^ td = defs->TargetOf(t);
            RideTool tool = shalimar ? RideTool::Shalimar : RideToolchain::Resolve(RideToolchain::FromWord(p->Toolchain), p->Lang);
            ToolDefinition^ shc = shalimar ? defs->ToolFor(t, "shalimarCompiler") : nullptr;
            SessionDefinition^ own = shc == nullptr ? nullptr : shc->Session;
            l->WorkDir = p->ProjectDir;
            l->Program = program;
            // a part (a folder with its own compiler) that writes no line table: the debugger cannot stop in it
            for each (KeyValuePair<String^, String^> kv in p->FolderToolchains)
            {
                RideTool part = RideToolchain::Resolve(RideToolchain::FromWord(kv.Value), p->Lang);
                if (!RideToolchain::EmitsDebugInfo(part, t)) l->Blind->Add(kv.Key + " (" + RideToolchain::Name(part) + ")");
            }
            if (!cfg->IsDebug)
            {
                l->WhyNot = (shalimar ? ShalimarWords::ReleaseHasNoSession : "release is built without -g") + " - choose Build > Configuration > Debug, then F5";
                return l;
            }
            if (!RideToolchain::EmitsDebugInfo(tool, t) && own == nullptr)
            {
                l->WhyNot = RideToolchain::Name(tool) + " generates MASM for " + arch + ", which carries no line table";
                return l;
            }
            if (td != nullptr && td->Emulated)
            {
                String^ vm = tc->ResolveFor("emulator", tc->Emulator);
                String^ sim = tc->ResolveFor("simulator", tc->Simulator);
                ToolDefinition^ vmDef = defs->ToolFor(t, "emulator");
                ToolDefinition^ simDef = defs->ToolFor(t, "simulator");
                if (vmDef == nullptr || String::IsNullOrEmpty(vm) || !File::Exists(vm))
                {
                    String^ vmName = vmDef == nullptr ? "vm6747" : vmDef->Name;
                    if (own != nullptr) return OwnSession(l, shc, own, arch, "no " + vmName + " beside GRIDE", true, simDef, sim, RideToolchain::TiProgramOf(program));
                    l->WhyNot = vmName + " is not beside GRIDE and not on the PATH - the VM6747 emulator is the debugger for " + arch +
                                " (" + vmName + " --debug); name one with $" + (vmDef == nullptr ? "VM6747" : vmDef->Lookup->Env);
                    return l;
                }
                l->Kind = DebuggerKind::C6x;
                String^ out = RideToolchain::TiProgramOf(program);
                String^ why;
                String^ backend = C6xBackend(assembly, out, sim, simDef == nullptr ? "sim6747" : simDef->Name, vmDef->Name, s->Env->EffectiveC6747Debugger(), why);
                l->BackendWhy = why;
                l->Backend = backend;
                if (backend == (simDef == nullptr ? "sim6747" : simDef->Name) && simDef != nullptr)
                {
                    l->Tool = simDef; l->Session = simDef->Session; l->Program = out;
                    l->Command = RideToolchain::Quote(sim) + " " + simDef->OperationArgs("debug")->Replace("$(Program)", out)->Replace("$(ShalimarRuntimeDir)", String::Empty)->Trim();
                }
                else
                {
                    l->Tool = vmDef; l->Session = vmDef->Session; l->Program = assembly;
                    String^ rt = shalimar && !String::IsNullOrEmpty(shalimarRuntimeDir) ? RideToolchain::Quote(shalimarRuntimeDir) : String::Empty;
                    l->Command = RideToolchain::Quote(vm) + " " + vmDef->OperationArgs("debug")->Replace("$(Program)", assembly)->Replace("$(ShalimarRuntimeDir)", rt)->Trim();
                }
                if (l->Session == nullptr) l->WhyNot = l->Tool->Name + " has no session in toolchain.json - nothing knows how to drive it";
                return l;
            }
            if (td != nullptr && td->Host)
            {
                // cl's PDB, and c90's and cpp11's on x86_64-windows: CodeView, which cdb reads
                ToolDefinition^ cdb = defs->ToolFor(t, "debugger");
                String^ exe = tc->ResolveFor("debugger", tc->Debugger);
                if (cdb == nullptr || String::IsNullOrEmpty(exe) || !File::Exists(exe))
                {
                    String^ cdbName = cdb == nullptr ? "cdb" : cdb->Name;
                    // the debugger named for the role fallbackDebugger stands in where it is found (not RIDE's: cdb alone there)
                    ToolDefinition^ fb = defs->ToolFor(t, "fallbackDebugger");
                    String^ fbExe = fb == nullptr ? nullptr : tc->ResolveFor("fallbackDebugger", fb->Exe);
                    if (fb != nullptr && fb->Session != nullptr && fb->Operation("debug") != nullptr && !String::IsNullOrEmpty(fbExe) && File::Exists(fbExe))
                    {
                        l->Kind = DebugTranscript::DialectOf(fb->Session->Dialect);
                        if (l->Kind == DebuggerKind::None) l->Kind = DebuggerKind::Lldb;
                        l->Tool = fb; l->Session = fb->Session; l->Backend = fb->Name;
                        l->BackendWhy = cdbName + " is not installed - " + fb->Name + " stands in";
                        l->Command = RideToolchain::Quote(fbExe) + " " + fb->OperationArgs("debug")->Replace("$(Program)", program)->Replace("$(OutDir)", Path::GetDirectoryName(program))->Trim();
                        return l;
                    }
                    if (own != nullptr) return OwnSession(l, shc, own, arch, cdbName + " is not installed", false, nullptr, nullptr, program);
                    l->WhyNot = RideToolchain::Name(tool) + " writes a .pdb and cdb reads one, but cdb is not installed - add Debugging Tools for Windows";
                    return l;
                }
                l->Kind = DebuggerKind::Cdb;
                l->Tool = cdb; l->Session = cdb->Session; l->Backend = cdb->Name;
                l->Command = RideToolchain::Quote(exe) + " " + cdb->OperationArgs("debug")->Replace("$(Program)", program)->Replace("$(OutDir)", Path::GetDirectoryName(program))->Trim();
                if (l->Session == nullptr) l->WhyNot = cdb->Name + " has no session in toolchain.json - nothing knows how to drive it";
                return l;
            }
            // a cross target: its debugger lives on its own machine
            ToolDefinition^ theirs = td == nullptr ? nullptr : defs->ToolFor(t, "debugger");
            l->WhyNot = "the program is built for " + arch + " - debug it there" + (theirs == nullptr ? String::Empty : ", with " + theirs->Name) +
                        "; here " + arch + " only reaches assembly";
            return l;
        }

        // RIDE's session for a Shalimar program where no engine reads it: the program itself, started with
        // the session's environment - or, on tms6747, its .out under the simulator's session operation
        // (RIDE's sessionCommand); why says which engine was not there
        static DebugLaunch^ OwnSession(DebugLaunch^ l, ToolDefinition^ shc, SessionDefinition^ own, String^ arch, String^ why,
                                       bool underSimulator, ToolDefinition^ simDef, String^ simulator, String^ program)
        {
            l->Kind = DebuggerKind::Shalimar; l->Tool = shc; l->Session = own; l->Backend = "the program itself"; l->Program = program;
            l->Blind->Clear();
            for each (KeyValuePair<String^, String^> kv in own->Environment) l->Environment->Add(kv.Key + "=" + kv.Value);
            if (!underSimulator)
            {
                l->BackendWhy = why + " - the program's own session";
                l->Command = RideToolchain::Quote(program);
                return l;
            }
            String^ simName = simDef == nullptr ? "sim6747" : simDef->Name;
            String^ head = "a Shalimar program on " + arch + " stops itself under " + simName + " on its .out - ";
            if (simDef == nullptr || String::IsNullOrEmpty(simulator) || !File::Exists(simulator)) { l->WhyNot = head + "no " + simName + " beside GRIDE"; return l; }
            if (!File::Exists(program)) { l->WhyNot = head + "the build left no " + Path::GetFileName(program); return l; }
            if (simDef->Operation("session") == nullptr) { l->WhyNot = head + simName + " has no 'session' operation in toolchain.json"; return l; }
            l->BackendWhy = why + " - the program's own session under " + simName;
            l->Command = RideToolchain::Quote(simulator) + " " + simDef->OperationArgs("session")->Replace("$(Program)", program)->Trim();
            return l;
        }

        // RIDE's dbg_c6xBackend with GRIDEd's setting over it: auto (the simulator on the .out when it is this
        // build's and carries the program's lines), sim6747, vm6747; why says what decided it when the fallback was taken
        static String^ C6xBackend(String^ assembly, String^ out, String^ simulator, String^ simName, String^ vmName, String^ setting, String^% why)
        {
            why = String::Empty;
            bool haveSim = !String::IsNullOrEmpty(simulator) && File::Exists(simulator);
            if (String::Compare(setting, vmName, true) == 0 || String::Compare(setting, "vm6747", true) == 0) { why = "the setting asks for the emulator"; return vmName; }
            if (String::Compare(setting, simName, true) == 0 || String::Compare(setting, "sim6747", true) == 0)
            {
                if (haveSim && File::Exists(out)) return simName;
                why = !haveSim ? "the setting asks for " + simName + ", and none is beside GRIDE" : "the setting asks for " + simName + ", and the build left no " + Path::GetFileName(out);
                return vmName;
            }
            if (!haveSim) { why = "no " + simName + " beside GRIDE"; return vmName; }
            if (!File::Exists(out)) { why = "the build left no " + Path::GetFileName(out) + " (asm6x, lnk6x and RTS6x make one)"; return vmName; }
            try
            {
                if (!String::IsNullOrEmpty(assembly) && (File::Exists(assembly) || Directory::Exists(assembly)))
                {
                    DateTime builtOut = File::GetLastWriteTimeUtc(out);
                    DateTime builtAsm = Directory::Exists(assembly) ? NewestIn(assembly) : File::GetLastWriteTimeUtc(assembly);
                    if (builtOut < builtAsm) { why = Path::GetFileName(out) + " is older than the assembly: not this build's"; return vmName; }
                }
            }
            catch (Exception^) { }
            String^ lines;
            if (!ElfImage::HasLines(out, lines)) { why = lines; return vmName; }
            return simName;
        }

    private:
        static DateTime NewestIn(String^ dir)
        {
            DateTime newest = DateTime::MinValue;
            for each (String^ f in Directory::GetFiles(dir)) { DateTime t = File::GetLastWriteTimeUtc(f); if (t > newest) newest = t; }
            return newest;
        }
    };

    // ---- the engine --------------------------------------------------------------------------------------
    public ref class ProcessDebugger : public DebugEngine
    {
    public:
        literal int AskTimeoutMs = 30000;        // RIDE's readUntil
        literal int MoveTimeoutMs = 600000;      // a move waits for the program; Stop ends the wait

        ProcessDebugger(DebugLaunch^ launch)
        {
            this->launch = launch;
            session = launch->Session;
            kind = launch->Kind;
            dialect = session == nullptr ? DebuggerKind::None : DebugTranscript::DialectOf(session->Dialect);
            if (dialect == DebuggerKind::None) dialect = kind == DebuggerKind::C6x ? DebuggerKind::Gdb : kind;
            proc = nullptr; last = gcnew DebugStop(); moving = false;
            onConsole = session != nullptr && session->Console;
            gate = gcnew Object();
            Said = gcnew StringBuilder();
        }

        property String^ Name { virtual String^ get() override { return launch->Backend + " (" + (session == nullptr ? "no session" : session->Dialect) + ")"; } }
        property String^ Backend { virtual String^ get() override { return launch->Backend; } }
        property String^ BackendWhy { virtual String^ get() override { return launch->BackendWhy; } }
        property DebuggerKind Kind { DebuggerKind get() { return kind; } }
        property bool Running { bool get() { return proc != nullptr && proc->Running; } }
        property DebugStop^ LastStop { DebugStop^ get() { return last; } }
        // everything the debugger said in this session, for a report
        property StringBuilder^ Said;

        // RIDE's start: the command, the preamble, the marker and the wait for it; then every breakpoint, then run
        virtual bool Start(String^ program, Target target, String^ entryFile, int entryLine) override
        {
            if (!StartSession()) return false;
            BeginMove("run", false);
            return true;
        }

        // the session without the run (the tests drive the moves themselves): false, with the reason printed, when nothing started
        bool StartSession()
        {
            if (session == nullptr) { Print(launch->WhyNot->Length > 0 ? launch->WhyNot : "no session"); return false; }
            Stop();
            exited = false; exitStatus = 0; currentFrame = 0; last = gcnew DebugStop();
            proc = gcnew DebugProcess();
            String^ why;
            Print("[" + launch->Backend + "] " + launch->Command + (onConsole ? "   (on a pseudo-console)" : String::Empty));
            if (launch->BackendWhy->Length > 0) Print("(" + launch->BackendWhy + ")");
            for each (String^ b in launch->Blind) Print("(" + b + " carries no debug information - the debugger cannot stop in it)");
            bool started = onConsole ? proc->StartOnConsole(launch->Command, launch->WorkDir, launch->Environment, why)
                                     : proc->Start(launch->Command, launch->WorkDir, launch->Environment, why);
            if (!started) { Print("the debugger could not be started: " + why); proc = nullptr; return false; }
            for each (String^ line in session->Preamble) proc->Say(line);
            proc->Say(session->MarkerCommand);
            bool found;
            String^ said = proc->ReadUntil(session->Marker, AskTimeoutMs, found);
            Said->Append(said);
            if (!found)
            {
                Print("the debugger did not answer its first command - " + (said->Trim()->Length > 0 ? said->Trim() : "nothing came back"));
                proc->Stop(nullptr); proc = nullptr;
                return false;
            }
            SetState(DebugState::Running);
            SetEveryBreakpoint();
            return true;
        }

        virtual void Stop() override
        {
            DebugProcess^ p = proc;
            if (p == nullptr) { if (state != DebugState::Idle) { location = nullptr; SetState(DebugState::Idle); } return; }
            proc = nullptr;
            p->Stop(session == nullptr ? "quit" : session->Command("quit"));
            if (state != DebugState::Idle)
            {
                Print("Debugging stopped by user.");
                location = nullptr;
                SetState(DebugState::Terminated);
                SetState(DebugState::Idle);
            }
        }

        virtual void Continue() override { if (state == DebugState::Break) BeginMove("continue", false); }
        virtual void StepOver() override { if (state == DebugState::Break) BeginMove("stepOver", true); }
        virtual void StepInto() override { if (state == DebugState::Break) BeginMove("stepInto", true); }
        virtual void StepOut() override { if (state == DebugState::Break) BeginMove("stepOut", false); }
        virtual void BreakAll() override { Print("a program running under " + launch->Backend + " cannot be interrupted here - it stops at a breakpoint or at its end"); }

        virtual void RunToCursor(String^ file, int line) override
        {
            if (state != DebugState::Break || !session->Has("runToLine")) { Continue(); return; }
            String^ said = Ask(Fill(session->Command("runToLine"), file, line));
            if (!said->Contains("Breakpoint") && !said->Contains("breakpoint")) { Print("no breakpoint at " + Path::GetFileName(file) + ":" + line + " - running on"); }
            BeginMove("continue", false);
        }

        // ---- the moves, synchronous (the tests' way in; the overrides above run these on a worker) ----
        DebugStop^ RunNow() { return MoveNow("run", false); }
        DebugStop^ ContinueNow() { return MoveNow("continue", false); }
        DebugStop^ StepOverNow() { return MoveNow("stepOver", true); }
        DebugStop^ StepIntoNow() { return MoveNow("stepInto", true); }
        DebugStop^ StepOutNow() { return MoveNow("stepOut", false); }

        // ---- what the engine answers while stopped ----
        virtual List<DebugVariable^>^ Locals() override
        {
            if (state != DebugState::Break) return gcnew List<DebugVariable^>();
            if (dialect == DebuggerKind::Cdb) return DebugTranscript::ReadVariables(dialect, Ask(session->Command("locals")), session->Marker);
            if (dialect == DebuggerKind::Lldb) return DebugTranscript::ReadVariables(dialect, Ask(session->Command("locals")), session->Marker);
            // gdb lists no addresses, so each variable is asked where it is
            List<DebugVariable^>^ found = gcnew List<DebugVariable^>();
            if (session->Has("args")) found->AddRange(DebugTranscript::ReadVariables(dialect, Ask(session->Command("args")), session->Marker));
            found->AddRange(DebugTranscript::ReadVariables(dialect, Ask(session->Command("locals")), session->Marker));
            for each (DebugVariable^ v in found)
            {
                v->Address = AddressOf(v->Name);
                // sim6747 and vm6747 know each variable's type from the compilers' .dwtag: whatis says it
                if (kind == DebuggerKind::C6x && String::IsNullOrEmpty(v->Type)) v->Type = TypeOf(v->Name);
            }
            return found;
        }

        virtual String^ WhyNoLocals() override
        {
            if (kind == DebuggerKind::C6x) return "no locals here: a Debug build (-g) names each function's variables - this function has none, or was built without -g";
            return String::Empty;
        }

        virtual List<DebugFrame^>^ CallStack() override
        {
            if (state != DebugState::Break) return gcnew List<DebugFrame^>();
            List<DebugFrame^>^ frames = DebugTranscript::ReadFrames(dialect, Ask(session->Command("frames")), session->Marker);
            for each (DebugFrame^ f in frames) f->File = ResolveFile(f->File);
            return frames;
        }

        virtual bool SelectFrame(int index) override
        {
            if (state != DebugState::Break || !session->Has("frame")) return false;
            String^ said = Ask(session->Command("frame")->Replace("$(N)", index.ToString()));
            if (said->Contains("error:") || said->Contains("No frame at level") || said->Contains("Invalid frame")) return false;
            currentFrame = index;
            ReadWatches();
            return true;
        }

        virtual String^ Evaluate(String^ expression) override
        {
            bool ok;
            return Evaluate(expression, ok);
        }

        virtual String^ Evaluate(String^ expression, bool% ok) override
        {
            ok = false;
            if (state != DebugState::Break || String::IsNullOrWhiteSpace(expression)) return String::Empty;
            String^ answer = Ask(session->Command("evaluate")->Replace("$(Expr)", expression));
            String^ value = DebugTranscript::ReadValue(dialect, answer, session->Marker);
            if (value->Length > 0) { ok = true; return value; }
            String^ why = DebugTranscript::ComplaintIn(answer);
            return why->Length == 0 ? "no answer" : why;
        }

        virtual String^ AddressOf(String^ expression) override
        {
            if (state != DebugState::Break || !session->Has("address")) return String::Empty;
            String^ answer = Ask(session->Command("address")->Replace("$(Expr)", expression));
            String^ value = DebugTranscript::ReadValue(dialect, answer, session->Marker);
            return value->Length == 0 ? String::Empty : DebugTranscript::AddressIn(value);
        }

        virtual String^ TypeOf(String^ expression) override
        {
            if (state != DebugState::Break || !session->Has("type")) return String::Empty;
            return DebugTranscript::ReadType(dialect, Ask(session->Command("type")->Replace("$(Expr)", expression)), session->Marker);
        }

        virtual bool SetVariable(String^ name, String^ value, String^% said) override
        {
            said = String::Empty;
            if (state != DebugState::Break || String::IsNullOrWhiteSpace(name) || String::IsNullOrWhiteSpace(value)) return false;
            String^ answer = Ask(session->Command("set")->Replace("$(Name)", name)->Replace("$(Value)", value));
            String^ why = DebugTranscript::ComplaintIn(answer);
            if (why->Length > 0) { said = why; return false; }
            ReadWatches();
            return true;
        }

        virtual List<RegisterValue^>^ Registers() override
        {
            if (state != DebugState::Break || !session->Has("registers")) return gcnew List<RegisterValue^>();
            return DebugTranscript::ReadRegisters(dialect, Ask(session->Command("registers")), session->Marker);
        }

        virtual array<Byte>^ ReadMemory(UInt32 address, int length) override
        {
            if (state != DebugState::Break || !session->Has("memory")) return gcnew array<Byte>(0);
            String^ said = Ask(session->Command("memory")->Replace("$(Count)", length.ToString())->Replace("$(Address)", String::Format("0x{0:x}", address)));
            return DebugTranscript::ReadMemoryBytes(dialect, said, session->Marker, length);
        }

        virtual void BreakpointsChanged() override
        {
            if (state == DebugState::Break) SetEveryBreakpoint();
        }

        // one command, then the marker, then what came before it (RIDE's ask)
        String^ Ask(String^ command)
        {
            DebugProcess^ p = proc;
            if (p == nullptr || !p->Running || session == nullptr) return String::Empty;
            Monitor::Enter(gate);
            try
            {
                p->Say(command);
                p->Say(session->MarkerCommand);
                bool found;
                String^ said = p->ReadUntil(session->Marker, AskTimeoutMs, found);
                Said->Append(said);
                if (!found) { Print("the debugger stopped answering"); p->Stop(nullptr); }
                if (onConsole) said = DebugTranscript::WithoutEcho(DebugTranscript::WithoutEscapes(said), command, session->MarkerCommand);
                return said;
            }
            finally { Monitor::Exit(gate); }
        }

    private:
        DebugLaunch^ launch;
        SessionDefinition^ session;
        DebuggerKind kind;
        DebuggerKind dialect;
        DebugProcess^ proc;
        DebugStop^ last;
        Object^ gate;
        volatile bool moving;
        bool onConsole;

        String^ Fill(String^ templ, String^ file, int line)
        {
            return templ->Replace("$(File)", Path::GetFileName(file))->Replace("$(Line)", line.ToString());
        }

        // every stored breakpoint: clear, then break each (RIDE's SetEveryBreakpoint)
        void SetEveryBreakpoint()
        {
            if (session->Has("clearBreakpoints")) Ask(session->Command("clearBreakpoints"));
            for each (KeyValuePair<String^, SortedSet<int>^> kv in breakpoints)
                for each (int line in kv.Value)
                {
                    String^ said = Ask(Fill(session->Command("break"), kv.Key, line));
                    bool ok = dialect == DebuggerKind::Cdb ? !said->Contains("Couldn't resolve") && !said->Contains("Bp expression")
                                                           : said->Contains("Breakpoint") || said->Contains("breakpoint");
                    if (!ok) Print("no breakpoint at " + Path::GetFileName(kv.Key) + ":" + line + " - " + said->Trim());
                }
        }

        ref class Move
        {
        public:
            Move(ProcessDebugger^ e, String^ c, bool s) { engine = e; command = c; stepping = s; }
            void Run(Object^) { engine->MoveNow(command, stepping); }
        private:
            ProcessDebugger^ engine; String^ command; bool stepping;
        };

        void BeginMove(String^ command, bool stepping)
        {
            if (moving) return;
            moving = true;
            if (state != DebugState::Running) SetState(DebugState::Running);
            ThreadPool::QueueUserWorkItem(gcnew WaitCallback(gcnew Move(this, command, stepping), &Move::Run));
        }

        // RIDE's afterMoving / afterStepping: the command, cdb's .lastevent and ln, the stop read, a
        // step that went nowhere repeated; then the state, the program's output and the watches
        DebugStop^ MoveNow(String^ commandId, bool stepping)
        {
            moving = true;
            try
            {
                if (state != DebugState::Running) state = DebugState::Running;
                DebugStop^ origin = last;
                DebugStop^ stop = commandId == "stepInto" && dialect == DebuggerKind::Cdb ? CdbStepInto(origin)
                                : stepping ? AfterStepping(commandId) : AfterMoving(commandId);
                Announce(stop);
                return stop;
            }
            finally { moving = false; }
        }

        DebugStop^ AfterMoving(String^ commandId)
        {
            String^ command = session->Command(commandId);
            DebugProcess^ p = proc;
            if (p == nullptr || !p->Running) { DebugStop^ gone = gcnew DebugStop(); gone->Exited = true; return gone; }
            String^ said;
            Monitor::Enter(gate);
            try
            {
                p->Say(command);
                p->Say(session->MarkerCommand);
                bool found;
                said = p->ReadUntil(session->Marker, MoveTimeoutMs, found);
                Said->Append(said);
                if (onConsole) said = DebugTranscript::WithoutEcho(DebugTranscript::WithoutEscapes(said), command, session->MarkerCommand);
                if (!found && p->Running) { Print("the debugger stopped answering"); p->Stop(nullptr); }
            }
            finally { Monitor::Exit(gate); }
            if (dialect == DebuggerKind::Cdb && session->Has("afterMove"))
            {
                String^ ev = Ask(session->Command("afterMove"));
                said += "\n" + ev;
                if (!ev->Contains("Exit process") && session->Has("where")) said += "\n" + Ask(session->Command("where"));
            }
            for each (String^ line in DebugTranscript::ProgramOutput(dialect, said, MarkerStart())) PrintProgram(line);
            DebugStop^ stop = DebugTranscript::ReadStop(dialect, said);
            if (proc == nullptr || !proc->Running) { if (!stop->Exited) stop->Stopped = false; }
            if (stop->Stopped)
            {
                if (stop->File->Length == 0) stop->File = last->File;
                if (stop->Function->Length == 0) stop->Function = last->Function;
                last = stop;
            }
            else if (stop->Exited) last = gcnew DebugStop();
            return stop;
        }

        DebugStop^ AfterStepping(String^ commandId)
        {
            DebugStop^ before = last;
            DebugStop^ stop = AfterMoving(commandId);
            String^ said = stop->Said;
            for (int again = 0; again < 16 && DebugTranscript::WentNowhere(dialect, before, stop); again++)
            {
                before = stop;
                stop = AfterMoving(commandId);
                said += "\n" + stop->Said;
            }
            stop->Said = said;
            return stop;
        }

        // a step into a function with no source here (the CRT's printf: an incremental-link thunk,
        // then a header under a path of Microsoft's build machine) steps out again and finishes the
        // line, as Visual Studio steps over what it cannot show
        bool HasSource(DebugStop^ stop)
        {
            if (!stop->Stopped || stop->File->Length == 0) return false;
            try { return File::Exists(ResolveFile(stop->File)); } catch (Exception^) { return false; }
        }

        static bool SamePlace(DebugStop^ a, DebugStop^ b)
        {
            return a != nullptr && b != nullptr && a->Stopped && b->Stopped && a->Line == b->Line &&
                   String::Compare(Path::GetFileName(a->File), Path::GetFileName(b->File), true) == 0;
        }

        // the source line cdb echoes at a stop (">   16: Shape *make_rect(...) { return ...; }")
        static String^ EchoedLine(String^ said)
        {
            array<String^>^ all = DebugTranscript::Lines(said);
            for (int i = all->Length - 1; i >= 0; i--)
            {
                String^ bare = DebugTranscript::WithoutPrompt(all[i])->Trim();
                if (!bare->StartsWith(">")) continue;
                int colon = bare->IndexOf(L':');
                if (colon > 1) return bare->Substring(colon + 1);
            }
            return String::Empty;
        }

        // cdb's Step Into as Visual Studio does it: each call on the line is entered in turn; one
        // with no source here (an incremental-link thunk's target in the CRT, Shalimar's shm_line,
        // printf) is stepped back out of, and the step ends in the first function with source, or
        // on the next line when the line has none
        DebugStop^ CdbStepInto(DebugStop^ origin)
        {
            String^ said = String::Empty;
            DebugStop^ stop = gcnew DebugStop();
            for (int call = 0; call < 24; call++)
            {
                stop = AfterMoving("stepInto");
                String^ fresh = stop->Said;
                said += "\n" + fresh;
                // an incrementally linked program calls through a thunk (cdemo!ILT+11090(greet)): it has no
                // frame of its own, so stepping out of it would run the whole call - one more step reaches
                // the function itself
                for (int thunk = 0; thunk < 4 && !stop->Stopped && !stop->Exited && fresh->Contains("!ILT+"); thunk++)
                {
                    stop = AfterMoving("stepInto");
                    fresh = stop->Said;
                    said += "\n" + fresh;
                }
                if (stop->Exited || proc == nullptr || !proc->Running) break;
                if (HasSource(stop))
                {
                    if (SamePlace(stop, origin) && stop->Function == origin->Function) continue;
                    // at a function's entry its prologue is stepped past, unless the whole body is on
                    // the entry line (make_rect's "{ return new Rect(w, h); }"): stepping would run it
                    String^ echoed = EchoedLine(fresh);
                    int brace = echoed->IndexOf(L'{');
                    bool oneLine = brace >= 0 && echoed->Substring(brace + 1)->Trim()->Length > 0;
                    if (AtFunctionEntry(fresh) && !oneLine)
                    {
                        stop = AfterStepping("stepOver");
                        said += "\n" + stop->Said;
                    }
                    break;
                }
                // no source here: back out to a frame with source
                for (int up = 0; up < 8 && !stop->Exited && !HasSource(stop) && proc != nullptr && proc->Running; up++)
                {
                    stop = AfterMoving("stepOut");
                    said += "\n" + stop->Said;
                }
                if (stop->Exited || !HasSource(stop)) break;
                // back part way through the line the step began on: enter its next call
                if (!SamePlace(stop, origin)) break;
            }
            stop->Said = said;
            return stop;
        }

        static bool AtFunctionEntry(String^ said)
        {
            array<String^>^ all = DebugTranscript::Lines(said);
            for (int i = all->Length - 1; i >= 0; i--)
            {
                String^ bare = all[i]->Trim();
                if (!bare->Contains("!") || !bare->EndsWith(":")) continue;
                return !bare->Contains("+0x") && !bare->Contains(" ");
            }
            return false;
        }

        String^ MarkerStart()
        {
            String^ m = session->Marker;
            int dash = m->IndexOf(L'-');
            return dash > 0 ? m->Substring(0, dash) : m;
        }

        // the stop or the end, as RIDE's window says them
        void Announce(DebugStop^ stop)
        {
            if (stop->Stopped)
            {
                String^ file = ResolveFile(stop->File);
                ReadWatches();
                Print(DebugTranscript::StopLine(file, stop->Line, stop->Function));
                StopAt(file, stop->Line, stop->Said->Contains("Breakpoint") || stop->Said->Contains("breakpoint") ? "breakpoint" : "step", stop->Function);
                return;
            }
            // cdb answered and the program is still there, stopped where there is no line to show (a
            // Step Out from main into the runtime): the session goes on, F5 or Shift+F11 carry on from it
            if (!stop->Exited && dialect == DebuggerKind::Cdb && proc != nullptr && proc->Running)
            {
                Print("stopped where there is no source to show - F5 continues, Shift+F11 steps out");
                ReadWatches();
                location = nullptr;
                SetState(DebugState::Break);
                return;
            }
            exited = stop->Exited;
            exitStatus = stop->Status;
            if (stop->Exited) Print("the program ran to the end and returned " + stop->Status);
            else if (DebugTranscript::StoppedWithNoSource(stop->Said)) Print("stopped where there is no source to show");
            else Print("the debugger stopped answering");
            ReadWatches();
            DebugProcess^ p = proc;
            proc = nullptr;
            if (p != nullptr) p->Stop(session->Command("quit"));
            location = nullptr;
            SetState(DebugState::Terminated);
            SetState(DebugState::Idle);
        }
    };
}}

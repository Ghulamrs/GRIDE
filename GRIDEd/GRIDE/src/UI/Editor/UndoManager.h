// =============================================================================
//  UndoManager.h - delta based undo / redo for the code editor
//
//  RichTextBox's own undo also records colour changes made by the syntax
//  highlighter, so Ctrl+Z would "undo colouring"; its buffer is switched off
//  (EM_SETUNDOLIMIT 0) and GRIDE keeps its own history instead. Each step
//  stores only what changed (EditDelta), never a copy of the whole text.
//  CodeEditor groups typing into bursts and records one delta per burst.
// =============================================================================
#pragma once
#include "../../pch.h"

namespace Gride { namespace UI {

    // one undo step: at Start, Removed was replaced by Inserted
    public ref class EditDelta
    {
    public:
        int Start;
        String^ Removed;
        String^ Inserted;
        int SelStartBefore;     // selection before the edit (restored by Undo)
        int SelLengthBefore;
        int CaretAfter;         // caret after the edit (restored by Redo)
        int FirstLineBefore;    // first visible line before the edit
    };

    public ref class UndoManager
    {
    public:
        UndoManager() { undo = gcnew List<EditDelta^>(); redo = gcnew List<EditDelta^>(); MaxSteps = 1000; savePoint = 0; }

        property int MaxSteps;
        property bool CanUndo { bool get() { return undo->Count > 0; } }
        property bool CanRedo { bool get() { return redo->Count > 0; } }
        // true when the steps undone / redone lead back to the text last saved
        property bool AtSavePoint { bool get() { return savePoint == undo->Count; } }

        void Reset() { undo->Clear(); redo->Clear(); savePoint = 0; }

        void MarkSaved() { savePoint = undo->Count; }

        // a new edit after undo: the redo branch (and a save point in it) is gone
        void DiscardRedo()
        {
            if (savePoint > undo->Count) savePoint = -1;
            redo->Clear();
        }

        void Push(EditDelta^ d)
        {
            DiscardRedo();
            undo->Add(d);
            if (undo->Count > MaxSteps)
            {
                int drop = undo->Count - MaxSteps;
                undo->RemoveRange(0, drop);
                savePoint = savePoint >= drop ? savePoint - drop : -1;
            }
        }

        // the step to revert (moved to the redo list), or null
        EditDelta^ TakeUndo()
        {
            if (undo->Count == 0) return nullptr;
            EditDelta^ d = undo[undo->Count - 1];
            undo->RemoveAt(undo->Count - 1);
            redo->Add(d);
            return d;
        }

        // the step to apply again (moved back to the undo list), or null
        EditDelta^ TakeRedo()
        {
            if (redo->Count == 0) return nullptr;
            EditDelta^ d = redo[redo->Count - 1];
            redo->RemoveAt(redo->Count - 1);
            undo->Add(d);
            return d;
        }

    private:
        List<EditDelta^>^ undo;
        List<EditDelta^>^ redo;
        int savePoint;          // undo depth of the saved text, -1 = no longer reachable
    };
}}

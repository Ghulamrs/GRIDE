// =============================================================================
//  UndoManager.h - snapshot based undo / redo for the code editor
//
//  RichTextBox's own undo also records colour changes made by the syntax
//  highlighter, so Ctrl+Z would "undo colouring". GRIDE therefore keeps its
//  own history of text snapshots (taken after a short typing pause and before
//  every programmatic change).
// =============================================================================
#pragma once
#include "../../pch.h"

namespace Gride { namespace UI {

    public ref class UndoState
    {
    public:
        UndoState(String^ text, int caret) { Text = text; Caret = caret; }
        property String^ Text;
        property int Caret;
    };

    public ref class UndoManager
    {
    public:
        UndoManager() { states = gcnew List<UndoState^>(); index = -1; MaxStates = 300; }

        property int MaxStates;
        property bool CanUndo { bool get() { return index > 0; } }
        property bool CanRedo { bool get() { return index >= 0 && index < states->Count - 1; } }

        void Reset(String^ text, int caret)
        {
            states->Clear();
            states->Add(gcnew UndoState(text, caret));
            index = 0;
        }

        // records a new state (ignored when the text did not change)
        void Push(String^ text, int caret)
        {
            if (index >= 0 && String::CompareOrdinal(states[index]->Text, text) == 0) return;
            if (index < states->Count - 1) states->RemoveRange(index + 1, states->Count - index - 1);
            states->Add(gcnew UndoState(text, caret));
            // large files keep fewer snapshots
            int max = text->Length > 1000000 ? 20 : MaxStates;
            while (states->Count > max) states->RemoveAt(0);
            index = states->Count - 1;
        }

        // returns the state to restore, or null
        UndoState^ Undo(String^ currentText, int caret)
        {
            Push(currentText, caret);         // keep un-snapshotted typing
            if (index <= 0) return nullptr;
            index--;
            return states[index];
        }

        UndoState^ Redo()
        {
            if (!CanRedo) return nullptr;
            index++;
            return states[index];
        }

    private:
        List<UndoState^>^ states;
        int index;
    };
}}

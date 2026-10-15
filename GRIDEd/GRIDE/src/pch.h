// =============================================================================
//  GRIDE - 3-language, 4-target IDE
//  pch.h : precompiled header - common .NET namespaces for every source file
// =============================================================================
#pragma once

// Short aliases used to avoid clashes between WinForms *properties* and *types*
// (for example Control::Font vs System::Drawing::Font, Form::DialogResult vs
// the DialogResult enum). Inside Control-derived classes always write SD::Font,
// WF::BorderStyle, WF::DialogResult ... instead of the bare name.
namespace SD = System::Drawing;
namespace WF = System::Windows::Forms;

using namespace System;
using namespace System::Collections::Generic;
using namespace System::IO;
using namespace System::Text;
using namespace System::Text::RegularExpressions;
using namespace System::Globalization;
using namespace System::Windows::Forms;
using namespace System::Drawing;

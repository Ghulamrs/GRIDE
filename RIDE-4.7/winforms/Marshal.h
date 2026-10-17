#pragma once

// Strings across the seam: the window's own String^ and the bridge's UTF-8 char*.
// Managed only - the bridge's types stay behind bridge.h, which is all this includes of ours.

#include "bridge.h"

namespace ridegui {

using namespace System;

// A string's UTF-8, pinned for as long as this lives: `Utf8 source(path); ride_x(source.c());`.
ref class Utf8 {
public:
    Utf8(String^ text) {
        array<Byte>^ raw = System::Text::Encoding::UTF8->GetBytes(text == nullptr ? "" : text);
        bytes_ = gcnew array<Byte>(raw->Length + 1);
        Array::Copy(raw, bytes_, raw->Length);
        handle_ = Runtime::InteropServices::GCHandle::Alloc(
            bytes_, Runtime::InteropServices::GCHandleType::Pinned);
    }
    ~Utf8() { this->!Utf8(); }
    !Utf8() {
        if (handle_.IsAllocated) handle_.Free();
    }
    const char* c() { return static_cast<const char*>(handle_.AddrOfPinnedObject().ToPointer()); }

private:
    array<Byte>^ bytes_;
    Runtime::InteropServices::GCHandle handle_;
};

inline String^ FromUtf8(const char* text) {
    if (text == nullptr) return String::Empty;
    int length = 0;
    while (text[length] != '\0') ++length;
    if (length == 0) return String::Empty;

    array<Byte>^ bytes = gcnew array<Byte>(length);
    Runtime::InteropServices::Marshal::Copy(IntPtr(const_cast<char*>(text)), bytes, 0, length);
    return System::Text::Encoding::UTF8->GetString(bytes);
}

inline String^ TakeUtf8(char* text) {
    String^ out = FromUtf8(text);
    ride_free(text);
    return out;
}

// The four compilers and the target, pinned together: what every build call is handed.
ref class Toolchain {
public:
    Toolchain(String^ cc1, String^ cl, String^ shc, String^ cxx1, String^ arch)
        : cc1_(gcnew Utf8(cc1)), cl_(gcnew Utf8(cl)), shc_(gcnew Utf8(shc)),
          cxx1_(gcnew Utf8(cxx1)), arch_(gcnew Utf8(arch)) {}
    ~Toolchain() { delete cc1_; delete cl_; delete shc_; delete cxx1_; delete arch_; }

    const char* cc1() { return cc1_->c(); }
    const char* cl() { return cl_->c(); }
    const char* shc() { return shc_->c(); }
    const char* cxx1() { return cxx1_->c(); }
    const char* arch() { return arch_->c(); }

private:
    Utf8^ cc1_;
    Utf8^ cl_;
    Utf8^ shc_;
    Utf8^ cxx1_;
    Utf8^ arch_;
};

}

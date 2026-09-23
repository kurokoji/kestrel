#pragma once

#include <string>

// Pure address-bar input handling, kept free of HWNDs so it can be unit
// tested. Environment variables are expanded by the caller beforehand.
namespace AddressInput {

// Turns what was typed into a path to navigate to: trims spaces and
// surrounding quotes, accepts '/' separators, maps "D:" to "D:\", resolves
// relative input ("..", "sub") against `currentDir`, and collapses "." and
// ".." segments. "shell:..." and "::{CLSID}" input is returned unchanged.
std::wstring resolve(const std::wstring& input, const std::wstring& currentDir);

}  // namespace AddressInput

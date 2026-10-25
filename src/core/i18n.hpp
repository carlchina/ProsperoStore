// ProsperoStore - The words the store shows, in the player's language.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

// Every sentence the store writes for a person is its English text, handed to tr(); the table in
// core/i18n_strings.cpp has the same sentence in each language. A sentence the table does not
// have, or has no translation of yet, stays English. The catalog's own words (an app's name and
// description, its release notes) are never passed through here: they stay as their developer
// wrote them. tools/check-i18n.py keeps the table and the code in step.
namespace store::i18n
{

// The order of the columns in the table.
enum class Language : std::uint8_t
{
    english,
    spanish,
    french,
    portuguese,
    japanese,
    german,
    dutch,
    turkish,
    chinese,
};
constexpr int kLanguageCount = 9;

// What the console is set to (SCE_SYSTEM_SERVICE_PARAM_ID_LANG) as one of ours. Both Spains, both
// Portugals and both Frances are one language here; a language the store lacks is English.
Language from_console(int console_language);

const char *code(Language language);        // "en", "es", "fr", "pt", "ja", "de", "nl", "tr", "zh"
const char *native_name(Language language); // as its speakers write it: "Español"
bool parse_code(std::string_view text, Language *language);
// Whether the store can show it now. Japanese and Chinese each need their font
// (assets/fonts/noto-sans-jp-*.huifont, assets/fonts/noto-sans-sc-*.huifont): the program that
// starts the store says whether the files are there.
bool available(Language language);
void set_japanese_available(bool available);
void set_chinese_available(bool available);

void set_language(Language language);
Language language();

// The sentence in the current language; the English itself when there is no translation.
std::string_view tr(std::string_view english);
// The same as a string, for what takes ownership of its text.
std::string trs(std::string_view english);
// Capitals for small tracked labels ("UPDATE AVAILABLE"), accented letters and ß included. In
// Turkish the dotted i and the dotless ı keep their own capitals: İ and I.
std::string upper(std::string_view text);
// For an API that wants a C string. Every row and every English text is a literal, so the result
// is one too: it ends where the text does and lives as long as the program.
const char *trc(const char *english);
// The same, with {0}, {1}... replaced by the arguments.
std::string trf(std::string_view english, std::initializer_list<std::string_view> arguments);
// A sentence with a count in it as {0}: `one` when the count is one, else `other`. Each is its own
// row in the table, so a language can say them as it needs.
std::string trn(std::uint64_t count, std::string_view one, std::string_view other);

} // namespace store::i18n

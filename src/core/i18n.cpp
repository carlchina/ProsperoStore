// ProsperoStore - The words the store shows, in the player's language.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/i18n.hpp"

#include "core/i18n_strings.hpp"

#include <algorithm>
#include <atomic>
#include <vector>

namespace store::i18n
{
namespace
{
constexpr const char *kCodes[kLanguageCount] = {"en", "es", "fr", "pt", "ja",
                                                "de", "nl", "tr", "zh"};
constexpr const char *kNames[kLanguageCount] = {"English",    "Español", "Français",
                                                "Português",  "日本語",  "Deutsch",
                                                "Nederlands", "Türkçe",  "简体中文"};

// Read by the installer's thread as well as the screen's.
std::atomic<Language> current{Language::english};
std::atomic<bool> japanese_font{false};
std::atomic<bool> chinese_font{false};

// The rows by their English, for a binary search; made on first use.
const std::vector<const Row *> &index()
{
    static const std::vector<const Row *> rows = []
    {
        std::vector<const Row *> sorted;
        sorted.reserve(kStringCount);
        for (std::size_t i = 0; i < kStringCount; ++i)
            sorted.push_back(&kStrings[i]);
        std::sort(sorted.begin(), sorted.end(),
                  [](const Row *a, const Row *b) { return a->text[0] < b->text[0]; });
        return sorted;
    }();
    return rows;
}

const Row *find(std::string_view english)
{
    const auto &rows = index();
    const auto at =
        std::lower_bound(rows.begin(), rows.end(), english,
                         [](const Row *row, std::string_view key) { return row->text[0] < key; });
    return at != rows.end() && (*at)->text[0] == english ? *at : nullptr;
}
} // namespace

Language from_console(int console_language)
{
    // The console's numbers: 0 Japanese, 1 American English, 2 French, 3 Spanish, 4 German,
    // 6 Dutch, 7 Portuguese, 11 Simplified Chinese, 17 Brazilian Portuguese, 18 British English,
    // 19 Turkish, 20 Latin American Spanish, 22 Canadian French. Anything else has no translation
    // here: English, Traditional Chinese (10) among them.
    Language language = Language::english;
    switch (console_language)
    {
    case 0:
        language = Language::japanese;
        break;
    case 2:
    case 22:
        language = Language::french;
        break;
    case 3:
    case 20:
        language = Language::spanish;
        break;
    case 4:
        language = Language::german;
        break;
    case 6:
        language = Language::dutch;
        break;
    case 7:
    case 17:
        language = Language::portuguese;
        break;
    case 11:
        language = Language::chinese;
        break;
    case 19:
        language = Language::turkish;
        break;
    default:
        break;
    }
    return available(language) ? language : Language::english;
}

const char *code(Language language)
{
    return kCodes[static_cast<int>(language)];
}

const char *native_name(Language language)
{
    return kNames[static_cast<int>(language)];
}

bool parse_code(std::string_view text, Language *language)
{
    for (int i = 0; i < kLanguageCount; ++i)
        if (text == kCodes[i])
        {
            *language = static_cast<Language>(i);
            return true;
        }
    return false;
}

bool available(Language language)
{
    return (language != Language::japanese || japanese_font.load()) &&
           (language != Language::chinese || chinese_font.load());
}

void set_japanese_available(bool is_available)
{
    japanese_font.store(is_available);
}

void set_chinese_available(bool is_available)
{
    chinese_font.store(is_available);
}

void set_language(Language language)
{
    current.store(available(language) ? language : Language::english);
}

Language language()
{
    return current.load();
}

std::string_view tr(std::string_view english)
{
    const Language now = current.load();
    if (now == Language::english)
        return english;
    const Row *row = find(english);
    if (!row)
        return english;
    const std::string_view words = row->text[static_cast<int>(now)];
    return words.empty() ? english : words;
}

std::string trs(std::string_view english)
{
    return std::string(tr(english));
}

std::string upper(std::string_view text)
{
    const bool turkish = current.load() == Language::turkish;
    std::string out;
    out.reserve(text.size() + 4);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte == 'i' && turkish)
        {
            out += "\xC4\xB0"; // İ
        }
        else if (byte >= 'a' && byte <= 'z')
        {
            out += static_cast<char>(byte - 'a' + 'A');
        }
        else if (byte == 0xC4 && i + 1 < text.size() &&
                 (static_cast<unsigned char>(text[i + 1]) == 0x9F ||
                  static_cast<unsigned char>(text[i + 1]) == 0xB1))
        {
            // Turkish: ğ to Ğ, and the dotless ı to a plain I.
            out += text[i + 1] == '\xB1' ? "I" : "\xC4\x9E";
            ++i;
        }
        else if (byte == 0xC5 && i + 1 < text.size() &&
                 static_cast<unsigned char>(text[i + 1]) == 0x9F)
        {
            out += "\xC5\x9E"; // ş to Ş
            ++i;
        }
        else if ((byte == 0xC3 || byte == 0xC5) && i + 1 < text.size())
        {
            // Latin-1 (U+00E0..U+00FE, but not the division sign) and the ligature oe.
            auto next = static_cast<unsigned char>(text[i + 1]);
            ++i;
            if (byte == 0xC3 && next == 0x9F)
                out += "SS"; // the sharp s has no capital in the store's fonts
            else if (byte == 0xC3 && next >= 0xA0 && next <= 0xBE && next != 0xB7)
            {
                out += static_cast<char>(byte);
                out += static_cast<char>(next - 0x20);
            }
            else if (byte == 0xC5 && next == 0x93)
            {
                out += static_cast<char>(byte);
                out += static_cast<char>(0x92);
            }
            else
            {
                out += static_cast<char>(byte);
                out += static_cast<char>(next);
            }
        }
        else
        {
            out += static_cast<char>(byte);
        }
    }
    return out;
}

const char *trc(const char *english)
{
    return tr(english).data();
}

std::string trf(std::string_view english, std::initializer_list<std::string_view> arguments)
{
    const std::string_view sentence = tr(english);
    std::string out;
    out.reserve(sentence.size() + 16);
    for (std::size_t i = 0; i < sentence.size(); ++i)
    {
        if (sentence[i] == '{' && i + 2 < sentence.size() && sentence[i + 1] >= '0' &&
            sentence[i + 1] <= '9' && sentence[i + 2] == '}')
        {
            const auto number = static_cast<std::size_t>(sentence[i + 1] - '0');
            if (number < arguments.size())
                out += *(arguments.begin() + static_cast<std::ptrdiff_t>(number));
            i += 2;
            continue;
        }
        out += sentence[i];
    }
    return out;
}

std::string trn(std::uint64_t count, std::string_view one, std::string_view other)
{
    // French counts nought as one, as the others count it as many.
    const bool single = count == 1 || (current.load() == Language::french && count == 0);
    return trf(single ? one : other, {std::to_string(count)});
}

} // namespace store::i18n

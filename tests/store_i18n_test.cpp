// ProsperoStore - The words in each language: lookup, fallback, counts and the table's shape.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/i18n.hpp"
#include "core/i18n_strings.hpp"
#include <cassert>
#include <set>
#include <string>

namespace
{
// The {0}, {1}... of a sentence, in the order they stand.
std::string placeholders(std::string_view text)
{
    std::string found;
    for (std::size_t i = 0; i + 2 < text.size(); ++i)
        if (text[i] == '{' && text[i + 1] >= '0' && text[i + 1] <= '9' && text[i + 2] == '}')
            found += text[i + 1];
    std::set<char> unique(found.begin(), found.end());
    return std::string(unique.begin(), unique.end());
}
} // namespace

int main()
{
    using namespace store::i18n;
    set_japanese_available(true);
    set_chinese_available(true);

    // The console's languages: both Spains, both Portugals and both Frances are one each.
    assert(from_console(1) == Language::english && from_console(18) == Language::english);
    assert(from_console(3) == Language::spanish && from_console(20) == Language::spanish);
    assert(from_console(2) == Language::french && from_console(22) == Language::french);
    assert(from_console(7) == Language::portuguese && from_console(17) == Language::portuguese);
    assert(from_console(4) == Language::german && from_console(6) == Language::dutch);
    assert(from_console(0) == Language::japanese && from_console(19) == Language::turkish);
    assert(from_console(11) == Language::chinese);
    assert(from_console(5) == Language::english &&
           from_console(9) == Language::english);  // none of ours
    assert(from_console(10) == Language::english); // Traditional Chinese is not one of ours
    assert(from_console(-1) == Language::english);
    // Without the Japanese font the console's Japanese is English, and so is choosing it.
    set_japanese_available(false);
    assert(from_console(0) == Language::english && !available(Language::japanese));
    set_language(Language::japanese);
    assert(language() == Language::english);
    set_japanese_available(true);
    // Without the Chinese font the console's Simplified Chinese is English, likewise.
    set_chinese_available(false);
    assert(from_console(11) == Language::english && !available(Language::chinese));
    set_language(Language::chinese);
    assert(language() == Language::english);
    set_chinese_available(true);

    Language parsed{};
    assert(parse_code("es", &parsed) && parsed == Language::spanish);
    assert(parse_code("ja", &parsed) && parsed == Language::japanese);
    assert(parse_code("zh", &parsed) && parsed == Language::chinese);
    assert(!parse_code("xx", &parsed) && !parse_code("", &parsed));
    assert(std::string(code(Language::dutch)) == "nl");
    assert(std::string(code(Language::chinese)) == "zh");
    assert(parse_code("tr", &parsed) && parsed == Language::turkish);
    assert(std::string(native_name(Language::turkish)) == "Türkçe");
    assert(std::string(native_name(Language::chinese)) == "简体中文");

    // The same sentence, in each language; a sentence that is not in the table stays as it is.
    set_language(Language::english);
    assert(tr("Settings") == "Settings");
    set_language(Language::spanish);
    assert(tr("Settings") == "Ajustes");
    assert(tr("This sentence is not in the table") == "This sentence is not in the table");
    set_language(Language::japanese);
    assert(tr("Settings") == "設定");
    set_language(Language::german);
    assert(tr("Settings") == "Einstellungen");
    set_language(Language::turkish);
    assert(tr("Settings") == "Ayarlar");
    assert(trn(2, "{0} comment", "{0} comments") == "2 yorum");
    set_language(Language::chinese);
    assert(tr("Settings") == "设置");
    assert(trn(2, "{0} comment", "{0} comments") == "2 条评论");

    // {0} and the counts.
    set_language(Language::english);
    assert(trf("What's new in {0}", {"v2"}) == "What's new in v2");
    assert(trf("{0} of {1}", {"1 MB", "4 MB"}) == "1 MB of 4 MB");
    assert(trn(1, "{0} like", "{0} likes") == "1 like");
    assert(trn(0, "{0} like", "{0} likes") == "0 likes");
    assert(trn(7, "{0} comment", "{0} comments") == "7 comments");
    set_language(Language::french);
    assert(trn(0, "{0} comment", "{0} comments") == "0 commentaire"); // nought is singular there
    assert(trn(2, "{0} comment", "{0} comments") == "2 commentaires");
    set_language(Language::spanish);
    assert(trn(1, "{0} comment", "{0} comments") == "1 comentario");
    // A {0} the call has no argument for is dropped, not read out of bounds.
    assert(trf("{0} of {1}", {"1 MB"}) == "1 MB de ");

    // Capitals for the small tracked labels: accented letters too, and the sharp s as two.
    assert(upper("update available") == "UPDATE AVAILABLE");
    assert(upper("actualización") == "ACTUALIZACIÓN");
    assert(upper("Français à ñ ü ç œ") == "FRANÇAIS À Ñ Ü Ç Œ");
    assert(upper("Straße") == "STRASSE");
    assert(upper("日本語") == "日本語");
    assert(upper("a÷b") == "A÷B"); // the division sign is not a letter
    // Turkish: the dotted i is İ and the dotless ı is I, but only while the store speaks Turkish.
    set_language(Language::turkish);
    assert(upper("güncelleme mevcut") == "GÜNCELLEME MEVCUT");
    assert(upper("işığı ğ") == "İŞIĞI Ğ");
    set_language(Language::english);
    assert(upper("işığı") == "IŞIĞI");

    // The table: a sentence once, in every language, with the same {0}s in all of them.
    std::set<std::string> seen;
    for (std::size_t i = 0; i < kStringCount; ++i)
    {
        const Row &row = kStrings[i];
        assert(!row.text[0].empty());
        assert(seen.insert(std::string(row.text[0])).second); // no sentence twice
        for (int column = 1; column < kLanguageCount; ++column)
        {
            assert(!row.text[column].empty());
            assert(placeholders(row.text[column]) == placeholders(row.text[0]));
        }
    }
    return 0;
}

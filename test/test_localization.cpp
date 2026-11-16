// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Language files and the grammar helpers of the message formatter: club
// articles, plurals, dates, position and competition names; numbers and
// money in each language's conventions, typed amounts read back, and the
// English fallback for a broken or incomplete language file.

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "global/global.h"
#include "global/language_manager.h"
#include "global/number_format.h"
#include "global/paths.h"
#include "global/runtime_paths.h"
#include "model/club_article.h"
#include "model/competition.h"
#include "model/inbox.h"
#include "model/league.h"
#include "model/role_utils.h"

namespace
{
nlohmann::json loadJson(std::string_view path)
{
  std::ifstream stream{std::string(path)};
  return nlohmann::json::parse(stream);
}

nlohmann::json languageFile(const char* file)
{
  return loadJson(AssetPaths::root() + "assets/lang/" + file);
}

/** Positional placeholder indices ("{0}", "{1:di}", "{2|a|b}" -> 0, 1, 2). */
std::set<int> positionalIndices(std::string_view text)
{
  static const std::regex PLACEHOLDER(R"(\{(\d+)[}:|])");
  std::set<int> indices;
  using Iterator = std::regex_iterator<std::string_view::const_iterator>;
  for (auto it = Iterator(text.begin(), text.end(), PLACEHOLDER);
       it != Iterator(); ++it)
    indices.insert(std::stoi((*it)[1].str()));
  return indices;
}

/** printf conversions in order, "%%" excluded. */
std::vector<std::string> printfConversions(std::string_view text)
{
  static const std::regex CONVERSION(
      R"(%[-+#0]*[0-9]*(?:\.[0-9]+)?(?:hh|h|ll|l|z|j|t)?([diouxXeEfgGcsp%]))");
  std::vector<std::string> conversions;
  using Iterator = std::regex_iterator<std::string_view::const_iterator>;
  for (auto it = Iterator(text.begin(), text.end(), CONVERSION);
       it != Iterator(); ++it)
  {
    if ((*it)[1].str() != "%") conversions.push_back(it->str());
  }
  return conversions;
}

class LanguageFormatTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  }

  void TearDown() override
  {
    LanguageManager::instance().loadLanguage(Language::EN);
  }

  static void useItalian()
  {
    ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::IT));
  }
};
}  // namespace

TEST(LanguageFilesTest, BothLanguagesHaveTheSameKeysAndPlaceholders)
{
  const nlohmann::json english = languageFile("English.json");
  const nlohmann::json italian = languageFile("Italian.json");
  ASSERT_EQ(english.size(), italian.size());
  static const std::regex SPEC(R"(\{\d+([:|])([^}]*)\})");
  static const std::set<std::string, std::less<>> PREPOSITIONS = {
      "il", "Il", "di", "Di", "a", "A", "da", "Da", "in", "In", "su", "Su"};
  for (const auto& [key, value] : english.items())
  {
    ASSERT_TRUE(italian.contains(key)) << key;
    const std::string en = value.get<std::string>();
    const std::string it = italian.at(key).get<std::string>();
    EXPECT_EQ(positionalIndices(en), positionalIndices(it)) << key;
    if (positionalIndices(en).empty())
      EXPECT_EQ(printfConversions(en), printfConversions(it)) << key;
    for (const std::string& text : {en, it})
    {
      for (auto match = std::sregex_iterator(text.begin(), text.end(), SPEC);
           match != std::sregex_iterator(); ++match)
      {
        if ((*match)[1].str() == ":")
          EXPECT_TRUE(PREPOSITIONS.contains((*match)[2].str())) << key;
        else
          EXPECT_NE((*match)[2].str().find('|'), std::string::npos) << key;
      }
    }
  }
}

TEST(LanguageFilesTest, EveryPackLeagueAndCupHasADisplayName)
{
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  const nlohmann::json english = languageFile("English.json");
  const nlohmann::json italian = languageFile("Italian.json");
  for (const auto& item : loadJson(AssetPaths::leagues()))
  {
    const League league(item.at("id").get<LeagueID>(),
                        item.at("name").get<std::string>());
    const std::string arg = Competitions::leagueNameArg(league);
    ASSERT_EQ(arg.front(), '@') << league.getName();
    EXPECT_TRUE(english.contains(arg.substr(1))) << arg;
    EXPECT_TRUE(italian.contains(arg.substr(1))) << arg;
    // The English name is the data pack's own, so English is unchanged.
    EXPECT_EQ(english.at(arg.substr(1)).get<std::string>(), league.getName());
    if (!item.contains("parent_league"))
    {
      std::string cup = "CUP_NAME_" + arg.substr(13);
      EXPECT_TRUE(english.contains(cup)) << cup;
      EXPECT_TRUE(italian.contains(cup)) << cup;
    }
  }
}

TEST(InboxFormatTest, ItalianArticlesFollowTheClubName)
{
  EXPECT_EQ(ClubArticle::italian("Lecce"), "il");
  EXPECT_EQ(ClubArticle::italian("Roma"), "la");  // "article_it" in the pack
  EXPECT_EQ(ClubArticle::italian("Acaya"), "l'");
  EXPECT_EQ(ClubArticle::italian("Hull Town"), "l'");
  EXPECT_EQ(ClubArticle::italian("Stoke City"), "lo");
  EXPECT_EQ(ClubArticle::italian("Zaragoza"), "lo");
  EXPECT_EQ(ClubArticle::italian("Southampton"), "il");
  EXPECT_EQ(ClubArticle::withPreposition("Roma", "di"), "della Roma");
  EXPECT_EQ(ClubArticle::withPreposition("Acaya", "a"), "all'Acaya");
  EXPECT_EQ(ClubArticle::withPreposition("Lecce", "da"), "dal Lecce");
  EXPECT_EQ(ClubArticle::withPreposition("Stoke City", "in"),
            "nello Stoke City");
  EXPECT_EQ(ClubArticle::withPreposition("Roma", "Il"), "La Roma");
  EXPECT_EQ(ClubArticle::withPreposition("Acaya", "Il"), "L'Acaya");
  EXPECT_EQ(ClubArticle::withPreposition("Lecce", "per"), "Lecce");
}

TEST_F(LanguageFormatTest, ClubPlaceholdersRenderArticlesOnlyInItalian)
{
  const std::vector<std::string> roma = {"Mario Rossi", "Roma"};
  EXPECT_EQ(formatLocalized("INBOX_MANAGER_APPOINTED_BODY", roma),
            "Mario Rossi is the new manager of Roma.");
  useItalian();
  EXPECT_EQ(formatLocalized("INBOX_MANAGER_APPOINTED_BODY", roma),
            "Mario Rossi è il nuovo allenatore della Roma.");
  EXPECT_EQ(
      formatLocalized("INBOX_MANAGER_APPOINTED_BODY", {"Mario Rossi", "Lecce"}),
      "Mario Rossi è il nuovo allenatore del Lecce.");
  EXPECT_EQ(formatLocalized("INBOX_JOB_REJECTED_TITLE", {"Acaya"}),
            "L'Acaya ha respinto la tua candidatura");
  // Key arguments are never given an article.
  EXPECT_EQ(formatLocalized("INBOX_SIGNING_BODY",
                            {"Mario Rossi", "@INBOX_FREE_AGENT", "€0"}),
            "Mario Rossi arriva svincolato per €\u00a00.");
}

TEST_F(LanguageFormatTest, PluralPlaceholdersPickTheWordForTheCount)
{
  EXPECT_EQ(formatLocalized("YOUTH_DAYS", {"1"}), "1 day");
  EXPECT_EQ(formatLocalized("YOUTH_DAYS", {"0"}), "0 days");
  useItalian();
  EXPECT_EQ(formatLocalized("YOUTH_DAYS", {"1"}), "1 giorno");
  EXPECT_EQ(formatLocalized("YOUTH_DAYS", {"12"}), "12 giorni");
  EXPECT_EQ(formatLocalized("INBOX_CONTRACTS_BODY", {"1", "Mario Rossi"}),
            "1 giocatore ha il contratto in scadenza a fine stagione: Mario "
            "Rossi.");
}

TEST_F(LanguageFormatTest, IsoDateArgumentsUseTheLocalisedMonth)
{
  const std::vector<std::string> args = {"Mario Rossi", "Lecce", "2027-06-30",
                                         "50"};
  EXPECT_NE(formatLocalized("INBOX_LOAN_OUT_BODY", args).find("30 Jun 2027"),
            std::string::npos);
  useItalian();
  const std::string italian = formatLocalized("INBOX_LOAN_OUT_BODY", args);
  EXPECT_NE(italian.find("al Lecce fino al 30 giu 2027"), std::string::npos)
      << italian;
  EXPECT_EQ(localizedDate("2027-06-30"), "30 giu 2027");
  // Anything that is not a whole ISO date stays as it is.
  EXPECT_EQ(localizedDate("not a date"), "not a date");
  EXPECT_EQ(formatLocalized("YOUTH_DAYS", {"2027-06-3"}), "2027-06-3 giorni");
}

TEST_F(LanguageFormatTest, PositionNamesFollowTheLanguage)
{
  EXPECT_STREQ(RoleUtils::shortName(PlayerRole::GK), "GK");
  EXPECT_STREQ(RoleUtils::longName(PlayerRole::CB), "Centre-back");
  EXPECT_EQ(RoleUtils::shortNameArg(PlayerRole::ST), "@ROLE_SHORT_ST");
  EXPECT_EQ(RoleUtils::toString(PlayerRole::GK), "GK");  // persistence code
  useItalian();
  EXPECT_STREQ(RoleUtils::shortName(PlayerRole::GK), "POR");
  EXPECT_STREQ(RoleUtils::shortName(PlayerRole::CAM), "COC");
  EXPECT_STREQ(RoleUtils::longName(PlayerRole::CB), "Difensore centrale");
  EXPECT_EQ(RoleUtils::toString(PlayerRole::GK), "GK");
  // A saved list of position keys is shown in the current language.
  const std::string preview = formatLocalized(
      "INBOX_YOUTH_PREVIEW_BODY",
      {"Mario Rossi", "@YOUTH_QUALITY_PROMISING",
       "@ROLE_SHORT_GK, @ROLE_SHORT_CB", "@YOUTH_PERS_CONTENT", "8"});
  EXPECT_NE(preview.find("in POR, DC"), std::string::npos) << preview;
}

TEST_F(LanguageFormatTest, LeagueNamesAreTranslatedAndCustomNamesKept)
{
  const League italian_league(1, "Italian League");
  const League custom(40, "Apulian Amateur League");
  EXPECT_EQ(Competitions::leagueName(italian_league), "Italian League");
  useItalian();
  EXPECT_EQ(Competitions::leagueName(italian_league), "Campionato italiano");
  EXPECT_EQ(Competitions::leagueName(custom), "Apulian Amateur League");
  EXPECT_EQ(Competitions::leagueNameArg(custom), "Apulian Amateur League");
}

// ---------------------------------------------------------------------------
// Numbers and money
// ---------------------------------------------------------------------------

TEST_F(LanguageFormatTest, NumbersAndMoneyFollowTheLanguage)
{
  using namespace NumberFormat;
  EXPECT_EQ(money(0), "€0");
  EXPECT_EQ(money(13'400), "€13K");
  EXPECT_EQ(money(2'450'000), "€2.45M");
  EXPECT_EQ(money(-31'000'000), "-€31.0M");
  EXPECT_EQ(money(1'250'000'000), "€1.25B");
  EXPECT_EQ(moneyFull(1'250'000), "€1,250,000");
  EXPECT_EQ(grouped(38'500), "38,500");
  EXPECT_EQ(grouped(-1'234'567), "-1,234,567");
  EXPECT_EQ(decimal(7.25, 1), "7.2");
  useItalian();
  EXPECT_EQ(current().decimal_mark, ",");
  EXPECT_EQ(money(0), "€ 0");
  EXPECT_EQ(money(1'500), "€ 1,5 mila");
  EXPECT_EQ(money(13'400), "€ 13 mila");
  EXPECT_EQ(money(2'450'000), "€ 2,45 mln");
  EXPECT_EQ(money(-31'000'000), "-€ 31,0 mln");
  EXPECT_EQ(money(1'250'000'000), "€ 1,25 mld");
  EXPECT_EQ(moneyFull(1'250'000), "€ 1.250.000");
  EXPECT_EQ(moneyFull(-999), "-€ 999");
  EXPECT_EQ(grouped(38'500), "38.500");
  EXPECT_EQ(decimal(7.5, 1), "7,5");
}

namespace
{
struct MoneyCase
{
  const char* text;
  std::optional<int64_t> english;  // nullopt: refused in English
  std::optional<int64_t> italian;
  NumberFormat::MoneyError error = NumberFormat::MoneyError::NONE;
};

constexpr std::optional<int64_t> REFUSED = std::nullopt;
using enum NumberFormat::MoneyError;

// Every way a player is likely to type an amount, in both languages.
constexpr std::array<MoneyCase, 56> MONEY_CASES = {{
    {"1,5M", 1'500'000, 1'500'000},
    {"1.5M", 1'500'000, 1'500'000},
    {"1.500.000", 1'500'000, 1'500'000},
    {"1,500,000", 1'500'000, 1'500'000},
    {"1500k", 1'500'000, 1'500'000},
    {"€ 2,25 mln", 2'250'000, 2'250'000},
    {"€2.25 mln", 2'250'000, 2'250'000},
    {"1,5 mln €", 1'500'000, 1'500'000},
    {"€\u00A01,5\u00A0mln", 1'500'000, 1'500'000},
    {"0,5m", 500'000, 500'000},
    {",5m", 500'000, 500'000},
    {"15m", 15'000'000, 15'000'000},
    {"€14.4M", 14'400'000, 14'400'000},
    {"850k", 850'000, 850'000},
    {"850 mila", 850'000, 850'000},
    {"3 mld", 3'000'000'000, 3'000'000'000},
    {"2 bn", 2'000'000'000, 2'000'000'000},
    {"5 million", 5'000'000, 5'000'000},
    {"3 milioni", 3'000'000, 3'000'000},
    {"1 500 000", 1'500'000, 1'500'000},
    {"1'500'000", 1'500'000, 1'500'000},
    {"€1,250,000", 1'250'000, 1'250'000},
    {"€\u00A01.250.000", 1'250'000, 1'250'000},
    {"1.234.567,89", 1'234'568, 1'234'568},
    {"1,234,567.89", 1'234'568, 1'234'568},
    {"14.500", 14'500, 14'500},
    {"14,500", 14'500, 14'500},
    {"950", 950, 950},
    {"12,50", 13, 13},
    {"EUR 300k", 300'000, 300'000},
    {"300k euro", 300'000, 300'000},
    // A single mark before three digits and a unit: only the language's
    // own decimal mark reads as one.
    {"1.250M", 1'250'000, REFUSED, AMBIGUOUS},
    {"1,250M", REFUSED, 1'250'000, AMBIGUOUS},
    {"1.500k", 1'500, REFUSED, AMBIGUOUS},
    {"1,500k", REFUSED, 1'500, AMBIGUOUS},
    // Separators that could mean two different amounts.
    {"1.50.000", REFUSED, REFUSED, AMBIGUOUS},
    {"1.500,000", REFUSED, REFUSED, AMBIGUOUS},
    {"1,500.000", REFUSED, REFUSED, AMBIGUOUS},
    {"1.500.000k", REFUSED, REFUSED, AMBIGUOUS},
    {"1.234,5M", REFUSED, REFUSED, AMBIGUOUS},
    {"1..5M", REFUSED, REFUSED, AMBIGUOUS},
    {"1,5,0", REFUSED, REFUSED, AMBIGUOUS},
    {"1,2345", REFUSED, REFUSED, AMBIGUOUS},
    {"1.", REFUSED, REFUSED, AMBIGUOUS},
    // Not an amount at all.
    {"", REFUSED, REFUSED, EMPTY},
    {"  ", REFUSED, REFUSED, EMPTY},
    {"€", REFUSED, REFUSED, EMPTY},
    {"abc", REFUSED, REFUSED, INVALID},
    {"12x", REFUSED, REFUSED, INVALID},
    {"1kM", REFUSED, REFUSED, INVALID},
    {"1k5", REFUSED, REFUSED, INVALID},
    {"k", REFUSED, REFUSED, INVALID},
    {"-5", REFUSED, REFUSED, INVALID},
    {"1 5 m", REFUSED, REFUSED, AMBIGUOUS},
    {"99999999999999999999", REFUSED, REFUSED, TOO_LARGE},
    {"10000000b", REFUSED, REFUSED, TOO_LARGE},
}};
}  // namespace

TEST_F(LanguageFormatTest, TypedMoneyIsReadTheSameWayInBothLanguages)
{
  for (const Language language : {Language::EN, Language::IT})
  {
    ASSERT_TRUE(LanguageManager::instance().loadLanguage(language));
    const bool italian = language == Language::IT;
    for (const MoneyCase& item : MONEY_CASES)
    {
      const auto expected = italian ? item.italian : item.english;
      const NumberFormat::MoneyParse parsed =
          NumberFormat::parseMoney(item.text);
      EXPECT_EQ(parsed.value, expected)
          << (italian ? "IT " : "EN ") << '"' << item.text << '"';
      if (!expected.has_value())
      {
        EXPECT_EQ(parsed.error, item.error) << '"' << item.text << '"';
        EXPECT_NE(NumberFormat::errorKey(parsed.error), nullptr);
      }
    }
    // What the field shows is read back unchanged.
    for (const int64_t amount : {int64_t{0}, int64_t{950}, int64_t{14'500},
                                 int64_t{1'250'000}, int64_t{987'654'321}})
    {
      EXPECT_EQ(NumberFormat::parseMoney(NumberFormat::moneyFull(amount)).value,
                amount);
      const auto compact =
          NumberFormat::parseMoney(NumberFormat::money(amount)).value;
      ASSERT_TRUE(compact.has_value()) << NumberFormat::money(amount);
      EXPECT_NEAR(static_cast<double>(*compact), static_cast<double>(amount),
                  static_cast<double>(amount) * 0.05 + 1.0);
    }
  }
}

TEST_F(LanguageFormatTest, MessageMoneyAndKeysAreShownInTheReadersLanguage)
{
  // Saved messages carry the amount and keys, never finished text.
  EXPECT_EQ(formatMoney(1'500'000), "€1500000");
  const std::vector<std::string> signing = {"Mario Rossi", FREE_AGENTS_NAME_ARG,
                                            formatMoney(1'500'000)};
  EXPECT_EQ(formatLocalized("INBOX_SIGNING_BODY", signing),
            "Mario Rossi has signed from free agency for €1.50M.");
  // An unknown key is its own pattern: "{0}" shows the argument alone.
  const auto alone = [](const std::string& argument)
  { return formatLocalized("{0}", {argument}); };
  const std::string digest = "Mario Rossi: @INBOX_FREE_AGENCY -> Roma (" +
                             formatMoney(2'000'000) + ")";
  EXPECT_EQ(alone(digest), "Mario Rossi: free agency -> Roma (€2.00M)");
  EXPECT_EQ(alone("Mario Rossi (@NT_Italian)"), "Mario Rossi (Italy)");
  EXPECT_EQ(alone(formatMoney(-2'000'000)), "-€2.00M");
  // Messages saved before keep their text; a lone "@" is not a key.
  EXPECT_EQ(alone("€1.25M"), "€1.25M");
  EXPECT_EQ(alone("-€2.0M and €350K"), "-€2.0M and €350K");
  EXPECT_EQ(alone("@"), "@");
  EXPECT_EQ(alone("Rossi @ Roma, @UNKNOWN_KEY_X"),
            "Rossi @ Roma, @UNKNOWN_KEY_X");

  useItalian();
  EXPECT_EQ(formatLocalized("INBOX_SIGNING_BODY", signing),
            "Mario Rossi arriva svincolato per € 1,50 mln.");
  EXPECT_EQ(alone(digest), "Mario Rossi: svincolato -> Roma (€ 2,00 mln)");
  EXPECT_EQ(alone("Mario Rossi (@NT_Italian)"), "Mario Rossi (Italia)");
  EXPECT_EQ(alone("@NT_Italian, @NT_French"), "Italia, Francia");
  EXPECT_EQ(alone("€1.25M"), "€1.25M");
}

// ---------------------------------------------------------------------------
// English fallback
// ---------------------------------------------------------------------------

namespace
{
std::filesystem::path writeFile(const std::string& name,
                                const std::string& contents)
{
  const std::filesystem::path path = RuntimePaths::root() / name;
  std::ofstream(path, std::ios::binary) << contents;
  return path;
}

std::string readText(const std::string& path)
{
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_F(LanguageFormatTest, ABrokenLanguageFileFallsBackToEnglish)
{
  LanguageManager& languages = LanguageManager::instance();
  const nlohmann::json english = languageFile("English.json");
  const std::string settings = english.at("MENU_SETTINGS").get<std::string>();
  const std::string italianText =
      readText(AssetPaths::root() + "assets/lang/Italian.json");
  ASSERT_GT(italianText.size(), 1000U);

  const std::filesystem::path truncated = writeFile(
      "fm_lang_truncated.json", italianText.substr(0, italianText.size() / 2));
  const std::filesystem::path garbage =
      writeFile("fm_lang_garbage.json", "\001\002 not json {");
  const std::filesystem::path list = writeFile("fm_lang_list.json", "[1, 2]");
  const std::filesystem::path missing =
      RuntimePaths::root() / "fm_lang_missing.json";
  std::filesystem::remove(missing);

  for (const auto& path : {truncated, garbage, list, missing})
  {
    EXPECT_FALSE(languages.loadFromFile(path.string(), Language::IT)) << path;
    EXPECT_TRUE(languages.status().file_failed) << path;
    EXPECT_TRUE(languages.status().fallback());
    EXPECT_EQ(languages.current(), Language::IT);
    // English everywhere instead of raw keys, English number formats too.
    EXPECT_EQ(std::string(LOC("MENU_SETTINGS")), settings) << path;
    EXPECT_EQ(NumberFormat::money(1'500'000), "€1.50M");
  }
  // A text missing everywhere is still its own key (same pointer).
  const char* unknown = "NO_SUCH_TEXT_ANYWHERE";
  EXPECT_EQ(LOC(unknown), unknown);

  // An incomplete file: what it has is used, the rest is English.
  const std::filesystem::path partial =
      writeFile("fm_lang_partial.json",
                R"({"MENU_SETTINGS": "Impostazioni", "NUMBER_DECIMAL_MARK": ",",
          "NUMBER_GROUP_MARK": ".", "BROKEN_ENTRY": 5})");
  EXPECT_TRUE(languages.loadFromFile(partial.string(), Language::IT));
  EXPECT_FALSE(languages.status().file_failed);
  EXPECT_EQ(languages.status().missing_keys, english.size() - 3);
  EXPECT_STREQ(LOC("MENU_SETTINGS"), "Impostazioni");
  EXPECT_EQ(std::string(LOC("MENU_QUIT")),
            english.at("MENU_QUIT").get<std::string>());
  EXPECT_EQ(NumberFormat::grouped(38'500), "38.500");

  // The real files load cleanly and clear the fallback.
  useItalian();
  EXPECT_FALSE(languages.status().fallback());
  EXPECT_STREQ(LOC("MENU_SETTINGS"), "Impostazioni");
  ASSERT_TRUE(languages.loadLanguage(Language::EN));
  EXPECT_FALSE(languages.status().fallback());
  EXPECT_EQ(std::string(LOC("MENU_SETTINGS")), settings);
  for (const auto& path : {truncated, garbage, list, partial})
    std::filesystem::remove(path);
}

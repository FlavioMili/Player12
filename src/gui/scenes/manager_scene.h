// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gui/scenes/management_scene.h"
#include "model/manager_career.h"
#include "model/national_job.h"

class GameController;

/**
 * @brief The "your manager" card of the new-game club choice: name,
 * nationality, experience (starting reputation and licence), preferred
 * style, and the option to start without a club.
 */
class ManagerSetupPanel
{
 public:
  enum class Action : uint8_t
  {
    NONE,
    START_UNEMPLOYED
  };

  /** @brief Draws the card across @p width. */
  Action render(const GameController& controller, float width);

  /** @brief What the manager chose (names never empty). */
  [[nodiscard]] ManagerSetup setup() const;

 private:
  void initialize(const GameController& controller);

  bool initialized = false;
  std::array<char, 40> first_name{};
  std::array<char, 40> last_name{};
  std::string default_first;
  std::string default_last;
  int nationality = 0;
  int background = static_cast<int>(
      std::to_underlying(ManagerBackground::ProfessionalPlayer));
  int style = 0;
  int age = 42;
};

/**
 * @brief The manager's own screen: profile card with reputation, licence
 * and contract, career history, season finishes and honours, and the Job
 * Centre (vacancies with the chance of an interview, applications,
 * interviews, offers with negotiation, resignation).
 */
class ManagerScene : public ManagementScene
{
 public:
  enum class Tab : uint8_t
  {
    PROFILE,
    JOB_CENTRE
  };

  /** Without @p tab the Job Centre opens while out of work. */
  explicit ManagerScene(GUIView* parent, std::optional<Tab> tab = std::nullopt);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::MANAGER; }

  /** @brief Home page of a manager out of work (drawn by the dashboard). */
  static void renderUnemployedHome(GUIView* view);

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::MANAGER;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  struct StintRow
  {
    std::string club;
    std::string league;
    std::string period;
    std::string record; /*!< "W-D-L". */
    std::string win_rate;
    std::string trophies;
    const char* departure_key = "";
    bool current = false;
  };

  struct SeasonRow
  {
    std::string season;
    std::string club;
    std::string finish;
    std::string expected;
    int margin = 0; /*!< Expected minus actual position. */
  };

  struct AwardRow
  {
    std::string season;
    const char* award_key = "";
    std::string club;
  };

  struct VacancyRow
  {
    TeamID team_id = 0;
    std::string club;
    std::string league;
    std::string reputation;
    std::string expectation;
    const char* owner_key = "";
    const char* licence_key = "";
    bool licence_missing = false;
    float chance = 0.0f;
    std::string chance_text;
    std::string opened;
    std::optional<ApplicationStage> stage;
  };

  struct OfferRow
  {
    std::uint32_t id = 0;
    TeamID team_id = 0;
    std::string club;
    std::string league;
    std::string wage;
    std::string terms; /*!< Years, release clause, expiry. */
    std::string compensation;
    bool unsolicited = false;
    std::int64_t weekly_wage = 0;
    int years = 2;
  };

  /** @brief A button press, executed once the page is drawn. */
  struct PendingAction
  {
    enum class Kind : uint8_t
    {
      NONE,
      APPLY,
      ACCEPT,
      DECLINE,
      NEGOTIATE,
      RESIGN,
      INTERVIEW,
      NATIONAL_APPLY,
      NATIONAL_ACCEPT,
      NATIONAL_DECLINE,
      NATIONAL_RESIGN
    };
    Kind kind = Kind::NONE;
    std::uint32_t id = 0; /*!< Club or offer. */
  };

  void renderProfile();
  void renderProfileCard(float width);
  void renderHonours(float width) const;
  void renderHistory() const;
  void renderSeasons() const;
  void renderJobCentre();
  void renderOffers();
  void renderInterviews();
  void renderVacancies();
  void renderVacancyDetail(const VacancyRow& row);
  void renderInterviewDialog();
  void renderConfirmations();
  void runPendingAction();
  // National-team jobs (manager_scene_national.cpp).
  void refreshNational();
  void renderNationalCard(float width);
  void renderNationalOffers();
  void renderNationalVacancies();
  void renderNationalConfirmation();
  /** True when the pending action was a national-team one (and ran). */
  bool runNationalAction(const PendingAction& action);

  Tab tab = Tab::PROFILE;
  bool unemployed = false;

  // Profile
  std::string subtitle;
  std::string reputation_value;
  const char* reputation_tier_key = "";
  float reputation = 0.0f;
  std::string licence_value;
  std::string licence_note;
  std::string club_value;
  std::string club_note;
  std::string record_value;
  std::string record_note;
  std::vector<std::pair<const char*, std::string>> profile_facts;
  std::vector<StintRow> stints;
  std::vector<SeasonRow> seasons;
  std::vector<AwardRow> awards;

  // Job Centre
  std::vector<VacancyRow> vacancies;
  std::vector<OfferRow> offers;
  std::vector<TeamID> interviews;
  TeamID selected_vacancy = 0;
  std::uint32_t negotiating = 0;
  std::int64_t counter_wage = 0;
  int counter_years = 1; /*!< Index: years - 1. */

  // Interview dialog
  TeamID interview_club = 0;
  std::string interview_club_name;
  std::size_t interview_step = 0;
  std::array<int, INTERVIEW_TOPICS> interview_answers{};
  std::optional<InterviewResult> interview_result;
  bool interview_requested = false;
  bool interview_close_requested = false;

  // Confirmations
  bool resign_requested = false;
  std::uint32_t accept_candidate = 0;
  std::string accept_text;
  bool accept_requested = false;

  PendingAction pending;

  // National-team jobs
  struct NationalVacancyRow
  {
    Language nation = Language::EN;
    std::string name;
    std::string rank;
    std::string wage;
    const char* licence_key = "";
    bool licence_missing = false;
    float chance = 0.0f;
    std::string chance_text;
    std::optional<NationalApplicationStage> stage;
  };
  struct NationalOfferRow
  {
    std::uint32_t id = 0;
    std::string name;
    std::string wage;
    std::string terms;
    bool unsolicited = false;
  };
  bool national_job = false;
  /** He has a club and is not famous enough to add a national team. */
  bool national_blocked = false;
  std::string national_value;
  std::string national_note;
  std::vector<std::pair<const char*, std::string>> national_facts;
  std::vector<std::string> national_history;
  std::vector<NationalVacancyRow> national_vacancies;
  std::vector<NationalOfferRow> national_offers;
  int selected_nation = -1;
  bool resign_national_requested = false;
};

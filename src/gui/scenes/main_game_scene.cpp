// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "main_game_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <utility>

#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/manager_scene.h"
#include "gui/scenes/match_report_scene.h"
#include "gui/scenes/match_scene.h"
#include "gui/scenes/onboarding_overlay.h"
#include "gui/scenes/team_selection_scene.h"
#include "gui/view_models/competition_view.h"
#include "gui/view_models/formation.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/competition.h"
#include "model/game.h"

namespace
{
constexpr size_t MAX_KEY_PLAYERS = 11;
constexpr size_t MAX_RECENT_RESULTS = 5;
constexpr float TWO_COLUMN_MIN_WIDTH = 860.0f;
constexpr float NEXT_MATCH_HEIGHT = 192.0f;
/** Below these heights the cards stop filling the window and flow. */
constexpr float DASHBOARD_FILL_MIN_HEIGHT = 420.0f;
constexpr float FINANCE_FILL_MIN_HEIGHT = 360.0f;
constexpr int WEEKS_PER_YEAR = 52;
constexpr uint8_t SEASON_START_MONTH = 7;
constexpr size_t MAX_TREND_POINTS = 120;
constexpr size_t FINANCE_MONTH_DAYS = 30;
constexpr size_t CONTINUE_LOG_LINES = 4;
constexpr double CONTINUE_OVERLAY_DELAY = 0.15;
constexpr double CONTINUE_FADE_SECONDS = 0.12;
constexpr float CONTINUE_DIM = 120.0f;

/** Fixed-height card when height > 0, otherwise a card sized to content. */
void beginDashboardCard(const char* id, const char* title, float width,
                        float height)
{
  if (height > 0.0f)
    UI::beginCard(id, title, ImVec2(width, height), true);
  else
    UI::beginAutoHeightCard(id, title, width);
}

std::string ordinalPosition(size_t position)
{
  return fmt::sprintf(LOC("DASHBOARD_POSITION_VALUE"), position);
}

}  // namespace

SceneID MainGameScene::getID() const { return SceneID::GAME_MENU; }

MainGameScene::MainGameScene(GUIView* guiViewPtr) : ManagementScene(guiViewPtr)
{
}

MainGameScene::~MainGameScene()
{
  // The worker never outlives the hub. A holiday or an off-season run is
  // called back after the day being simulated; a Continue to the next
  // fixture completes its days first.
  if (continuation_running && stop_on_close && continue_controller != nullptr)
    continue_controller->requestContinueStop();
  if (continue_operation.valid()) continue_operation.wait();
}

void MainGameScene::requestStop()
{
  if (!continuation_running || stop_requested) return;
  stop_requested = true;
  guiView->getController().requestContinueStop();
}

void MainGameScene::showPendingSeasonReview()
{
  // After the Continue that ended the season, once the screens above the
  // hub, the holiday report and the welcome tour are out of the way.
  if (continuation_running || continuation_requested ||
      season_review_dialog.isOpen() || holidayDialog().isOpen() ||
      welcome_requested || welcome_tour.isOpen() ||
      guiView->getOverlayDepth() > 0)
    return;
  const GameController& controller = guiView->getController();
  if (const SeasonReview* review = controller.getUnseenSeasonReview())
    season_review_dialog.open(controller, *review);
}

NavSection MainGameScene::navSection() const
{
  return active_page == Page::FINANCES ? NavSection::FINANCES
                                       : NavSection::HOME;
}

void MainGameScene::onEnter()
{
  const GameController& controller = guiView->getController();
  // A career out of work has no club to choose: it looks for a job.
  if (!controller.hasSelectedTeam() && !controller.isUnemployed())
    guiView->overlayScene(std::make_unique<TeamSelectionScene>(guiView));
  else
    refreshData();
}

void MainGameScene::update(float /*deltaTime*/)
{
  showPendingSeasonReview();
  // Wait for the frozen backdrop first so it shows the screen, not the card.
  if (continuation_requested && !continuation_running &&
      !guiView->isBackdropPending())
  {
    continuation_requested = false;
    startContinuation();
  }

  if (continuation_running) trackContinueProgress();

  if (continuation_running &&
      continue_operation.wait_for(std::chrono::seconds::zero()) ==
          std::future_status::ready)
  {
    continuation_running = false;
    guiView->releaseBackdrop();
    continue_log.clear();
    continue_finished_at = continue_overlay_shown ? ImGui::GetTime() : -1.0;
    try
    {
      const int advancedDays = continue_operation.get();
      // After a match its report is the news, not the day count; otherwise
      // the toast says why the days stopped.
      const ContinueStop stop = guiView->getController().getLastContinueStop();
      const bool atFixture =
          stop == ContinueStop::None || stop == ContinueStop::Fixture;
      std::string toast =
          fmt::sprintf(Format::plural(atFixture ? "DASHBOARD_ADVANCED_DAYS"
                                                : "CONTINUE_ADVANCED_DAYS",
                                      advancedDays),
                       advancedDays);
      if (!atFixture)
        toast +=
            std::string("  ·  ") + LOC(GameController::continueStopKey(stop));
      if (!match_report && !holiday_running) showToast(toast);
      refreshData();
      if (holiday_running)
        holidayDialog().showSummary(
            guiView->getController(),
            guiView->getController().getHolidaySummary());
    }
    catch (const std::exception&)
    {
      showToast(LOC("DASHBOARD_ADVANCE_FAILED"), true);
    }
    holiday_running = false;
    // The match was recorded before the day was simulated: its report opens
    // either way.
    if (match_report)
    {
      guiView->navigateTo(std::make_unique<MatchReportScene>(
          guiView, match_report->date, match_report->home_id,
          match_report->away_id));
      match_report.reset();
    }
  }

  // Team selection is an overlay, so the dashboard does not re-enter when the
  // choice closes. Refresh once as soon as a club becomes available.
  if (cached_squad.empty() && !continuation_running &&
      guiView->getController().hasSelectedTeam())
    refreshData();
  refreshIfStale();
}

void MainGameScene::refreshIfStale()
{
  const GameController& controller = guiView->getController();
  if (continuation_running) return;
  // A new job (or the loss of one) changes the club the page shows.
  const auto managed = controller.getManagedTeam();
  if (cached_club != (managed ? managed->get().getId() : TeamID{0}))
  {
    refreshData();
    return;
  }
  if (!managed) return;
  bool stale = !(cached_date == controller.getCurrentDate());
  if (!stale && cached_next)
    if (const Game* game = controller.getGame())
      if (const Match* match = game->getCalendar().findMatch(
              cached_next->date, cached_next->home_id, cached_next->away_id))
        stale = match->isPlayed();
  if (stale) refreshData();
}

std::string MainGameScene::continueLabel() const
{
  const GameController& controller = guiView->getController();
  if (controller.isUnemployed()) return LOC("DASHBOARD_CONTINUE_UNEMPLOYED");
  if (!controller.hasSelectedTeam()) return {};
  if (cached_next && cached_next->date == controller.getCurrentDate())
    return LOC("DASHBOARD_PLAY_MATCH");
  if (cached_next)
    return fmt::sprintf(LOC("DASHBOARD_CONTINUE_TO"),
                        Format::dayMonth(cached_next->date));
  return LOC("DASHBOARD_CONTINUE");
}

void MainGameScene::requestContinue()
{
  const GameController& controller = guiView->getController();
  if (continuation_running ||
      (!controller.hasSelectedTeam() && !controller.isUnemployed()))
    return;
  if (cached_next && cached_next->date == controller.getCurrentDate())
  {
    guiView->navigateTo(std::make_unique<MatchScene>(
        guiView, cached_next->home_id, cached_next->away_id));
    return;
  }
  // Simulation starts from update() once every screen above the hub has
  // closed, so no screen reads the game state while it changes.
  continuation_requested = true;
  guiView->requestBackdropCapture();
  if (guiView->getOverlayDepth() > 0) guiView->navigateTo(nullptr);
}

void MainGameScene::requestHoliday(const HolidayPlan& plan)
{
  if (const GameController& controller = guiView->getController();
      continuation_running || !controller.hasSelectedTeam())
    return;
  pending_holiday = plan;
  continuation_requested = true;
  guiView->requestBackdropCapture();
  if (guiView->getOverlayDepth() > 0) guiView->navigateTo(nullptr);
}

void MainGameScene::requestPostMatchAdvance(const GameDateValue& date,
                                            TeamID home_id, TeamID away_id)
{
  if (continuation_running) return;
  match_report = PlayedMatch{date, home_id, away_id};
  pending_holiday.reset();
  continuation_requested = true;
  guiView->requestBackdropCapture();
  if (guiView->getOverlayDepth() > 0) guiView->navigateTo(nullptr);
}

void MainGameScene::startContinuation()
{
  continuation_running = true;
  continue_start_date = guiView->getController().getCurrentDate();
  continue_progress = {};
  continue_logged_day = 0;
  continue_day_matches = 0;
  continue_log.clear();
  continue_started_at = ImGui::GetTime();
  continue_overlay_shown = false;
  GameController* controllerPtr = &guiView->getController();
  continue_controller = controllerPtr;
  holiday_running = pending_holiday.has_value();
  stop_requested = false;
  // Closing the game interrupts open-ended runs; a Continue to the next
  // fixture (at most a few weeks) and the rest of a match day complete.
  stop_on_close =
      !match_report && (holiday_running || !cached_next.has_value() ||
                        controllerPtr->isUnemployed());
  // The rest of a managed match day: exactly one day, whatever comes next.
  if (match_report)
  {
    continue_operation = launchBackgroundTask(continue_worker,
                                              [controllerPtr]()
                                              {
                                                controllerPtr->advanceDay();
                                                return 1;
                                              });
    return;
  }
  if (pending_holiday)
  {
    continue_operation = launchBackgroundTask(
        continue_worker, [controllerPtr, plan = *pending_holiday]()
        { return controllerPtr->goOnHoliday(plan); });
    pending_holiday.reset();
    return;
  }
  continue_operation = launchBackgroundTask(
      continue_worker,
      [controllerPtr, hasFixture = cached_next.has_value(),
       unemployed = controllerPtr->isUnemployed()]()
      {
        // Out of work a week passes, or less when news arrives.
        if (unemployed) return controllerPtr->advanceWhileUnemployed(7);
        if (hasFixture) return controllerPtr->advanceToNextManagedFixture();
        // Off-season: on to whatever needs the manager next.
        return controllerPtr->advanceToNextEvent();
      });
}

void MainGameScene::trackContinueProgress()
{
  continue_progress = guiView->getController().getContinueProgress();
  // A day's match count is only known while it is being simulated; days the
  // poll never saw with matches are left out rather than guessed.
  while (continue_logged_day < continue_progress.days_done)
  {
    if (continue_day_matches > 0)
    {
      continue_log.push_back(fmt::sprintf(
          LOC("CONTINUE_LOG_MATCHES"),
          Format::dayMonth(continue_start_date +
                           static_cast<size_t>(continue_logged_day)),
          continue_day_matches));
      if (continue_log.size() > CONTINUE_LOG_LINES)
        continue_log.erase(continue_log.begin());
    }
    continue_day_matches = 0;
    ++continue_logged_day;
  }
  continue_day_matches =
      std::max(continue_day_matches, continue_progress.matches_total);
}

void MainGameScene::renderContinueOverlay()
{
  const Theme::Palette& palette = Theme::palette();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 min = viewport->WorkPos;
  const ImVec2 max(min.x + viewport->WorkSize.x, min.y + viewport->WorkSize.y);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  // Short waits (a quiet day) keep showing the frozen screen only; the card
  // appears once Continue takes longer than a blink, then fades in.
  const double elapsed = ImGui::GetTime() - continue_started_at;
  if (elapsed < CONTINUE_OVERLAY_DELAY) return;
  continue_overlay_shown = true;
  const float alpha =
      Theme::reducedMotion()
          ? 1.0f
          : std::clamp(static_cast<float>((elapsed - CONTINUE_OVERLAY_DELAY) /
                                          CONTINUE_FADE_SECONDS),
                       0.0f, 1.0f);
  drawList->AddRectFilled(
      min, max, IM_COL32(0, 0, 0, static_cast<int>(CONTINUE_DIM * alpha)));
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

  const float scale = Theme::scale();
  const ImVec2 cardSize(460.0f * scale, 0.0f);
  ImGui::SetCursorScreenPos(
      ImVec2(min.x + (viewport->WorkSize.x - cardSize.x) * 0.5f,
             min.y + viewport->WorkSize.y * 0.30f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, palette.surface);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * scale);
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      ImVec2(Theme::Space::XL * scale, Theme::Space::L * scale));
  ImGui::BeginChild("##continue_card", cardSize,
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                        ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  UI::sectionLabel(LOC("CONTINUE_TITLE"));
  const GameDateValue shownDate =
      continue_start_date +
      static_cast<size_t>(std::max(0, continue_progress.days_done));
  {
    Theme::ScopedText heading(Theme::Text::HEADING);
    ImGui::TextUnformatted(Format::date(shownDate).c_str());
  }
  const bool known = continue_progress.days_total > 0;
  const float fraction = continue_progress.fraction();
  const float barHeight = 8.0f * scale;
  if (known)
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, barHeight), "");
  else if (!Theme::reducedMotion())
    ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()),
                       ImVec2(-FLT_MIN, barHeight), "");
  if (known)
  {
    const std::string days = fmt::sprintf(
        LOC("CONTINUE_DAYS"),
        std::min(continue_progress.days_done + 1, continue_progress.days_total),
        continue_progress.days_total);
    ImGui::TextColored(palette.muted, "%s", days.c_str());
    ImGui::SameLine();
    UI::textRightColored(palette.muted,
                         std::format("{:.0f}%", fraction * 100.0f).c_str());
  }
  if (continue_progress.matches_total > 0)
    ImGui::TextUnformatted(fmt::sprintf(LOC("CONTINUE_MATCHES_NOW"),
                                        continue_progress.matches_done,
                                        continue_progress.matches_total)
                               .c_str());
  if (!continue_log.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    for (auto line = continue_log.rbegin(); line != continue_log.rend(); ++line)
      ImGui::TextColored(palette.faint, "%s", line->c_str());
  }
  // Stop ends the run once the day being simulated is complete.
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
  if (stop_requested)
  {
    ImGui::TextColored(palette.muted, "%s", LOC("CONTINUE_STOPPING"));
  }
  else if (UI::secondaryButton(LOC("CONTINUE_STOP")) ||
           ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    requestStop();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

float MainGameScene::continueFadeOut() const
{
  if (continuation_running || continue_finished_at < 0.0 ||
      Theme::reducedMotion())
    return 0.0f;
  const double since = ImGui::GetTime() - continue_finished_at;
  return std::clamp(1.0f - static_cast<float>(since / CONTINUE_FADE_SECONDS),
                    0.0f, 1.0f);
}

void MainGameScene::autoFixLineup()
{
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  if (!managed) return;
  const size_t replaced = controller.autoFixLineup(
      managed->get().getId(),
      cached_next ? cached_next->type : MatchType::LEAGUE);
  showToast(fmt::sprintf(
      Format::plural("DASHBOARD_AUTO_FIXED", static_cast<int64_t>(replaced)),
      replaced));
  refreshData();
}

void MainGameScene::renderContent()
{
  if (guiView->getController().isUnemployed())
  {
    ManagerScene::renderUnemployedHome(guiView);
    // A season that ended with the sack still gets its summary.
    renderSeasonReview();
    return;
  }
  if (!guiView->getController().getManagedTeam())
  {
    UI::emptyState(LOC("DASHBOARD_CHOOSE_CLUB"), nullptr);
    return;
  }
  if (active_page == Page::FINANCES)
    renderFinances();
  else
    renderOverview();
  // The welcome tour of a new career, over Home.
  if (welcome_requested)
  {
    welcome_requested = false;
    welcome_tour.open(guiView->getController());
  }
  welcome_tour.render();
  renderSeasonReview();
}

void MainGameScene::renderSeasonReview()
{
  if (const auto season = season_review_dialog.render(); season.has_value())
    guiView->getController().markSeasonReviewSeen(*season);
}

void MainGameScene::renderOverview()
{
  GameController& controller = guiView->getController();
  const Team& club = controller.getManagedTeam()->get();
  const Theme::Palette& palette = Theme::palette();

  const auto league = controller.getLeagueById(club.getLeagueId());
  const std::string subtitle = fmt::sprintf(
      LOC("DASHBOARD_SUBTITLE"),
      league ? Competitions::leagueName(league->get()).c_str() : "",
      cached_season, Format::date(controller.getCurrentDate()).c_str());
  UI::pageHeader(club.getName().c_str(), subtitle.c_str());

  // Headline figures.
  UI::TileRow tiles(4);
  const float width = tiles.width();
  tiles.next();
  if (const auto rank = std::ranges::find_if(
          cached_table, [&club](const CompetitionView::StandingRow& row)
          { return row.team_id == club.getId(); });
      rank != cached_table.end() && rank->played > 0)
  {
    const std::string value = ordinalPosition(
        static_cast<size_t>(std::distance(cached_table.begin(), rank)) + 1);
    const std::string footnote = fmt::sprintf(LOC("DASHBOARD_POINTS_PLAYED"),
                                              rank->points, rank->played);
    UI::statTile("tile_position", LOC("DASHBOARD_TILE_POSITION"), value.c_str(),
                 footnote.c_str(), palette.text, width);
  }
  else
  {
    UI::statTile("tile_position", LOC("DASHBOARD_TILE_POSITION"), "–",
                 LOC("DASHBOARD_NO_RESULTS"), palette.muted, width);
  }
  tiles.next();
  const int64_t balance = club.getFinances().getBalance();
  const std::string balanceText = Format::money(balance);
  const std::string payrollText = fmt::sprintf(
      LOC("DASHBOARD_PAYROLL_FOOTNOTE"), Format::money(cached_payroll).c_str());
  // Under a transfer embargo the cash tile says so instead of the payroll.
  UI::statTile(
      "tile_balance", LOC("FINANCE_CASH"), balanceText.c_str(),
      cached_embargo ? LOC("TRANSFER_TILE_EMBARGO_NOTE") : payrollText.c_str(),
      balance < 0 ? palette.negative : palette.text, width);
  tiles.next();
  const std::string squadText = std::to_string(cached_squad.size());
  const std::string averageText =
      fmt::sprintf(LOC("DASHBOARD_AVERAGE_OVR"),
                   static_cast<double>(cached_average_overall));
  UI::statTile("tile_squad", LOC("DASHBOARD_TILE_SQUAD"), squadText.c_str(),
               averageText.c_str(), palette.text, width);
  tiles.next();
  const std::string valueText = Format::money(cached_squad_value);
  UI::statTile("tile_value", LOC("DASHBOARD_TILE_VALUE"), valueText.c_str(),
               LOC("DASHBOARD_VALUE_FOOTNOTE"), palette.text, width);

  next_steps.render(guiView, ImGui::GetContentRegionAvail().x);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  if (available >= TWO_COLUMN_MIN_WIDTH * Theme::scale())
  {
    const float leftWidth = std::floor((available - gap) * 0.58f);
    const float rightWidth = available - gap - leftWidth;
    // Fill the window when the cards fit (the tables scroll inside, the page
    // does not); otherwise cards take their natural height and the page
    // scrolls. Never both at once.
    const float columnHeight = ImGui::GetContentRegionAvail().y;
    const bool fill =
        columnHeight >= DASHBOARD_FILL_MIN_HEIGHT * Theme::scale();
    const float nextHeight = NEXT_MATCH_HEIGHT * Theme::scale();
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginGroup();
    renderNextMatchCard(leftWidth, nextHeight);
    renderStandingsCard(leftWidth,
                        fill ? columnHeight - nextHeight - spacing : 0.0f);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float recentHeight =
        fill
            ? std::min(columnHeight * 0.45f,
                       ImGui::GetTextLineHeightWithSpacing() *
                               (static_cast<float>(MAX_RECENT_RESULTS) + 2.5f) +
                           2.0f * Theme::Space::M * Theme::scale())
            : 0.0f;
    renderRecentResultsCard(rightWidth, recentHeight);
    renderKeyPlayersCard(rightWidth,
                         fill ? columnHeight - recentHeight - spacing : 0.0f);
    ImGui::EndGroup();
  }
  else
  {
    // Stacked: every card sizes to its content and the page scrolls.
    renderNextMatchCard(available, NEXT_MATCH_HEIGHT * Theme::scale());
    renderRecentResultsCard(available, 0.0f);
    renderKeyPlayersCard(available, 0.0f);
    renderStandingsCard(available, 0.0f);
  }
}

void MainGameScene::renderNextMatchCard(float width, float height)
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  const TeamID clubId = controller.getManagedTeam()->get().getId();
  UI::beginCard("next_match", LOC("DASHBOARD_NEXT_FIXTURE"),
                ImVec2(width, height));
  if (!cached_next)
  {
    UI::emptyState(LOC("DASHBOARD_NO_FIXTURE"), nullptr);
    UI::endCard();
    return;
  }
  const CompetitionView::FixtureRow& next = *cached_next;
  const bool home = next.home_id == clubId;
  const TeamID opponentId = home ? next.away_id : next.home_id;

  UI::badge(LOC(CompetitionView::matchTypeKey(next.type)), palette.info);
  ImGui::SameLine();
  UI::badge(LOC(home ? "FIXTURE_HOME" : "FIXTURE_AWAY"),
            home ? palette.positive : palette.warning);
  ImGui::SameLine();
  const bool today = next.date == controller.getCurrentDate();
  const std::string when = today ? std::string(LOC("FIXTURE_TODAY")) + " " +
                                       Format::kickoff(next.kickoff)
                                 : Format::matchDay(next.date, next.kickoff);
  ImGui::TextColored(today ? palette.accent : palette.muted, "%s",
                     when.c_str());

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  {
    Theme::ScopedText heading(Theme::Text::HEADING);
    const std::string versus = LOC("FIXTURE_VS");
    const float inner = ImGui::GetContentRegionAvail().x;
    const float versusWidth = ImGui::CalcTextSize(versus.c_str()).x;
    const float side =
        (inner - versusWidth) * 0.5f - Theme::Space::M * Theme::scale();
    const float homeWidth = ImGui::CalcTextSize(next.home_name.c_str()).x;
    const float startX = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(startX + std::max(0.0f, side - homeWidth));
    ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                        ImVec2(ImGui::GetCursorScreenPos().x + side,
                               ImGui::GetCursorScreenPos().y + 100.0f),
                        true);
    ImGui::TextUnformatted(next.home_name.c_str());
    ImGui::PopClipRect();
    ImGui::SameLine(startX + side + Theme::Space::M * Theme::scale());
    ImGui::TextColored(palette.faint, "%s", versus.c_str());
    ImGui::SameLine(startX + side + versusWidth +
                    2.0f * Theme::Space::M * Theme::scale());
    ImGui::TextUnformatted(next.away_name.c_str());
  }

  // Opponent context from the cached table.
  if (const auto opponentRow = std::ranges::find_if(
          cached_table, [opponentId](const CompetitionView::StandingRow& row)
          { return row.team_id == opponentId; });
      opponentRow != cached_table.end() && opponentRow->played > 0)
  {
    const auto position = static_cast<size_t>(
        std::distance(cached_table.begin(), opponentRow) + 1);
    const std::string context =
        fmt::sprintf(LOC("DASHBOARD_OPPONENT_CONTEXT"),
                     ordinalPosition(position).c_str(), opponentRow->points);
    ImGui::TextColored(palette.muted, "%s", context.c_str());
    ImGui::SameLine();
    UI::formStrip(std::span(opponentRow->form.data(), opponentRow->form_count));
  }

  const float buttonsY =
      height - UI::buttonHeight() - ImGui::GetStyle().WindowPadding.y;
  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), buttonsY));
  if (UI::secondaryButton(LOC("NAV_LINEUP")))
    Navigation::open(guiView, NavSection::LINEUP);
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("NAV_TACTICS")))
    Navigation::open(guiView, NavSection::TACTICS);
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("DASHBOARD_SCOUT_OPPONENT")))
    Navigation::openClub(guiView, opponentId);

  // Match readiness on the same row, right-aligned: players who cannot play
  // this fixture (injured, suspended) do not count as ready starters. With
  // nobody fit to replace them the injured play through it: nothing to fix.
  const size_t ready = cached_starters - cached_unavailable_starters;
  const bool blocked = lineupBlocked();
  const bool playingInjured = !blocked && !cached_unavailable.empty();
  std::string readiness;
  if (blocked)
    readiness = fmt::sprintf(LOC("DASHBOARD_XI_UNAVAILABLE"), ready,
                             cached_unavailable.size());
  else if (playingInjured)
    readiness = fmt::sprintf(LOC("DASHBOARD_XI_PLAYING_INJURED"), ready,
                             cached_unavailable.size());
  else
    readiness = fmt::sprintf(LOC("DASHBOARD_STARTING_XI"), cached_starters);
  const char* fixLabel = LOC("DASHBOARD_AUTO_FIX");
  const float fixWidth =
      blocked ? UI::buttonWidth(fixLabel) + ImGui::GetStyle().ItemSpacing.x
              : 0.0f;
  const float readinessWidth =
      ImGui::CalcTextSize(readiness.c_str()).x + fixWidth;
  UI::sameLineIfFits(readinessWidth);
  ImGui::SetCursorPosX(
      std::max(ImGui::GetCursorPosX(),
               ImGui::GetWindowContentRegionMax().x - readinessWidth));
  ImGui::AlignTextToFramePadding();
  ImVec4 readinessColor = palette.warning;
  if (blocked)
    readinessColor = palette.negative;
  else if (cached_starters == 11 && !playingInjured)
    readinessColor = palette.positive;
  ImGui::TextColored(readinessColor, "%s", readiness.c_str());
  if (blocked)
  {
    ImGui::SameLine();
    if (UI::primaryButton(fixLabel)) autoFixLineup();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
      ImGui::SetTooltip("%s", LOC("DASHBOARD_AUTO_FIX_HELP"));
  }
  UI::endCard();
}

void MainGameScene::renderStandingsCard(float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  const TeamID clubId =
      guiView->getController().getManagedTeam()->get().getId();
  // height 0: natural height, no inner scrolling (the page scrolls).
  beginDashboardCard("league_table", LOC("DASHBOARD_LEAGUE_TABLE"), width,
                     height);
  if (ImGui::BeginTable("dashboard_table", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_SizingFixedFit |
                            (height > 0.0f ? ImGuiTableFlags_ScrollY : 0)))
  {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(LOC("MAIN_GAME_POS"));
    ImGui::TableSetupColumn(LOC("MAIN_GAME_TEAM"),
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(LOC("TABLE_COL_PLAYED"));
    ImGui::TableSetupColumn(LOC("TABLE_COL_GD"));
    ImGui::TableSetupColumn(LOC("MAIN_GAME_PTS"));
    ImGui::TableSetupColumn(LOC("TABLE_COL_FORM"));
    UI::staticHeadersRow();
    for (size_t index = 0; index < cached_table.size(); ++index)
    {
      const CompetitionView::StandingRow& row = cached_table[index];
      // Before the first round the order is alphabetical: no zones yet.
      const bool promoted = row.played > 0 && index < cached_zones.promotion;
      const bool relegated =
          row.played > 0 &&
          index + cached_zones.relegation >= cached_table.size();
      ImGui::TableNextRow();
      if (row.team_id == clubId)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               Theme::toU32(palette.accent, 0.16f));
      ImGui::TableNextColumn();
      ImVec4 positionColor = palette.muted;
      if (promoted)
        positionColor = palette.positive;
      else if (relegated)
        positionColor = palette.negative;
      ImGui::TextColored(positionColor, "%zu", index + 1);
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(row.team_id));
      if (ImGui::Selectable(row.name.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns))
        Navigation::openClub(guiView, row.team_id);
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::Text("%d", row.played);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(Format::signedInt(row.goalDifference()).c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d", row.points);
      ImGui::TableNextColumn();
      UI::formStrip(std::span(row.form.data(), row.form_count));
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void MainGameScene::renderRecentResultsCard(float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  const TeamID clubId =
      guiView->getController().getManagedTeam()->get().getId();
  beginDashboardCard("recent_results", LOC("DASHBOARD_RECENT_RESULTS"), width,
                     height);
  if (cached_recent.empty())
  {
    UI::emptyState(LOC("DASHBOARD_NO_RESULTS"), nullptr);
    UI::endCard();
    return;
  }
  if (ImGui::BeginTable("recent", 4, ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::TableSetupColumn("date");
    ImGui::TableSetupColumn("opponent", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("score");
    ImGui::TableSetupColumn("outcome");
    for (const CompetitionView::FixtureRow& fixture : cached_recent)
    {
      const bool home = fixture.home_id == clubId;
      const UI::Outcome outcome = CompetitionView::outcomeFor(fixture, clubId);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s",
                         Format::dayMonth(fixture.date).c_str());
      ImGui::TableNextColumn();
      ImGui::TextColored(
          palette.faint, "%s",
          LOC(home ? "FIXTURE_HOME_SHORT" : "FIXTURE_AWAY_SHORT"));
      ImGui::SameLine();
      ImGui::PushID(&fixture);
      if (ImGui::Selectable(
              home ? fixture.away_name.c_str() : fixture.home_name.c_str(),
              false, ImGuiSelectableFlags_SpanAllColumns))
        Navigation::openMatchReport(guiView, fixture.date, fixture.home_id,
                                    fixture.away_id);
      ImGui::PopID();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", LOC("FIXTURES_OPEN_REPORT_HINT"));
      ImGui::TableNextColumn();
      ImGui::Text("%u – %u", fixture.home_score, fixture.away_score);
      ImGui::TableNextColumn();
      UI::formStrip(std::span(&outcome, 1));
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void MainGameScene::renderKeyPlayersCard(float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  beginDashboardCard("key_players", LOC("DASHBOARD_KEY_PLAYERS"), width,
                     height);
  if (ImGui::BeginTable("key_players_table", 3,
                        ImGuiTableFlags_SizingFixedFit |
                            (height > 0.0f ? ImGuiTableFlags_ScrollY : 0)))
  {
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("role");
    ImGui::TableSetupColumn("ovr");
    for (size_t index = 0;
         index < MAX_KEY_PLAYERS && index < cached_squad.size(); ++index)
    {
      const PlayerView::PlayerRow& player = cached_squad[index];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(player.id));
      if (ImGui::Selectable(player.name.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns))
        Navigation::openPlayer(guiView, player.id);
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s", player.role.c_str());
      ImGui::TableNextColumn();
      UI::ratingChip(player.overall);
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void MainGameScene::renderFinances()
{
  GameController& controller = guiView->getController();
  const Team& club = controller.getManagedTeam()->get();
  const Finances& finances = club.getFinances();
  const Theme::Palette& palette = Theme::palette();

  UI::pageHeader(LOC("FINANCE_TITLE"), LOC("FINANCE_SUBTITLE"));

  UI::TileRow tiles(4);
  const float width = tiles.width();
  tiles.next();
  const std::string cash = Format::money(finances.getBalance());
  const std::string cashFull = Format::moneyFull(finances.getBalance());
  UI::statTile("fin_cash", LOC("FINANCE_CASH"), cash.c_str(), cashFull.c_str(),
               finances.getBalance() < 0 ? palette.negative : palette.text,
               width);
  tiles.next();
  // What can be committed today (cash reserve, instalments and an embargo
  // included), the same figure the transfer screens use.
  const std::string budget = Format::money(cached_transfer_budget);
  UI::statTile("fin_budget", LOC("FINANCE_TRANSFER_BUDGET"), budget.c_str(),
               LOC(cached_embargo ? "TRANSFER_TILE_EMBARGO_NOTE"
                                  : "FINANCE_BUDGET_FOOTNOTE"),
               cached_embargo ? palette.negative : palette.text, width);
  tiles.next();
  const int64_t wageBudget = finances.getWageBudget();
  const std::string payroll = Format::money(cached_payroll);
  const std::string payrollNote =
      wageBudget > 0
          ? fmt::sprintf(LOC("FINANCE_WAGE_BUDGET_NOTE"),
                         Format::money(wageBudget).c_str(),
                         100.0 * static_cast<double>(cached_payroll) /
                             static_cast<double>(wageBudget))
          : std::string(LOC("FINANCE_ANNUAL_FOOTNOTE"));
  UI::statTile("fin_payroll", LOC("FINANCE_PAYROLL"), payroll.c_str(),
               payrollNote.c_str(),
               wageBudget > 0 && cached_payroll > wageBudget ? palette.negative
                                                             : palette.text,
               width);
  tiles.next();
  // The opening balance is a carried-over figure, not this season's result.
  const int64_t seasonNet =
      cached_season_summary.net() -
      cached_season_summary.by_category[static_cast<size_t>(
          std::to_underlying(FinanceCategory::OpeningBalance))];
  const std::string net = Format::money(seasonNet);
  UI::statTile("fin_net", LOC("FINANCE_SEASON_NET"), net.c_str(),
               LOC("FINANCE_SEASON_NET_NOTE"),
               seasonNet < 0 ? palette.negative : palette.positive, width);

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float leftWidth =
      twoColumns ? std::floor((available - gap) * 0.45f) : available;
  const float rightWidth = twoColumns ? available - gap - leftWidth : available;
  // Side by side and tall enough: fill the window, the ledger scrolls inside.
  // Otherwise both cards take their natural height and the page scrolls.
  const bool fill = twoColumns && ImGui::GetContentRegionAvail().y >=
                                      FINANCE_FILL_MIN_HEIGHT * Theme::scale();
  const float cardHeight = fill ? ImGui::GetContentRegionAvail().y : 0.0f;

  // Income and expenses by category, this season or the last 30 days.
  beginDashboardCard("fin_breakdown", nullptr, leftWidth, cardHeight);
  UI::sectionLabel(LOC(finance_show_month ? "FINANCE_BREAKDOWN_MONTH"
                                          : "FINANCE_BREAKDOWN"));
  ImGui::SameLine();
  const char* periodToggle =
      LOC(finance_show_month ? "FINANCE_SHOW_SEASON" : "FINANCE_SHOW_MONTH");
  ImGui::SetCursorPosX(
      std::max(ImGui::GetCursorPosX(),
               ImGui::GetWindowContentRegionMax().x -
                   UI::buttonWidth(periodToggle, UI::ButtonSize::COMPACT)));
  if (UI::secondaryButton(periodToggle, ImVec2(0.0f, 0.0f),
                          UI::ButtonSize::COMPACT))
    finance_show_month = !finance_show_month;
  if (const std::vector<FinanceBar>& periodBars =
          finance_show_month ? cached_month_bars : cached_finance_bars;
      periodBars.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("FINANCE_NO_TRANSACTIONS"));
  }
  else
  {
    std::vector<UI::BarDatum> bars;
    bars.reserve(periodBars.size());
    for (const FinanceBar& bar : periodBars)
      bars.emplace_back(LOC(bar.label_key),
                        static_cast<float>(std::llabs(bar.amount)),
                        bar.amount >= 0 ? palette.positive : palette.negative,
                        bar.amount_text);
    UI::barChart("fin_bars", bars, ImGui::GetContentRegionAvail().x,
                 130.0f * Theme::scale());
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  UI::sectionLabel(LOC("FINANCE_BUDGET_ADJUST"));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("FINANCE_BUDGET_ADJUST_HELP"));
  ImGui::PopTextWrapPos();
  const float step = 100'000.0f;
  ImGui::SetNextItemWidth(std::min(260.0f * Theme::scale(),
                                   ImGui::GetContentRegionAvail().x * 0.6f));
  const std::string adjustLabel =
      Format::money(static_cast<int64_t>(budget_shift));
  ImGui::SliderFloat("##budget_shift", &budget_shift,
                     -static_cast<float>(std::min<int64_t>(
                         wageBudget * WEEKS_PER_YEAR, 50'000'000)),
                     static_cast<float>(finances.getTransferBudget()),
                     adjustLabel.c_str());
  budget_shift = std::round(budget_shift / step) * step;
  ImGui::SameLine();
  ImGui::BeginDisabled(budget_shift == 0.0f);
  if (UI::primaryButton(LOC("FINANCE_BUDGET_APPLY")))
  {
    if (controller.moveTransferToWageBudget(static_cast<int64_t>(budget_shift)))
    {
      showToast(LOC("FINANCE_BUDGET_TOAST"));
      budget_shift = 0.0f;
      refreshData();
    }
    else
    {
      showToast(LOC("FINANCE_BUDGET_REFUSED"), true);
    }
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", LOC("FINANCE_BUDGET_SLIDER_HELP"));
  UI::endCard();
  if (twoColumns) ImGui::SameLine();

  // Balance trend and the ledger itself.
  beginDashboardCard("fin_ledger", LOC("FINANCE_LEDGER"), rightWidth,
                     cardHeight);
  if (cached_balance_trend.size() >= 2)
  {
    UI::sparkline(
        "fin_trend", cached_balance_trend,
        ImVec2(ImGui::GetContentRegionAvail().x, 46.0f * Theme::scale()),
        palette.info);
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  }
  if (cached_ledger.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("FINANCE_NO_TRANSACTIONS"));
  }
  else if (ImGui::BeginTable(
               "ledger", 3,
               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                   ImGuiTableFlags_SizingFixedFit |
                   (cardHeight > 0.0f ? ImGuiTableFlags_ScrollY : 0)))
  {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(LOC("FIXTURES_COL_DATE"));
    ImGui::TableSetupColumn(LOC("FINANCE_COL_CATEGORY"),
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(LOC("FINANCE_COL_AMOUNT"));
    UI::staticHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(cached_ledger.size()));
    while (clipper.Step())
    {
      for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
      {
        const LedgerRow& entry = cached_ledger[static_cast<size_t>(line)];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(palette.muted, "%s", entry.date.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(LOC(entry.category_key));
        ImGui::TableNextColumn();
        UI::textRightColored(
            entry.amount >= 0 ? palette.positive : palette.negative,
            entry.amount_text.c_str());
      }
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void MainGameScene::refreshData()
{
  GameController& controller = guiView->getController();
  cached_date = controller.getCurrentDate();
  cached_season = controller.getCurrentSeason();
  const auto managed = controller.getManagedTeam();
  cached_club = managed ? managed->get().getId() : TeamID{0};
  if (!managed)
  {
    cached_next.reset();
    cached_squad.clear();
    return;
  }
  const Team& club = managed->get();
  next_steps.refresh(controller);

  cached_table =
      CompetitionView::buildStandings(controller, club.getLeagueId());
  cached_zones = CompetitionView::zonesFor(controller, club.getLeagueId());
  cached_next = CompetitionView::nextFixture(controller, club.getId());
  // Same rule as the pre-match gate: anyone in the matchday squad who is
  // injured or suspended for the next fixture's competition.
  const MatchType nextType =
      cached_next ? cached_next->type : MatchType::LEAGUE;
  const auto ineligible =
      controller.getIneligibleSelections(club.getId(), nextType);
  cached_unavailable = {ineligible.begin(), ineligible.end()};
  const Lineup& lineup = club.getLineup();
  cached_starters = lineup.getOutfieldPlayers().size() +
                    (lineup.getGoalkeeper() != nullptr ? 1U : 0U);
  cached_unavailable_starters =
      Formation::unavailableStarters(lineup, cached_unavailable);
  cached_can_kick_off = cached_unavailable.empty() ||
                        controller.canKickOff(club.getId(), nextType);

  cached_recent.clear();
  const auto fixtures =
      CompetitionView::buildClubFixtures(controller, club.getId());
  for (auto fixture = fixtures.rbegin();
       fixture != fixtures.rend() && cached_recent.size() < MAX_RECENT_RESULTS;
       ++fixture)
  {
    if (fixture->played) cached_recent.push_back(*fixture);
  }

  cached_squad.clear();
  cached_payroll = 0;
  cached_squad_value = 0;
  cached_embargo = controller.isTransferEmbargoed();
  cached_transfer_budget = controller.transferBudgetForTeam(club.getId());
  double overallSum = 0.0;
  for (const auto& playerRef : controller.getPlayersForTeam(club.getId()))
  {
    PlayerView::PlayerRow row =
        PlayerView::makeRow(controller, playerRef.get());
    cached_payroll += row.wage;
    cached_squad_value += row.market_value;
    overallSum += static_cast<double>(row.overall);
    cached_squad.push_back(std::move(row));
  }
  cached_average_overall =
      cached_squad.empty()
          ? 0.0f
          : static_cast<float>(overallSum /
                               static_cast<double>(cached_squad.size()));
  std::ranges::sort(cached_squad, [](const PlayerView::PlayerRow& left,
                                     const PlayerView::PlayerRow& right)
                    { return left.overall > right.overall; });

  // Finance view models: season summary, ledger rows and balance trend.
  const GameDateValue today = controller.getCurrentDate();
  const GameDateValue seasonStart(
      static_cast<uint16_t>(today.month >= SEASON_START_MONTH ? today.year
                                                              : today.year - 1),
      SEASON_START_MONTH, 1);
  cached_season_summary =
      controller.getFinanceSummary(club.getId(), seasonStart, today);
  const auto buildBars = [](const FinanceSummary& summary)
  {
    std::vector<FinanceBar> bars;
    for (size_t index = 0; index < FINANCE_CATEGORY_COUNT; ++index)
    {
      const auto category = static_cast<FinanceCategory>(index);
      const int64_t amount = summary.by_category[index];
      if (amount == 0 || category == FinanceCategory::OpeningBalance) continue;
      bars.emplace_back(financeCategoryKey(category), amount,
                        Format::money(amount));
    }
    std::ranges::sort(bars, [](const auto& left, const auto& right)
                      { return left.amount > right.amount; });
    return bars;
  };
  cached_finance_bars = buildBars(cached_season_summary);
  cached_month_bars = buildBars(controller.getFinanceSummary(
      club.getId(), today - FINANCE_MONTH_DAYS, today));
  const auto& ledger = controller.getFinanceLedger(club.getId());
  cached_ledger.clear();
  cached_ledger.reserve(ledger.size());
  for (auto entry = ledger.rbegin(); entry != ledger.rend(); ++entry)
    cached_ledger.emplace_back(Format::date(entry->date),
                               financeCategoryKey(entry->category),
                               entry->amount, Format::money(entry->amount));
  cached_balance_trend.clear();
  int64_t running = 0;
  const size_t trendStart =
      ledger.size() > MAX_TREND_POINTS ? ledger.size() - MAX_TREND_POINTS : 0;
  for (size_t index = 0; index < ledger.size(); ++index)
  {
    running += ledger[index].amount;
    if (index >= trendStart)
      cached_balance_trend.push_back(static_cast<float>(running));
  }
}

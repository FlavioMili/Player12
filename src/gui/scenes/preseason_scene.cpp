// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/preseason_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 980.0f;
constexpr std::array<std::uint32_t, 3> SEAT_OPTIONS = {2'000, 5'000, 10'000};
constexpr std::array<const char*, 3> CAMP_KEYS = {
    "PLANNING_CAMP_NONE", "PLANNING_CAMP_DOMESTIC", "PLANNING_CAMP_ABROAD"};

std::string teamName(const GameController& controller, TeamID id)
{
  const auto team = controller.getTeamById(id);
  return team ? team->get().getName() : std::string("–");
}

int months(std::uint16_t days) { return std::max(1, (days + 15) / 30); }
}  // namespace

PreseasonScene::PreseasonScene(GUIView* parent) : ManagementScene(parent) {}

void PreseasonScene::refresh()
{
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  friendlies.clear();
  projects.clear();
  completed_projects.clear();
  mentoring_lines.clear();
  if (!managed) return;
  const Team& club = managed->get();

  const auto suggestion = controller.getPreseasonSuggestion();
  for (const FriendlySlot& slot : controller.getPreseasonFriendlies())
  {
    FriendlyLine line;
    line.slot = slot;
    line.date = Format::dayMonth(slot.date);
    line.opponent = teamName(controller, slot.opponent_id);
    if (const auto pick =
            std::ranges::find(suggestion, slot.date, &FriendlySuggestion::date);
        slot.editable && pick != suggestion.end() &&
        pick->opponent_id != slot.opponent_id)
      line.suggestion =
          fmt::sprintf(LOC("PLANNING_ASSISTANT_PICK"),
                       teamName(controller, pick->opponent_id).c_str());
    friendlies.push_back(std::move(line));
  }
  if (selected_friendly >= static_cast<int>(friendlies.size()) ||
      (selected_friendly >= 0 &&
       !friendlies[static_cast<size_t>(selected_friendly)].slot.editable))
    selected_friendly = -1;

  const PreseasonState& state = controller.getPreseasonState();
  camp_choice = static_cast<int>(std::to_underlying(state.camp));
  for (size_t index = 0; index < camp_quotes.size(); ++index)
    camp_quotes[index] =
        controller.getCampQuote(static_cast<TrainingCamp>(index));
  camp_open = camp_quotes[1].available &&
              controller.getCurrentDate() < camp_quotes[1].start &&
              !state.camp_applied;
  camp_status.clear();
  if (state.camp != TrainingCamp::None)
  {
    const CampQuote& quote =
        camp_quotes[static_cast<size_t>(std::to_underlying(state.camp))];
    camp_status =
        state.camp_applied
            ? std::string(LOC("PLANNING_CAMP_DONE"))
            : fmt::sprintf(LOC("PLANNING_CAMP_BOOKED"),
                           LOC(CAMP_KEYS[static_cast<size_t>(state.camp)]),
                           Format::dayMonth(quote.start).c_str(),
                           Format::dayMonth(quote.end).c_str());
  }
  camp_suggestion = fmt::sprintf(
      LOC("PLANNING_CAMP_SUGGESTION"),
      LOC(CAMP_KEYS[static_cast<size_t>(controller.getSuggestedCamp())]));

  for (size_t index = 0; index < seat_labels.size(); ++index)
    seat_labels[index] = Format::thousands(SEAT_OPTIONS[index]);
  const std::vector<FacilityProject> all = controller.getFacilityProjects();
  const ClubProfile& profile = club.getProfile();
  using enum FacilityProjectType;
  for (const FacilityProjectType type :
       {TrainingGround, MedicalCentre, StadiumExpansion})
  {
    ProjectLine line;
    line.type = type;
    line.name = LOC(BoardModel::projectTypeKey(type));
    const bool stadium = type == StadiumExpansion;
    const ProjectQuote quote = controller.getProjectQuote(
        type, stadium ? SEAT_OPTIONS[static_cast<size_t>(seats_choice)] : 0);
    switch (type)
    {
      case TrainingGround:
        line.level =
            fmt::sprintf(LOC("PLANNING_LEVEL"), profile.training_facilities);
        break;
      case MedicalCentre:
        line.level = fmt::sprintf(LOC("PLANNING_LEVEL"),
                                  controller.getMedicalLevel(club.getId()));
        break;
      case StadiumExpansion:
      case COUNT:
        line.level =
            fmt::sprintf(LOC("PLANNING_SEATS"),
                         Format::thousands(profile.stadium_capacity).c_str());
        break;
    }
    if (quote.amount > 0)
      line.quote =
          stadium ? fmt::sprintf(LOC("PLANNING_QUOTE_STADIUM"),
                                 Format::thousands(quote.amount).c_str(),
                                 Format::money(quote.cost).c_str(),
                                 months(quote.days),
                                 Format::thousands(quote.disruption).c_str())
                  : fmt::sprintf(LOC("PLANNING_QUOTE_LEVELS"), quote.amount,
                                 Format::money(quote.cost).c_str(),
                                 months(quote.days));
    for (const FacilityProject& project : all)
    {
      if (project.completed || project.type != type) continue;
      line.running = project;
      line.status = fmt::sprintf(LOC("PLANNING_PROJECT_RUNNING"),
                                 Format::date(project.end).c_str(),
                                 Format::money(project.paid).c_str(),
                                 Format::money(project.cost).c_str());
    }
    if (!line.running)
    {
      if (const auto cooldown = controller.getProjectCooldown(type))
        line.status = fmt::sprintf(LOC("PLANNING_PROJECT_COOLDOWN"),
                                   Format::date(*cooldown).c_str());
      else if (quote.amount == 0)
        line.status = LOC("PROJECT_VERDICT_MAXIMUM");
      line.can_request =
          quote.amount > 0 && !controller.getProjectCooldown(type);
    }
    projects.push_back(std::move(line));
  }
  for (const FacilityProject& project : all)
    if (project.completed)
      completed_projects.push_back(
          fmt::sprintf(LOC("PLANNING_PROJECT_DONE"),
                       LOC(BoardModel::projectTypeKey(project.type)),
                       Format::date(project.end).c_str(),
                       Format::money(project.cost).c_str()));

  const auto gamedata = controller.getGameData();
  const auto nameOf = [&](PlayerID id)
  {
    const auto player = gamedata->getPlayer(id);
    return player ? player->get().getName() : std::string("–");
  };
  for (const MentoringGroup& group : controller.getMentoringGroups())
  {
    std::string mentees;
    for (const MenteeState& mentee : group.mentees)
    {
      if (!mentees.empty()) mentees += ", ";
      mentees += nameOf(mentee.player_id);
    }
    if (mentees.empty()) mentees = LOC("PLANNING_MENTORING_NOBODY");
    mentoring_lines.push_back(fmt::sprintf(LOC("PLANNING_MENTORING_LINE"),
                                           nameOf(group.mentor_id).c_str(),
                                           mentees.c_str()));
  }
}

void PreseasonScene::loadOpponents()
{
  opponents.clear();
  opponent_labels.clear();
  opponent_index = -1;
  tour_fee_for = -1;
  if (selected_friendly < 0) return;
  const FriendlyLine& line = friendlies[static_cast<size_t>(selected_friendly)];
  opponents = guiView->getController().getFriendlyOpponents(
      line.slot.date, static_cast<OpponentLevel>(level), region == 1);
  for (size_t index = 0; index < opponents.size(); ++index)
  {
    opponent_labels.push_back(fmt::sprintf(
        LOC("PLANNING_OPPONENT_OPTION"),
        teamName(guiView->getController(), opponents[index].team_id).c_str(),
        opponents[index].reputation));
    if (opponents[index].team_id == line.slot.opponent_id)
      opponent_index = static_cast<int>(index);
  }
  if (opponent_index < 0 && !opponents.empty()) opponent_index = 0;
}

void PreseasonScene::renderContent()
{
  UI::pageHeader(LOC("PLANNING_TITLE"), LOC("PLANNING_SUBTITLE"));
  if (!guiView->getController().getManagedTeam()) return;
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.55f) : available;
  const float right = twoColumns ? available - gap - left : available;
  if (twoColumns)
  {
    ImGui::BeginGroup();
    renderPreseason(left);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    renderProjects(right);
    renderMentoring(right);
    ImGui::EndGroup();
  }
  else
  {
    renderPreseason(available);
    renderProjects(available);
    renderMentoring(available);
  }
  if (mentoring_dialog.render(guiView->getController())) refresh();
}

void PreseasonScene::renderPreseason(float width)
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("planning_preseason", LOC("PLANNING_PRESEASON"),
                          width);
  if (friendlies.empty())
  {
    UI::emptyState(LOC("PLANNING_PRESEASON_EMPTY_TITLE"),
                   LOC("PLANNING_PRESEASON_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  static const std::array<UI::Column, 4> COLUMNS = {{
      {"PLANNING_COL_DATE", 70.0f, 0},
      {"PLANNING_COL_OPPONENT", 0.0f, 0},
      {"PLANNING_COL_VENUE", 80.0f, 1},
      {"PLANNING_COL_NOTE", 190.0f, 2},
  }};
  std::array<UI::Column, 4> columns = COLUMNS;
  for (UI::Column& column : columns) column.label = LOC(column.label);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 150.0f);
  bool any_editable = false;
  if (UI::beginResponsiveTable(
          "friendlies", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (size_t index = 0; index < friendlies.size(); ++index)
    {
      const FriendlyLine& line = friendlies[index];
      any_editable = any_editable || line.slot.editable;
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s", line.date.c_str());
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(!line.slot.editable);
      if (ImGui::Selectable(line.opponent.c_str(),
                            selected_friendly == static_cast<int>(index),
                            ImGuiSelectableFlags_SpanAllColumns))
      {
        selected_friendly = selected_friendly == static_cast<int>(index)
                                ? -1
                                : static_cast<int>(index);
        venue = line.slot.home ? 0 : 1;
        tour = line.slot.tour;
        loadOpponents();
      }
      ImGui::EndDisabled();
      if (UI::cell(mask, 2))
        ImGui::TextUnformatted(
            LOC(line.slot.home ? "PLANNING_HOME" : "PLANNING_AWAY"));
      if (UI::cell(mask, 3))
      {
        const std::string note = line.slot.tour
                                     ? std::string(LOC("PLANNING_TOUR"))
                                     : line.suggestion;
        UI::textFitted(note, ImGui::GetContentRegionAvail().x,
                       line.slot.tour ? palette.text : palette.faint);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (selected_friendly >= 0)
    renderFriendlyEditor(friendlies[static_cast<size_t>(selected_friendly)]);
  else if (any_editable)
    ImGui::TextColored(palette.faint, "%s", LOC("PLANNING_PICK_HINT"));
  if (any_editable)
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    if (UI::secondaryButton(LOC("PLANNING_USE_ASSISTANT")))
    {
      const size_t applied = controller.applyPreseasonSuggestion();
      showToast(fmt::sprintf(LOC("PLANNING_ASSISTANT_TOAST"), applied));
      selected_friendly = -1;
      refresh();
    }
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  renderCamp();
  UI::endCard();
}

void PreseasonScene::renderFriendlyEditor(const FriendlyLine& line)
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  UI::sectionLabel(
      fmt::sprintf(LOC("PLANNING_EDIT_FRIENDLY"), line.date.c_str()).c_str());
  const float full = ImGui::GetContentRegionAvail().x;
  if (const std::array<const char*, 3> levels =
          {LOC("PLANNING_LEVEL_WEAKER"), LOC("PLANNING_LEVEL_SIMILAR"),
           LOC("PLANNING_LEVEL_STRONGER")};
      UI::segmented("##level", level, levels,
                    std::min(full, 420.0f * Theme::scale())))
    loadOpponents();
  if (const std::array<const char*, 2> regions = {LOC("PLANNING_DOMESTIC"),
                                                  LOC("PLANNING_ABROAD")};
      UI::segmented("##region", region, regions,
                    std::min(full, 280.0f * Theme::scale())))
    loadOpponents();
  if (opponents.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("PLANNING_NO_OPPONENTS"));
    return;
  }
  ImGui::SetNextItemWidth(std::min(full, 360.0f * Theme::scale()));
  if (const char* preview =
          opponent_index >= 0
              ? opponent_labels[static_cast<size_t>(opponent_index)].c_str()
              : "";
      ImGui::BeginCombo("##opponent", preview, ImGuiComboFlags_HeightLarge))
  {
    for (size_t index = 0; index < opponent_labels.size(); ++index)
      if (ImGui::Selectable(opponent_labels[index].c_str(),
                            static_cast<int>(index) == opponent_index))
        opponent_index = static_cast<int>(index);
    ImGui::EndCombo();
  }
  const std::array<const char*, 2> venues = {LOC("PLANNING_HOME"),
                                             LOC("PLANNING_AWAY")};
  UI::segmented("##venue", venue, venues,
                std::min(full, 240.0f * Theme::scale()));
  const OpponentOption& option = opponents[static_cast<size_t>(opponent_index)];
  const bool tour_possible = venue == 1 && option.abroad;
  if (!tour_possible) tour = false;
  ImGui::BeginDisabled(!tour_possible);
  ImGui::Checkbox(LOC("PLANNING_BOOK_TOUR"), &tour);
  ImGui::EndDisabled();
  if (tour_possible)
  {
    if (tour_fee_for != opponent_index)
    {
      tour_fee_for = opponent_index;
      tour_fee_text = fmt::sprintf(
          LOC("PLANNING_TOUR_FEE"),
          Format::money(controller.getTourFee(option.team_id)).c_str());
    }
    ImGui::SameLine();
    ImGui::TextColored(palette.muted, "%s", tour_fee_text.c_str());
  }
  else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    ImGui::SetTooltip("%s", LOC("PLANNING_TOUR_HINT"));
  }
  if (UI::primaryButton(LOC("PLANNING_CONFIRM_FRIENDLY")))
  {
    const bool done = controller.setPreseasonFriendly(
        line.slot.date, option.team_id, venue == 0, tour);
    showToast(LOC(done ? "PLANNING_FRIENDLY_SET" : "PLANNING_FRIENDLY_FAILED"),
              !done);
    selected_friendly = -1;
    refresh();
  }
}

void PreseasonScene::renderCamp()
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  UI::sectionLabel(LOC("PLANNING_CAMP"));
  const PreseasonState& state = controller.getPreseasonState();
  const bool open = camp_open;
  if (open)
  {
    const std::array<const char*, 3> camps = {
        LOC(CAMP_KEYS[0]), LOC(CAMP_KEYS[1]), LOC(CAMP_KEYS[2])};
    UI::segmented(
        "##camp", camp_choice, camps,
        std::min(ImGui::GetContentRegionAvail().x, 420.0f * Theme::scale()));
  }
  const auto choice = static_cast<TrainingCamp>(camp_choice);
  const CampQuote& quote = camp_quotes[static_cast<size_t>(camp_choice)];
  ImGui::PushTextWrapPos(0.0f);
  if (open && quote.available)
    ImGui::TextColored(palette.muted, "%s",
                       fmt::sprintf(LOC("PLANNING_CAMP_QUOTE"),
                                    Format::money(quote.cost).c_str(),
                                    static_cast<int>(quote.sharpness),
                                    static_cast<int>(quote.familiarity),
                                    Format::dayMonth(quote.start).c_str(),
                                    Format::dayMonth(quote.end).c_str())
                           .c_str());
  if (!camp_status.empty()) ImGui::TextUnformatted(camp_status.c_str());
  if (open) ImGui::TextColored(palette.faint, "%s", camp_suggestion.c_str());
  ImGui::PopTextWrapPos();
  if (!open)
  {
    if (camp_status.empty())
      ImGui::TextColored(palette.faint, "%s", LOC("PLANNING_CAMP_CLOSED"));
    return;
  }
  ImGui::BeginDisabled(choice == state.camp);
  if (const bool cancelling =
          choice == TrainingCamp::None && state.camp != TrainingCamp::None;
      UI::primaryButton(
          LOC(cancelling ? "PLANNING_CAMP_CANCEL" : "PLANNING_CAMP_BOOK")))
  {
    const bool booked = controller.bookTrainingCamp(choice);
    showToast(LOC(booked ? "PLANNING_CAMP_TOAST" : "PLANNING_CAMP_FAILED"),
              !booked);
    refresh();
  }
  ImGui::EndDisabled();
}

void PreseasonScene::renderProjects(float width)
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("planning_projects", LOC("PLANNING_PROJECTS"), width);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("PLANNING_PROJECTS_EXPLAIN"));
  ImGui::PopTextWrapPos();
  // refresh() rebuilds `projects`: it runs after the loop.
  bool dirty = false;
  for (const ProjectLine& line : projects)
  {
    ImGui::PushID(static_cast<int>(std::to_underlying(line.type)));
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    const float full = ImGui::GetContentRegionAvail().x;
    {
      Theme::ScopedText title(Theme::Text::TITLE);
      UI::textFitted(line.name, full * 0.6f, palette.text);
    }
    ImGui::SameLine();
    UI::textRightColored(palette.muted, line.level.c_str());
    if (line.running)
    {
      const float progress =
          line.running->progress(controller.getCurrentDate());
      ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 6.0f * Theme::scale()), "");
      UI::textFitted(line.status, full, palette.muted);
    }
    else
    {
      if (line.type == FacilityProjectType::StadiumExpansion)
      {
        const std::array<const char*, 3> seats = {seat_labels[0].c_str(),
                                                  seat_labels[1].c_str(),
                                                  seat_labels[2].c_str()};
        if (UI::segmented("##seats", seats_choice, seats,
                          std::min(full, 300.0f * Theme::scale())))
          dirty = true;
      }
      if (!line.quote.empty()) UI::textFitted(line.quote, full, palette.text);
      if (!line.status.empty())
        UI::textFitted(line.status, full, palette.faint);
      ImGui::BeginDisabled(!line.can_request);
      if (UI::secondaryButton(LOC("PLANNING_ASK_BOARD"), ImVec2(0, 0),
                              UI::ButtonSize::COMPACT))
      {
        const ProjectVerdict verdict = controller.requestFacilityProject(
            line.type, line.type == FacilityProjectType::StadiumExpansion
                           ? SEAT_OPTIONS[static_cast<size_t>(seats_choice)]
                           : 0);
        project_message = LOC(BoardModel::projectVerdictKey(verdict));
        project_message_error = verdict != ProjectVerdict::Approved;
        ImGui::EndDisabled();
        ImGui::PopID();
        dirty = true;
        break;
      }
      ImGui::EndDisabled();
    }
    ImGui::PopID();
  }
  if (dirty) refresh();
  if (!project_message.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(
        project_message_error ? palette.warning : palette.positive, "%s",
        project_message.c_str());
    ImGui::PopTextWrapPos();
  }
  if (!completed_projects.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
    UI::sectionLabel(LOC("PLANNING_COMPLETED"));
    for (const std::string& line : completed_projects)
      UI::textFitted(line, ImGui::GetContentRegionAvail().x, palette.muted);
  }
  UI::endCard();
}

void PreseasonScene::renderMentoring(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("planning_mentoring", LOC("PLANNING_MENTORING"),
                          width);
  if (mentoring_lines.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.muted, "%s", LOC("PLANNING_MENTORING_EMPTY"));
    ImGui::PopTextWrapPos();
  }
  for (const std::string& line : mentoring_lines)
    UI::textFitted(line, ImGui::GetContentRegionAvail().x, palette.text);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (UI::secondaryButton(LOC("PLANNING_MENTORING_MANAGE")))
    mentoring_dialog.open(guiView->getController());
  UI::endCard();
}

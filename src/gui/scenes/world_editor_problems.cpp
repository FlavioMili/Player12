// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <imgui.h>

#include <algorithm>
#include <array>
#include <iterator>

#include "database/world/world_schema.h"
#include "global/language_manager.h"
#include "gui/scenes/world_editor_state.h"
#include "gui/widgets/theme.h"

using json = nlohmann::json;
using World::Kind;
using State = WorldEditorScene::State;
using Page = State::Page;
using namespace WorldEditorUi;

namespace
{
constexpr float ISSUE_DELAY = 0.4F;
}  // namespace

void State::showIssue(const World::Issue& issue)
{
  if (!issue.kind)
  {
    return;
  }
  setPage(pageOf(*issue.kind));
  const std::int64_t key = *issue.kind == Kind::Travel ? issue.index : issue.id;
  if (entity(*issue.kind, key) != nullptr)
  {
    select(*issue.kind, key);
  }
}

const std::vector<World::Issue>& State::problems(bool force)
{
  if (issues_stale && (force || since_edit >= ISSUE_DELAY))
  {
    issues = World::validate(doc);
    auto blocking = World::playability(doc);
    issues.insert(issues.end(), std::make_move_iterator(blocking.begin()),
                  std::make_move_iterator(blocking.end()));
    std::ranges::stable_sort(issues, {}, &World::Issue::severity);
    issues_stale = false;
  }
  return issues;
}

namespace
{
std::string issueText(const World::Issue& issue)
{
  static const std::array<const char*, 26> CODES = {
      "NOT_JSON",
      "TOO_LARGE",
      "CANNOT_READ",
      "NO_MANIFEST",
      "NEWER_SCHEMA",
      "NOT_A_LIST",
      "NOT_AN_OBJECT",
      "BAD_ID",
      "DUPLICATE_ID",
      "MISSING",
      "WRONG_TYPE",
      "OUT_OF_RANGE",
      "BAD_CHOICE",
      "BAD_COLOUR",
      "BAD_REFERENCE",
      "DUPLICATE_NAME",
      "DUPLICATE_SHORT_NAME",
      "PARENT_CYCLE",
      "NO_PLAYABLE_LEAGUE",
      "ODD_CLUB_COUNT",
      "TOO_FEW_CLUBS",
      "NO_TOP_DIVISION",
      "SQUAD_TOO_LARGE",
      "UNKNOWN_NATIONALITY",
      "NO_COUNTRY",
      "UNASSIGNED",
  };
  std::string key = std::string("WORLD_ISSUE_") +
                    CODES.at(static_cast<std::size_t>(issue.code));
  if (issue.code == World::IssueCode::OutOfRange &&
      issue.kind == Kind::Travel && issue.detail.empty())
  {
    key = "WORLD_ISSUE_TRAVEL_LOOP";
  }
  std::string where;
  if (issue.kind)
  {
    where = kindName(*issue.kind);
  }
  if (!issue.name.empty())
  {
    where += (where.empty() ? "" : ": ") + issue.name;
  }
  const std::string text =
      format(key.c_str(), {fieldLabel(issue.kind, issue.field), issue.detail});
  return where.empty() ? text : where + " - " + text;
}
}  // namespace

void State::renderProblems(float width, float height)
{
  UI::beginCard("##world_problems", LOC("WORLD_NAV_PROBLEMS"),
                ImVec2(width, height), true);
  if (issues.empty())
  {
    UI::emptyState(LOC("WORLD_NO_PROBLEMS_TITLE"),
                   LOC("WORLD_NO_PROBLEMS_HINT"));
    UI::endCard();
    return;
  }
  muted(LOC("WORLD_PROBLEMS_HINT"));
  const std::array<World::Severity, 3> order = {World::Severity::Error,
                                                World::Severity::Blocking,
                                                World::Severity::Warning};
  const std::array<const char*, 3> keys = {
      "WORLD_GROUP_ERRORS", "WORLD_GROUP_BLOCKING", "WORLD_GROUP_WARNINGS"};
  const std::array<ImVec4, 3> colours = {Theme::palette().negative,
                                         Theme::palette().warning,
                                         Theme::palette().muted};
  for (std::size_t group = 0; group < order.size(); ++group)
  {
    const auto range = std::ranges::equal_range(issues, order[group], {},
                                                &World::Issue::severity);
    if (range.empty())
    {
      continue;
    }
    const std::string header =
        format(keys[group], {std::to_string(range.size())}) + "###group" +
        std::to_string(group);
    ImGui::PushStyleColor(ImGuiCol_Text, colours[group]);
    const bool open =
        ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open)
    {
      continue;
    }
    const auto begin = static_cast<std::size_t>(range.begin() - issues.begin());
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(range.size()));
    while (clipper.Step())
    {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
      {
        const World::Issue& issue = issues[begin + static_cast<std::size_t>(i)];
        ImGui::PushID(static_cast<int>(begin) + i);
        if (ImGui::Selectable(issueText(issue).c_str(), false) && issue.kind)
        {
          const World::Issue target = issue;
          showIssue(target);
          ImGui::PopID();
          UI::endCard();
          return;
        }
        ImGui::PopID();
      }
    }
  }
  UI::endCard();
}

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_bench_scene.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <sstream>
#include <string_view>

#include "controller/game_controller.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_contact_view.h"
#include "tools/match_sandbox_drill_scene.h"
#include "tools/match_sandbox_measure.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_setup.h"

namespace
{
/** Saved runs offered in the Load list. */
constexpr std::size_t SAVED_LISTED = 30;

/** Categorical colours for a dark surface, in their fixed order (the
 * dataviz reference palette's dark steps). A colour follows its entity: a
 * tag value's or a test's place in the catalogue, never its rank on screen. */
constexpr std::array<ImU32, 8> SERIES{
    IM_COL32(0x39, 0x87, 0xe5, 255), IM_COL32(0xd9, 0x59, 0x26, 255),
    IM_COL32(0x19, 0x9e, 0x70, 255), IM_COL32(0xc9, 0x85, 0x00, 255),
    IM_COL32(0xd5, 0x51, 0x81, 255), IM_COL32(0x00, 0x83, 0x00, 255),
    IM_COL32(0x90, 0x85, 0xe9, 255), IM_COL32(0xe6, 0x67, 0x67, 255)};
constexpr ImU32 TEXT = SandboxPageUI::CREAM;
constexpr ImU32 TEXT_MUTED = SandboxPageUI::MUTED;
constexpr ImU32 GRID = IM_COL32(255, 255, 255, 28);
constexpr ImU32 SURFACE = SandboxPageUI::DARK;
constexpr ImVec4 WARNING{1.0F, 0.75F, 0.3F, 1.0F};
/** Rate ramp (0 to 1) and change scale (red worse, blue better, grey none;
 * FULL_CHANGE_POINTS is full colour; a relative change of a mean is scaled
 * the same way, on a base of at least MIN_CHANGE_BASE). */
constexpr ImVec4 RATE_ZERO{0.10F, 0.14F, 0.20F, 1.0F};
constexpr ImVec4 RATE_FULL{0.53F, 0.71F, 0.94F, 1.0F};
constexpr ImVec4 CHANGE_NONE{0.22F, 0.22F, 0.21F, 1.0F};
constexpr ImVec4 CHANGE_WORSE{0.90F, 0.40F, 0.40F, 1.0F};
constexpr ImVec4 CHANGE_BETTER{0.22F, 0.53F, 0.90F, 1.0F};
constexpr double FULL_CHANGE_POINTS = 30.0;
constexpr double MIN_CHANGE_BASE = 1e-3;
/** Rates above this get dark text on their light cell. */
constexpr double DARK_TEXT_RATE = 0.7;
constexpr double PERCENT = 100.0;
/** Axis ticks: 1, 2 or 5 times a power of ten; rates every 20 points. */
constexpr double TICK_DECADE = 10.0;
constexpr std::array<double, 4> TICK_STEPS{1.0, 2.0, 5.0, 10.0};
constexpr double RATE_TICK_STEP = 20.0;
constexpr int VALUE_TICKS = 5;
constexpr double SAME_SKILL = 1e-6;
constexpr double TICK_TOLERANCE = 1e-9;
/** Controls' limits. */
constexpr int MIN_REPEATS = 5;
constexpr int MAX_REPEATS = 300;
constexpr float MAX_WATCH_SKILL = 100.0F;
/** Layout (scaled pixels). */
constexpr float TICK_COLUMN_WIDTH = 28.0F;
constexpr float SWATCH_TOP = 11.0F;
constexpr float SWATCH_BOTTOM = 17.0F;
constexpr float SWATCH_WIDTH = 6.0F;
constexpr float SWATCH_ROOM = 8.0F;
constexpr float FIELDS_TABLE_WIDTH = 660.0F;
constexpr float MIN_TABLE_HEIGHT = 240.0F;
/** The detail table: a row per skill, plus the header and the scrollbar. */
constexpr float DETAIL_EXTRA_ROWS = 2.0F;
/** The chart (scaled pixels unless said). */
constexpr float CENTRE = 0.5F;
constexpr float AXIS_LABEL_GAP = 6.0F;
constexpr float SKILL_LABEL_GAP = 4.0F;
constexpr float HOVER_PIXELS = 12.0F;
constexpr float LINE_THICKNESS = 2.0F;
constexpr float FAINT_LINE_THICKNESS = 1.5F;
constexpr float FAINT_DASH = 4.0F;
constexpr int FAINT_ALPHA = 120;
constexpr float FAINT_MARKER_RADIUS = 3.0F;
constexpr float MARKER_RING_RADIUS = 5.0F;
constexpr float MARKER_RADIUS = 4.0F;
/** A dash, then a gap as long. */
constexpr float DASH_AND_GAP = 2.0F;
/** Curves are labelled at their right end when there are at most this many. */
constexpr std::size_t MAX_END_LABELS = 8;
constexpr float END_LABEL_GAP = 7.0F;
/** Legend entries: a swatch (middle half of the line, rounded) and gaps. */
constexpr float LEGEND_ENTRY_EXTRA = 30.0F;
constexpr float LEGEND_SWATCH_TOP = 0.25F;
constexpr float LEGEND_SWATCH_BOTTOM = 0.75F;
constexpr float LEGEND_SWATCH_WIDTH = 12.0F;
constexpr float LEGEND_SWATCH_ROUNDING = 2.0F;
constexpr float LEGEND_SWATCH_ROOM = 16.0F;
constexpr float LEGEND_ENTRY_GAP = 14.0F;

constexpr std::array<const char*, 6> SKILL_OVERRIDES{
    "Each test's own", "Passing",     "Shooting",
    "Dribbling",       "First Touch", "Condition"};

/** Metrics worth a column in one test's detail, where it has them. */
constexpr std::array<const char*, 6> DETAIL_METRICS{
    "Angle failed",        "Weight failed",      "Strike miss (m)",
    "Abs miss across (m)", "Launch speed (m/s)", "Short"};

void nextButton(const char* label, float scale, ImFont* font)
{
  if (ImGui::GetItemRectMax().x + (SandboxPageUI::PANEL_GAP * scale) +
          SandboxPageUI::buttonWidth(label, scale, font) <=
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
  {
    ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  }
}

void field(const char* label)
{
  ImGui::TextWrapped("%s", label);
  ImGui::SetNextItemWidth(-1);
}

ImU32 withAlpha(ImU32 colour, int alpha)
{
  return (colour & ~IM_COL32_A_MASK) |
         (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

ImU32 blend(ImVec4 a, ImVec4 b, float t)
{
  t = std::clamp(t, 0.0F, 1.0F);
  return ImGui::GetColorU32(ImVec4(a.x + ((b.x - a.x) * t),
                                   a.y + ((b.y - a.y) * t),
                                   a.z + ((b.z - a.z) * t), 1.0F));
}

/** Success rate 0-1 on one blue ramp, receding into the dark surface at 0. */
ImU32 rateColour(double rate)
{
  return blend(RATE_ZERO, RATE_FULL, static_cast<float>(rate));
}

/** A change in points: red worse, blue better, grey none (±30 full). */
ImU32 changeColour(double points)
{
  const ImVec4 grey = CHANGE_NONE;
  const auto t = static_cast<float>(std::abs(points) / FULL_CHANGE_POINTS);
  return points < 0.0 ? blend(grey, CHANGE_WORSE, t)
                      : blend(grey, CHANGE_BETTER, t);
}

double tickStep(double range, int ticks)
{
  const double raw = range / std::max(1, ticks);
  const double power = std::pow(TICK_DECADE, std::floor(std::log10(raw)));
  for (const double step : TICK_STEPS)
  {
    if (raw <= step * power)
    {
      return step * power;
    }
  }
  return TICK_DECADE * power;
}

std::vector<float> parseSkills(const char* text)
{
  std::vector<float> skills;
  std::stringstream stream(text);
  for (std::string part; std::getline(stream, part, ',');)
  {
    while (!part.empty() && part.front() == ' ')
    {
      part.erase(part.begin());
    }
    while (!part.empty() && part.back() == ' ')
    {
      part.pop_back();
    }
    float value = 0.0F;
    const auto [end, error] =
        std::from_chars(part.data(), part.data() + part.size(), value);
    if (error == std::errc() && end == part.data() + part.size())
    {
      skills.push_back(std::clamp(value, 1.0F, 100.0F));
    }
  }
  std::ranges::sort(skills);
  skills.erase(std::ranges::unique(skills).begin(), skills.end());
  return skills;
}

void dashedLine(ImDrawList& draw, ImVec2 a, ImVec2 b, ImU32 colour,
                float thickness, float dash)
{
  const float length = std::hypot(b.x - a.x, b.y - a.y);
  if (length < 1.0F)
  {
    return;
  }
  const float period = dash * DASH_AND_GAP;
  const int dashes = static_cast<int>(std::ceil(length / period));
  for (int index = 0; index < dashes; ++index)
  {
    const float at = static_cast<float>(index) * period;
    const float end = std::min(at + dash, length);
    draw.AddLine(ImVec2(a.x + ((b.x - a.x) * at / length),
                        a.y + ((b.y - a.y) * at / length)),
                 ImVec2(a.x + ((b.x - a.x) * end / length),
                        a.y + ((b.y - a.y) * end / length)),
                 colour, thickness);
  }
}
}  // namespace

TechniqueBenchScene::TechniqueBenchScene(GUIView* guiView_ptr, bool runNow)
    : GUIScene(guiView_ptr), runPending(runNow)
{
  reload();
  // Opens on the passes, coloured by type: every curve at once is too many
  // to read.
  filters["family"] = {"pass"};
  for (std::size_t index = 0; index < tagValues.size(); ++index)
  {
    if (tagValues[index].first == "type")
    {
      colourBy = static_cast<int>(index);
    }
  }
  watchDrills = makeDrills();
  for (const auto& drill : watchDrills)
  {
    for (const DrillParameter& parameter : drill->parameters())
    {
      if (parameter.value != nullptr)
      {
        watchDefaults[drill.get()][parameter.name] = *parameter.value;
      }
    }
  }
}

TechniqueBenchScene::~TechniqueBenchScene() = default;

SceneID TechniqueBenchScene::getID() const { return SceneID::MATCH_SANDBOX; }

void TechniqueBenchScene::onEnter()
{
  // Reuse the launch page's fonts. Direct tool entry loads them once into
  // the current atlas, so reopening the bench does not add font sources.
  auto* atlas = ImGui::GetIO().Fonts;
  const auto font = [&](const char* file, float size)
  {
    const auto config = SandboxTypography::manropeConfig();
    for (auto* existing : atlas->Fonts)
    {
      if (std::string_view(existing->GetDebugName()).find(file) !=
              std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
      {
        return existing;
      }
    }
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(),
        size, &config);
  };
  regularFont = font("Manrope-Regular.ttf", SandboxPageUI::REGULAR_FONT_SIZE);
  boldFont = font("Manrope-Bold.ttf", SandboxPageUI::BOLD_FONT_SIZE);
}

void TechniqueBenchScene::reload()
{
  catalogue = loadBenchCatalogue(benchCataloguePath());
  ticked.assign(catalogue.tests.size(), true);
  std::string skills;
  for (const float skill : catalogue.skills)
  {
    skills += std::format("{}{:g}", skills.empty() ? "" : ", ", skill);
  }
  std::snprintf(skillsText.data(), skillsText.size(), "%s", skills.c_str());
  tagValues.clear();
  for (const BenchTest& test : catalogue.tests)
  {
    for (const auto& [key, value] : test.tags)
    {
      auto found = std::ranges::find(
          tagValues, key, [](const auto& entry) { return entry.first; });
      if (found == tagValues.end())
      {
        tagValues.push_back({key, {}});
        found = tagValues.end() - 1;
      }
      if (std::ranges::find(found->second, value) == found->second.end())
      {
        found->second.push_back(value);
      }
    }
  }
  // Filters on values that are gone go.
  for (auto& [key, values] : filters)
  {
    const auto entry = std::ranges::find(
        tagValues, key, [](const auto& item) { return item.first; });
    std::erase_if(values,
                  [&](const std::string& value)
                  {
                    return entry == tagValues.end() ||
                           std::ranges::find(entry->second, value) ==
                               entry->second.end();
                  });
  }
  if (colourBy >= static_cast<int>(tagValues.size()))
  {
    colourBy = -1;
  }
}

bool TechniqueBenchScene::shown(const BenchTest& test) const
{
  return std::ranges::all_of(filters,
                             [&](const auto& filter)
                             {
                               const auto& [key, values] = filter;
                               return values.empty() ||
                                      values.contains(test.tag(key));
                             });
}

std::vector<const BenchTest*> TechniqueBenchScene::drawnTests() const
{
  std::vector<const BenchTest*> tests;
  for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
  {
    if (ticked[index] && shown(catalogue.tests[index]))
    {
      tests.push_back(&catalogue.tests[index]);
    }
  }
  return tests;
}

ImU32 TechniqueBenchScene::colourOf(const BenchTest& test) const
{
  if (colourBy >= 0)
  {
    const auto& [key, values] = tagValues[static_cast<std::size_t>(colourBy)];
    const auto found = std::ranges::find(values, test.tag(key));
    const auto place = static_cast<std::size_t>(found - values.begin());
    return SERIES[place % SERIES.size()];
  }
  const auto found =
      std::ranges::find(catalogue.tests, test.id, &BenchTest::id);
  const auto place = static_cast<std::size_t>(found - catalogue.tests.begin());
  return SERIES[place % SERIES.size()];
}

const BenchResults* TechniqueBenchScene::shownRun() const
{
  return showIndex >= 0 && showIndex < static_cast<int>(runs.size())
             ? runs[static_cast<std::size_t>(showIndex)].get()
             : nullptr;
}

const BenchResults* TechniqueBenchScene::compareRun() const
{
  return compareIndex >= 0 && compareIndex < static_cast<int>(runs.size()) &&
                 compareIndex != showIndex
             ? runs[static_cast<std::size_t>(compareIndex)].get()
             : nullptr;
}

std::optional<double> TechniqueBenchScene::valueOf(const BenchCell* cell) const
{
  if (cell == nullptr)
  {
    return std::nullopt;
  }
  if (result == 0)
  {
    const auto rate = cell->rate();
    if (!rate)
    {
      return std::nullopt;
    }
    return *rate * 100.0;
  }
  const BenchResults* run = shownRun();
  if ((run == nullptr) ||
      result - 1 >= static_cast<int>(run->metricNames().size()))
  {
    return std::nullopt;
  }
  const auto found = cell->metrics.find(
      run->metricNames()[static_cast<std::size_t>(result - 1)]);
  if (found == cell->metrics.end() || found->second.count == 0)
  {
    return std::nullopt;
  }
  return found->second.mean();
}

void TechniqueBenchScene::start(bool onlyShown)
{
  if (runner)
  {
    return;
  }
  BenchSpec spec;
  for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
  {
    if (!onlyShown || (ticked[index] && shown(catalogue.tests[index])))
    {
      spec.tests.push_back(index);
    }
  }
  if (spec.tests.empty())
  {
    status = "No tests to run: tick some, or loosen the filters.";
    return;
  }
  spec.skills = parseSkills(skillsText.data());
  if (spec.skills.empty())
  {
    spec.skills = catalogue.skills;
  }
  spec.repeats = std::max(1, repeats);
  spec.firstSeed = static_cast<std::uint32_t>(std::max(0, firstSeed));
  if (skillOverride > 0)
  {
    spec.skillOverride =
        SKILL_OVERRIDES[static_cast<std::size_t>(skillOverride)];
  }
  spec.tuning = sandboxTuning();
  runner = std::make_unique<BenchRunner>(
      catalogue, guiView->getController().getStatsConfig(), std::move(spec));
  // The run before becomes the comparison.
  compareIndex = static_cast<int>(runs.size()) - 1;
  runs.push_back(std::make_unique<BenchResults>());
  runningIndex = static_cast<int>(runs.size()) - 1;
  showIndex = runningIndex;
  runner->collect(*runs.back());
  status.clear();
}

void TechniqueBenchScene::finishRun()
{
  if (!runner)
  {
    return;
  }
  BenchResults& results = *runs[static_cast<std::size_t>(runningIndex)];
  runner->collect(results);
  const bool complete = runner->done();
  const double seconds = runner->seconds();
  const std::size_t done = runner->runsDone();
  runner.reset();
  runningIndex = -1;
  if (!complete)
  {
    results.label += " (stopped)";
  }
  const std::filesystem::path path = benchResultsPath();
  status = results.save(path)
               ? std::format("{} runs in {:.1f} s. Saved {}", done, seconds,
                             path.string())
               : std::format("{} runs in {:.1f} s. Could not write {}", done,
                             seconds, path.string());
}

void TechniqueBenchScene::loadSaved(const std::filesystem::path& path)
{
  std::string error;
  auto loaded = BenchResults::load(path, error);
  if (!loaded)
  {
    status = error;
    return;
  }
  runs.push_back(std::make_unique<BenchResults>(std::move(*loaded)));
  compareIndex = static_cast<int>(runs.size()) - 1;
  if (showIndex < 0)
  {
    showIndex = compareIndex;
  }
  status = "Loaded " + path.filename().string() + " (as the comparison).";
}

void TechniqueBenchScene::update(float /*deltaTime*/)
{
  if (runPending)
  {
    runPending = false;
    start(false);
  }
  if (!runner)
  {
    return;
  }
  runner->collect(*runs[static_cast<std::size_t>(runningIndex)]);
  if (!runner->running())
  {
    finishRun();
  }
}

void TechniqueBenchScene::openContactView()
{
  // Back to it if the bench was opened from it, so the two don't stack up.
  if (dynamic_cast<ContactViewScene*>(guiView->getSceneBelowTop()) != nullptr)
  {
    guiView->popScene();
  }
  else
  {
    guiView->overlayScene(std::make_unique<ContactViewScene>(guiView));
  }
}

void TechniqueBenchScene::render()
{
  namespace Page = SandboxPageUI;
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  const Page::Style style(regularFont, scale);
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(SandboxPageUI::PAGE_PADDING * scale,
                             SandboxPageUI::PAGE_PADDING * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##technique_bench", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter =
      (SandboxPageUI::PANEL_GAP * scale) + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= 1000 * scale;
  const float panel =
      columns ? (available.x >= 1400 * scale ? 440.0F : 400.0F) * scale
              : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##bench_controls", ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##bench_results", ImVec2(width, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderResults(std::max(scale, width - gutter), available.y);
    ImGui::EndChild();
  }
  else
  {
    ImGui::BeginChild("##bench_page", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    renderResults(panel, available.y);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

void TechniqueBenchScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const std::string context = std::format(
      "Pass tolerance: {:.0f} m at {:.0f} m, rising to {:.0f} m at {:.0f} m. "
      "Shots: on target.",
      BENCH_NEAR_TOLERANCE, BENCH_NEAR_METRES, BENCH_FAR_TOLERANCE,
      BENCH_FAR_METRES);
  Page::header("Technique bench",
               "Compare pass and shot success across skill levels and seeds.",
               width, scale, boldFont, regularFont, context.c_str(), true);
  Page::beginCard("##bench_actions", "RUN BENCH", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to sandbox", false, scale, boldFont))
  {
    if (runner)
    {
      runner->stop();
    }
    finishRun();
    guiView->popScene();
  }
  const bool running = runner != nullptr;
  ImGui::BeginDisabled(running);
  if (Page::button("Run ticked", true, scale, boldFont))
  {
    start(true);
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("The ticked tests the filters show.");
  }
  nextButton("Run all", scale, boldFont);
  if (Page::button("Run all", false, scale, boldFont))
  {
    start(false);
  }
  ImGui::EndDisabled();
  if (running)
  {
    nextButton("Stop", scale, boldFont);
    if (Page::button("Stop", false, scale, boldFont))
    {
      runner->stop();
    }
    const float share = runner->runsTotal() == 0
                            ? 1.0F
                            : static_cast<float>(runner->runsDone()) /
                                  static_cast<float>(runner->runsTotal());
    ImGui::ProgressBar(
        share, ImVec2(-1.0F, 0.0F),
        std::format("{} / {} runs · {:.1f} s", runner->runsDone(),
                    runner->runsTotal(), runner->seconds())
            .c_str());
    ImGui::PushTextWrapPos(0.0F);
    for (const std::string& note : runner->warnings())
    {
      ImGui::TextColored(WARNING, "%s", note.c_str());
    }
    ImGui::PopTextWrapPos();
  }
  if (!status.empty())
  {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::PopTextWrapPos();
  }

  Page::endCard(scale);
  Page::beginCard("##bench_tabs", "CONTROLS", width, Page::MID, scale,
                  boldFont);
  const std::array<const char*, 4> tabs{"Run", "Tests", "Filters", "Tuning"};
  for (std::size_t tab = 0; tab < tabs.size(); ++tab)
  {
    if (tab > 0)
    {
      ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
    }
    if (Page::button(tabs[tab], panelTab == static_cast<int>(tab), scale,
                     boldFont))
    {
      panelTab = static_cast<int>(tab);
    }
  }
  Page::endCard(scale);
  if (panelTab == 1)
  {
    Page::beginCard("##bench_tests", "TEST CATALOGUE", width, Page::LIGHT,
                    scale, boldFont);
    renderTestList();
    Page::endCard(scale);
    return;
  }
  if (panelTab == 2 || panelTab == 3)
  {
    Page::beginCard(panelTab == 2 ? "##bench_filters" : "##bench_tuning",
                    panelTab == 2 ? "FILTERS" : "TUNING", width, Page::LIGHT,
                    scale, boldFont);
    ImGui::PushTextWrapPos(0);
    if (panelTab == 2)
    {
      renderFilters();
    }
    else
    {
      renderTuning();
    }
    ImGui::PopTextWrapPos();
    Page::endCard(scale);
    return;
  }
  Page::beginCard("##bench_run_setup", "RUN SETUP", width, Page::LIGHT, scale,
                  boldFont);
  ImGui::BeginDisabled(running);
  if (ImGui::BeginTable(
          "##run_fields", 2,
          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
  {
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    field("Runs per point");
    ImGui::SliderInt("##repeats", &repeats, MIN_REPEATS, MAX_REPEATS);
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Seeds per test and skill. 50 gives a rate to about +/-7 points near "
          "50%%; 200 to about +/-3.5.");
    }
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    field("First seed");
    ImGui::InputInt("##first_seed", &firstSeed);
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    field("Skills");
    ImGui::InputText("##skills", skillsText.data(), skillsText.size());
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Comma-separated skill values (1-100): %s",
                        skillsText.data());
    }
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    field("Skill swept");
    SandboxPageUI::combo("##skill_override", &skillOverride,
                         SKILL_OVERRIDES.data(),
                         static_cast<int>(SKILL_OVERRIDES.size()));
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Each test's own skill, or one setting for every test that has it.");
    }
    ImGui::EndTable();
  }
  ImGui::EndDisabled();
  Page::endCard(scale);
  Page::beginCard("##bench_runs", "RUNS & COMPARISON", width, Page::MID, scale,
                  boldFont);
  const auto runName = [&](int index)
  {
    if (index < 0 || index >= static_cast<int>(runs.size()))
    {
      return std::string("(none)");
    }
    const BenchResults& run = *runs[static_cast<std::size_t>(index)];
    return std::format(
        "{}: {}{}", index + 1, run.label,
        run.tuningChanges.empty()
            ? std::string(", game's tuning")
            : std::format(", {} changes", run.tuningChanges.size()));
  };
  const auto runCombo = [&](const char* label, int& index, bool optional)
  {
    field(label);
    const std::string id = std::string("##") + label;
    if (SandboxPageUI::beginCombo(id.c_str(), runName(index).c_str()))
    {
      if (optional && ImGui::Selectable("(none)", index < 0))
      {
        index = -1;
      }
      for (int candidate = static_cast<int>(runs.size()) - 1; candidate >= 0;
           --candidate)
      {
        if (ImGui::Selectable(runName(candidate).c_str(), candidate == index))
        {
          index = candidate;
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered() && index >= 0 &&
        index < static_cast<int>(runs.size()))
    {
      const BenchResults& run = *runs[static_cast<std::size_t>(index)];
      std::string tip = std::format("{} runs per point\nTuning:", run.repeats);
      if (run.tuningChanges.empty())
      {
        tip += " the game's values";
      }
      for (const std::string& line : run.tuningChanges)
      {
        tip += "\n  " + line;
      }
      ImGui::SetTooltip("%s", tip.c_str());
    }
  };
  runCombo("Show", showIndex, false);
  runCombo("Compare with", compareIndex, true);
  field("Load saved");
  if (SandboxPageUI::beginCombo("##load_saved", "Choose a file…"))
  {
    std::vector<std::filesystem::path> files;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(
             measureFolder() / "bench", ignored))
    {
      if (entry.path().extension() == ".csv")
      {
        files.push_back(entry.path());
      }
    }
    std::ranges::sort(files, std::greater<>());
    if (files.size() > SAVED_LISTED)
    {
      files.resize(SAVED_LISTED);
    }
    if (files.empty())
    {
      ImGui::TextDisabled("No saved runs yet.");
    }
    for (const auto& file : files)
    {
      if (ImGui::Selectable(file.stem().string().c_str()))
      {
        loadSaved(file);
      }
    }
    ImGui::EndCombo();
  }

  Page::endCard(scale);
}

void TechniqueBenchScene::renderTuning()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  TechniqueTuning& tuning = sandboxTuning();
  const std::vector<std::string> changes = tuning.changes();
  if (changes.empty())
  {
    ImGui::TextUnformatted("The game's values.");
  }
  else
  {
    ImGui::TextColored(WARNING, "%zu changes from the game's values.",
                       changes.size());
  }
  if (!changes.empty() && ImGui::IsItemHovered())
  {
    std::string tip;
    for (const std::string& line : changes)
    {
      tip += line + "\n";
    }
    ImGui::SetTooltip("%s", tip.c_str());
  }
  const auto bodyName = [](BodyId body)
  { return body == BodyId::BINTURONG ? "Binturong" : "Aardvark"; };
  ImGui::TextDisabled("Bodies: drill side %s, other side %s (the sandbox's).",
                      bodyName(sandboxBodies()[0]),
                      bodyName(sandboxBodies()[1]));
  if (sandboxBodies()[0] != BodyId::BINTURONG)
  {
    ImGui::TextColored(WARNING,
                       "The technique rules are the Binturong body's.");
  }
  if (Page::button("Edit in contact view", false, scale, boldFont))
  {
    openContactView();
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "The contact view edits this tuning (the stretch map and "
        "every touch and strike knob); runs started here use "
        "it.");
  }
  nextButton("Copy", scale, boldFont);
  ImGui::BeginDisabled(changes.empty());
  if (Page::button("Copy", false, scale, boldFont))
  {
    ImGui::SetClipboardText(tuning.changesText().c_str());
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    ImGui::SetTooltip("The changes as text, to copy into match_tuning.h.");
  }
  nextButton("Reset", scale, boldFont);
  if (Page::button("Reset", false, scale, boldFont))
  {
    tuning = TechniqueTuning{};
  }
  ImGui::EndDisabled();
}

void TechniqueBenchScene::renderFilters()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const float rowWidth = ImGui::GetContentRegionAvail().x;
  for (const auto& [key, values] : tagValues)
  {
    ImGui::PushID(key.c_str());
    ImGui::TextDisabled("%s", key.c_str());
    std::set<std::string>& active = filters[key];
    float used = 0;
    const auto chip = [&](const char* label, bool selected)
    {
      const float width = Page::buttonWidth(label, scale, boldFont);
      if (used > 0 &&
          used + (SandboxPageUI::PANEL_GAP * scale) + width <= rowWidth)
      {
        ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
      }
      else
      {
        used = 0;
      }
      used += width + (SandboxPageUI::PANEL_GAP * scale);
      return Page::button(label, selected, scale, boldFont);
    };
    if (chip("All", active.empty()))
    {
      active.clear();
    }
    for (const auto& value : values)
    {
      if (chip(value.c_str(), active.contains(value)))
      {
        if (active.contains(value))
        {
          active.erase(value);
        }
        else
        {
          active.insert(value);
        }
      }
    }
    ImGui::PopID();
  }
}

void TechniqueBenchScene::renderTestList()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  ImGui::TextWrapped("%s", catalogue.path.string().c_str());
  for (const auto& problem : catalogue.problems)
  {
    ImGui::TextColored(WARNING, "%s", problem.c_str());
  }
  if (Page::button("Tick shown", false, scale, boldFont))
  {
    for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
    {
      if (shown(catalogue.tests[index]))
      {
        ticked[index] = true;
      }
    }
  }
  nextButton("Untick shown", scale, boldFont);
  if (Page::button("Untick shown", false, scale, boldFont))
  {
    for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
    {
      if (shown(catalogue.tests[index]))
      {
        ticked[index] = false;
      }
    }
  }
  nextButton("Reload", scale, boldFont);
  if (Page::button("Reload", false, scale, boldFont))
  {
    reload();
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("Reads the tests file again (edit it, then reload).");
  }
  std::string section;
  bool open = false;
  bool table = false;
  const auto close = [&]
  {
    if (table)
    {
      ImGui::EndTable();
      ImGui::PopStyleVar();
      ImGui::PopID();
      table = false;
    }
  };
  for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
  {
    const auto& test = catalogue.tests[index];
    if (!shown(test))
    {
      continue;
    }
    if (test.section != section)
    {
      close();
      section = test.section;
      open = ImGui::CollapsingHeader(section.c_str(),
                                     ImGuiTreeNodeFlags_DefaultOpen);
    }
    if (!open)
    {
      continue;
    }
    if (!table)
    {
      ImGui::PushID(section.c_str());
      ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                          ImVec2(3 * scale, 3 * scale));
      table = ImGui::BeginTable("##tests", 2,
                                ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_BordersInnerH |
                                    ImGuiTableFlags_BordersInnerV);
      if (!table)
      {
        ImGui::PopStyleVar();
        ImGui::PopID();
        continue;
      }
      ImGui::TableSetupColumn("Tick", ImGuiTableColumnFlags_WidthFixed,
                              TICK_COLUMN_WIDTH * scale);
      ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch);
    }
    ImGui::PushID(static_cast<int>(index));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    bool on = ticked[index];
    if (ImGui::Checkbox("##tick", &on))
    {
      ticked[index] = on;
    }
    ImGui::TableNextColumn();
    const ImVec2 swatch = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(swatch.x, swatch.y + (SWATCH_TOP * scale)),
        ImVec2(swatch.x + (SWATCH_WIDTH * scale),
               swatch.y + (SWATCH_BOTTOM * scale)),
        colourOf(test));
    ImGui::Dummy(ImVec2(SWATCH_ROOM * scale, ImGui::GetFrameHeight()));
    ImGui::SameLine(0, 4 * scale);
    const std::string label = std::format("{}  {}", test.id, test.name);
    if (ImGui::Selectable(
            label.c_str(), focus == test.id, 0,
            ImVec2(std::max(scale, ImGui::GetContentRegionAvail().x),
                   ImGui::GetFrameHeight())))
    {
      focus = test.id;
      selectView = 2;
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("%s\n%s", test.name.c_str(),
                        test.success.describe(test.range).c_str());
    }
    ImGui::PopID();
  }
  close();
}

// --- Results
// ---------------------------------------------------------------------

void TechniqueBenchScene::renderResults(float width, float height)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const auto* run = shownRun();
  Page::beginCard("##bench_result_controls", "RESULTS", width, Page::MID, scale,
                  boldFont);
  if (run == nullptr)
  {
    ImGui::TextWrapped(
        "Run the ticked tests or the full catalogue to see success curves, a "
        "results grid and individual test details.");
    Page::endCard(scale);
    return;
  }
  const auto& names = run->metricNames();
  result = std::clamp(result, 0, static_cast<int>(names.size()));
  if (ImGui::BeginTable("##result_fields", 2, ImGuiTableFlags_SizingStretchProp,
                        ImVec2(std::min(FIELDS_TABLE_WIDTH * scale,
                                        ImGui::GetContentRegionAvail().x),
                               0)))
  {
    ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthStretch, 2);
    ImGui::TableSetupColumn("Colour", ImGuiTableColumnFlags_WidthStretch, 1);
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    field("Result");
    if (SandboxPageUI::beginCombo(
            "##result",
            result == 0 ? "Success rate (%)"
                        : names[static_cast<std::size_t>(result - 1)].c_str()))
    {
      if (ImGui::Selectable("Success rate (%)", result == 0))
      {
        result = 0;
      }
      for (std::size_t index = 0; index < names.size(); ++index)
      {
        if (ImGui::Selectable(names[index].c_str(),
                              result == static_cast<int>(index) + 1))
        {
          result = static_cast<int>(index) + 1;
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Success rate, or the mean of any result over the "
          "finished runs.");
    }
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    field("Colour by");
    if (SandboxPageUI::beginCombo(
            "##colour_by",
            colourBy < 0
                ? "test"
                : tagValues[static_cast<std::size_t>(colourBy)].first.c_str()))
    {
      if (ImGui::Selectable("test", colourBy < 0))
      {
        colourBy = -1;
      }
      for (std::size_t index = 0; index < tagValues.size(); ++index)
      {
        if (ImGui::Selectable(tagValues[index].first.c_str(),
                              colourBy == static_cast<int>(index)))
        {
          colourBy = static_cast<int>(index);
        }
      }
      ImGui::EndCombo();
    }
    ImGui::EndTable();
  }
  if (const BenchResults* other = compareRun())
  {
    ImGui::TextWrapped("Dashed: run %d (%s)", compareIndex + 1,
                       other->label.c_str());
  }

  if (selectView >= 0)
  {
    view = selectView;
    selectView = -1;
  }
  const std::array<const char*, 3> tabs{"Curves", "Grid", "Test"};
  for (int tab = 0; tab < 3; ++tab)
  {
    if (tab > 0)
    {
      ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
    }
    if (Page::button(tabs[tab], view == tab, scale, boldFont))
    {
      view = tab;
    }
  }
  Page::endCard(scale);
  const float bodyHeight =
      std::max(300 * scale, height - ImGui::GetCursorPosY() - (56 * scale));
  Page::beginCard("##bench_result_body",
                  view == 0 ? (result == 0 ? "SUCCESS CURVES" : "RESULT CURVES")
                  : view == 1 ? "RESULT GRID"
                              : "TEST DETAIL",
                  width, Page::DARK, scale, boldFont);
  ImGui::PushTextWrapPos(0);
  if (view == 0)
  {
    renderChart(ImVec2(ImGui::GetContentRegionAvail().x, bodyHeight));
  }
  else if (view == 1)
  {
    renderGrid(ImVec2(ImGui::GetContentRegionAvail().x, bodyHeight));
  }
  else
  {
    renderTest();
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

void TechniqueBenchScene::renderChart(ImVec2 size)
{
  const BenchResults& run = *shownRun();
  const BenchResults* other = compareRun();
  const std::vector<const BenchTest*> tests = drawnTests();
  const float scale = Theme::scale();
  const bool rates = result == 0;

  // Legend rows below the plot: by tag value, or by test.
  struct Entry
  {
    std::string label;
    ImU32 colour;
  };
  std::vector<Entry> legend;
  if (colourBy >= 0)
  {
    const auto& [key, values] = tagValues[static_cast<std::size_t>(colourBy)];
    for (const std::string& value : values)
    {
      if (std::ranges::any_of(tests, [&](const BenchTest* test)
                              { return test->tag(key) == value; }))
      {
        legend.push_back(
            {.label = std::format("{} {}", key, value),
             .colour =
                 SERIES[static_cast<std::size_t>(
                            std::ranges::find(values, value) - values.begin()) %
                        SERIES.size()]});
      }
    }
  }
  else
  {
    for (const BenchTest* test : tests)
    {
      legend.push_back({.label = test->id, .colour = colourOf(*test)});
    }
  }
  const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
  float legendWidth = 0.0F;
  for (const Entry& entry : legend)
  {
    legendWidth += ImGui::CalcTextSize(entry.label.c_str()).x +
                   (LEGEND_ENTRY_EXTRA * scale);
  }
  const float legendRows =
      std::ceil(legendWidth / std::max(size.x, 1.0F)) + 1.0F;
  const ImVec2 plotSize(
      size.x, std::max(200.0F * scale, size.y - (legendRows * lineHeight) -
                                           (lineHeight * 1.5F)));

  // Ranges: skills across; 0-100 for rates, else the data's.
  std::vector<float> skills = run.skills;
  if (other != nullptr)
  {
    for (const float skill : other->skills)
    {
      if (std::ranges::find(skills, skill) == skills.end())
      {
        skills.push_back(skill);
      }
    }
  }
  std::ranges::sort(skills);
  if (skills.empty())
  {
    return;
  }
  double xMin = skills.front();
  double xMax = skills.back();
  if (xMax - xMin < SAME_SKILL)
  {
    xMin -= 1.0;
    xMax += 1.0;
  }
  double yMin = rates ? 0.0 : std::numeric_limits<double>::max();
  double yMax = rates ? 100.0 : std::numeric_limits<double>::lowest();
  if (!rates)
  {
    for (const BenchTest* test : tests)
    {
      for (const float skill : skills)
      {
        for (const BenchResults* source : {&run, other})
        {
          if (source != nullptr)
          {
            if (const auto value = valueOf(source->cell(test->id, skill)))
            {
              yMin = std::min(yMin, *value);
              yMax = std::max(yMax, *value);
            }
          }
        }
      }
    }
    if (yMin > yMax)
    {
      ImGui::TextDisabled("No values for this result in the tests shown.");
      return;
    }
    const double pad =
        std::max((yMax - yMin) * 0.08, 1e-3 + (0.05 * std::abs(yMax)));
    yMin -= pad;
    yMax += pad;
  }

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##chart", plotSize);
  const bool hovering = ImGui::IsItemHovered();
  const bool clicked = ImGui::IsItemClicked();
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  const float left = origin.x + (52.0F * scale);
  const float right = origin.x + plotSize.x - (48.0F * scale);
  const float top = origin.y + (8.0F * scale);
  const float bottom = origin.y + plotSize.y - (36.0F * scale);
  const auto sx = [&](double x)
  {
    return left +
           (static_cast<float>((x - xMin) / (xMax - xMin)) * (right - left));
  };
  const auto sy = [&](double y)
  {
    return bottom -
           (static_cast<float>((y - yMin) / (yMax - yMin)) * (bottom - top));
  };
  draw.AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), SURFACE);
  const double yStep =
      rates ? RATE_TICK_STEP : tickStep(yMax - yMin, VALUE_TICKS);
  const double firstY = std::ceil(yMin / yStep) * yStep;
  const int yTicks =
      static_cast<int>(std::floor((yMax + TICK_TOLERANCE - firstY) / yStep)) +
      1;
  for (int tick = 0; tick < yTicks; ++tick)
  {
    const double y = firstY + (static_cast<double>(tick) * yStep);
    draw.AddLine(ImVec2(left, sy(y)), ImVec2(right, sy(y)), GRID);
    const std::string label =
        std::format("{:g}", std::round(y / yStep) * yStep);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(left - labelSize.x - (AXIS_LABEL_GAP * scale),
                        sy(y) - (labelSize.y * CENTRE)),
                 TEXT_MUTED, label.c_str());
  }
  for (const float skill : skills)
  {
    const std::string label = std::format("{:g}", skill);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(sx(skill) - (labelSize.x * CENTRE),
                        bottom + (SKILL_LABEL_GAP * scale)),
                 TEXT_MUTED, label.c_str());
  }
  const std::string axis = std::format(
      "Skill ({})", SKILL_OVERRIDES[static_cast<std::size_t>(skillOverride)]);
  const ImVec2 axisSize = ImGui::CalcTextSize(axis.c_str());
  draw.AddText(ImVec2((left + right - axisSize.x) * CENTRE,
                      bottom + (SKILL_LABEL_GAP * scale) + axisSize.y),
               TEXT_MUTED, axis.c_str());

  // The comparison first (dashed, faint), then the shown run over it.
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const BenchTest* hoveredTest = nullptr;
  float hoveredSkill = 0.0F;
  float hoveredDistance = HOVER_PIXELS * scale;
  const auto curve = [&](const BenchResults& source, const BenchTest& test,
                         bool faint) -> std::optional<ImVec2>
  {
    const ImU32 colour = colourOf(test);
    std::optional<ImVec2> before;
    std::optional<ImVec2> last;
    for (const float skill : skills)
    {
      const auto value = valueOf(source.cell(test.id, skill));
      if (!value)
      {
        continue;
      }
      const ImVec2 at(sx(skill), sy(*value));
      if (before)
      {
        if (faint)
        {
          dashedLine(draw, *before, at, withAlpha(colour, FAINT_ALPHA),
                     FAINT_LINE_THICKNESS * scale, FAINT_DASH * scale);
        }
        else
        {
          draw.AddLine(*before, at, colour, LINE_THICKNESS * scale);
        }
      }
      if (faint)
      {
        draw.AddCircle(at, FAINT_MARKER_RADIUS * scale,
                       withAlpha(colour, FAINT_ALPHA), 0, 1.0F * scale);
      }
      else
      {
        // A ring of surface round each marker keeps crossing curves apart.
        draw.AddCircleFilled(at, MARKER_RING_RADIUS * scale, SURFACE);
        draw.AddCircleFilled(at, MARKER_RADIUS * scale, colour);
        const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
        if (distance < hoveredDistance)
        {
          hoveredDistance = distance;
          hoveredTest = &test;
          hoveredSkill = skill;
        }
      }
      before = at;
      last = at;
    }
    return last;
  };
  if (other != nullptr)
  {
    for (const BenchTest* test : tests)
    {
      (void)curve(*other, *test, true);
    }
  }
  std::vector<std::pair<ImVec2, const BenchTest*>> ends;
  for (const BenchTest* test : tests)
  {
    if (const auto end = curve(run, *test, false))
    {
      ends.emplace_back(*end, test);
    }
  }
  // Direct labels at the right end when few enough to read.
  if (ends.size() <= MAX_END_LABELS)
  {
    for (const auto& [at, test] : ends)
    {
      draw.AddText(ImVec2(at.x + (END_LABEL_GAP * scale),
                          at.y - (ImGui::GetTextLineHeight() * CENTRE)),
                   TEXT, test->id.c_str());
    }
  }

  if ((hoveredTest != nullptr) && hovering)
  {
    // A crosshair at the skill, and every number at the point.
    draw.AddLine(ImVec2(sx(hoveredSkill), top),
                 ImVec2(sx(hoveredSkill), bottom), IM_COL32(255, 255, 255, 60),
                 1.0F * scale);
    const BenchCell* cell = run.cell(hoveredTest->id, hoveredSkill);
    std::string tip = std::format("{}  {}\nSkill {:g}", hoveredTest->id,
                                  hoveredTest->name, hoveredSkill);
    if (cell != nullptr)
    {
      const auto rate = cell->rate();
      tip += std::format("\nSuccess {}  ({} of {} finished, {} runs)",
                         rate ? std::format("{:.0f}%", *rate * 100.0) : "-",
                         cell->successes, cell->completed, cell->runs);
      if (!rates)
      {
        if (const auto value = valueOf(cell))
        {
          tip += std::format(
              "\n{}: {:.3f}",
              run.metricNames()[static_cast<std::size_t>(result - 1)], *value);
        }
      }
      if (cell->tolerance.count > 0)
      {
        tip += std::format("\nWithin {:.2f} m (mean tolerance)",
                           cell->tolerance.mean());
      }
    }
    if (other != nullptr)
    {
      const auto then = valueOf(other->cell(hoveredTest->id, hoveredSkill));
      const auto now = valueOf(cell);
      if (then)
      {
        tip += std::format("\nRun {}: {:.1f}{}", compareIndex + 1, *then,
                           now ? std::format("  (change {:+.1f})", *now - *then)
                               : std::string());
      }
    }
    tip += "\n" + hoveredTest->success.describe(hoveredTest->range);
    tip += "\nClick: this test's detail";
    ImGui::SetTooltip("%s", tip.c_str());
    if (clicked)
    {
      focus = hoveredTest->id;
      watchSkill = hoveredSkill;
      selectView = 2;
    }
  }

  // Legend.
  if (colourBy < 0 && tests.size() > SERIES.size())
  {
    ImGui::TextColored(
        WARNING,
        "%zu curves share 8 colours: colour by a tag, or filter, "
        "to tell them apart (hover a point to name it).",
        tests.size());
  }
  float used = 0.0F;
  for (const Entry& entry : legend)
  {
    const float wide =
        ImGui::CalcTextSize(entry.label.c_str()).x + (30.0F * scale);
    if (used > 0.0F && used + wide <= size.x)
    {
      ImGui::SameLine(0.0F, 0.0F);
    }
    else
    {
      used = 0.0F;
    }
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(at.x, at.y + (line * LEGEND_SWATCH_TOP)),
        ImVec2(at.x + (LEGEND_SWATCH_WIDTH * scale),
               at.y + (line * LEGEND_SWATCH_BOTTOM)),
        entry.colour, LEGEND_SWATCH_ROUNDING);
    ImGui::Dummy(ImVec2(LEGEND_SWATCH_ROOM * scale, line));
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::TextUnformatted(entry.label.c_str());
    ImGui::SameLine(0.0F, 0.0F);
    ImGui::Dummy(ImVec2(LEGEND_ENTRY_GAP * scale, line));
    used += wide;
  }
}

void TechniqueBenchScene::renderGrid(ImVec2 size)
{
  const BenchResults& run = *shownRun();
  const BenchResults* other = compareRun();
  const std::vector<const BenchTest*> tests = drawnTests();
  const float controlsY = ImGui::GetCursorPosY();
  ImGui::Checkbox("Change from the comparison", &gridChange);
  ImGui::TextWrapped(gridChange
                         ? "Points (or units) against the dashed run: blue "
                           "up, red down."
                         : "Click a row for its detail.");
  const bool change = gridChange && (other != nullptr);
  std::vector<float> skills = run.skills;
  const int columns = 2 + static_cast<int>(skills.size()) + 1;
  if (!ImGui::BeginTable(
          "##grid", columns,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
              ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
              ImGuiTableFlags_SizingFixedFit,
          ImVec2(0, std::max(MIN_TABLE_HEIGHT * Theme::scale(),
                             size.y - (ImGui::GetCursorPosY() - controlsY) -
                                 (2 * Theme::scale())))))
  {
    return;
  }
  ImGui::TableSetupScrollFreeze(2, 1);
  ImGui::TableSetupColumn("Test");
  ImGui::TableSetupColumn("Name");
  for (const float skill : skills)
  {
    ImGui::TableSetupColumn(std::format("{:g}", skill).c_str());
  }
  ImGui::TableSetupColumn("Success counts when");
  ImGui::PushFont(boldFont, SandboxPageUI::BOLD_FONT_SIZE);
  ImGui::TableHeadersRow();
  ImGui::PopFont();
  std::string section;
  for (const BenchTest* test : tests)
  {
    if (test->section != section)
    {
      section = test->section;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", section.c_str());
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (ImGui::Selectable(test->id.c_str(), focus == test->id,
                          ImGuiSelectableFlags_SpanAllColumns))
    {
      focus = test->id;
      selectView = 2;
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(test->name.c_str());
    for (const float skill : skills)
    {
      ImGui::TableNextColumn();
      const BenchCell* cell = run.cell(test->id, skill);
      const auto value = valueOf(cell);
      if (!value)
      {
        ImGui::TextDisabled("-");
        continue;
      }
      if (change)
      {
        const auto then = valueOf(other->cell(test->id, skill));
        if (!then)
        {
          ImGui::TextDisabled("new");
          continue;
        }
        const double delta = *value - *then;
        ImGui::TableSetBgColor(
            ImGuiTableBgTarget_CellBg,
            changeColour(result == 0
                             ? delta
                             : delta * FULL_CHANGE_POINTS /
                                   std::max(std::abs(*then), MIN_CHANGE_BASE)));
        ImGui::Text(result == 0 ? "%+.0f" : "%+.2f", delta);
      }
      else
      {
        if (result == 0)
        {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                 rateColour(*value / PERCENT));
          ImGui::PushStyleColor(ImGuiCol_Text, *value > DARK_TEXT_RATE * PERCENT
                                                   ? IM_COL32(16, 20, 28, 255)
                                                   : TEXT);
          ImGui::Text("%.0f", *value);
          ImGui::PopStyleColor();
        }
        else
        {
          ImGui::Text("%.2f", *value);
        }
      }
      if (ImGui::IsItemHovered() && (cell != nullptr))
      {
        ImGui::SetTooltip("%s at %g: %d of %d finished runs succeeded",
                          test->id.c_str(), static_cast<double>(skill),
                          cell->successes, cell->completed);
      }
    }
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", test->success.describe(test->range).c_str());
  }
  ImGui::EndTable();
}

void TechniqueBenchScene::renderTest()
{
  const BenchResults& run = *shownRun();
  const BenchResults* other = compareRun();
  const auto found = std::ranges::find(catalogue.tests, focus, &BenchTest::id);
  if (found == catalogue.tests.end())
  {
    ImGui::TextDisabled(
        "Click a test (in the list, a curve's point or a grid row).");
    return;
  }
  const BenchTest& test = *found;
  ImGui::TextWrapped("%s  %s", test.id.c_str(), test.name.c_str());
  ImGui::TextDisabled("%s · drill \"%s\" · skill %s", test.section.c_str(),
                      test.drill.c_str(), test.skill.c_str());
  ImGui::TextWrapped("Success: %s", test.success.describe(test.range).c_str());
  // Choices by name, from the drill's own settings.
  std::vector<DrillParameter> parameters;
  for (const auto& drill : watchDrills)
  {
    if (test.drill == drill->name())
    {
      parameters = drill->parameters();
    }
  }
  std::string settings;
  for (const auto& [name, value] : test.settings)
  {
    std::string text = std::format("{:g}", value);
    for (const DrillParameter& parameter : parameters)
    {
      if (name == parameter.name && !parameter.choices.empty() &&
          value >= 0.0F &&
          static_cast<std::size_t>(value) < parameter.choices.size())
      {
        text = parameter.choices[static_cast<std::size_t>(value)];
      }
    }
    settings +=
        std::format("{}{} {}", settings.empty() ? "" : " · ", name, text);
  }
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextDisabled("Settings: %s", settings.c_str());
  std::string tags;
  for (const auto& [key, value] : test.tags)
  {
    tags += std::format("{}{}={}", tags.empty() ? "" : " · ", key, value);
  }
  ImGui::TextDisabled("Tags: %s", tags.c_str());
  ImGui::PopTextWrapPos();

  ImGui::SeparatorText("Watch");
  if (SandboxPageUI::button("Watch", true, Theme::scale(), boldFont))
  {
    watch(test, watchSkill, watchSeed);
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "Opens this test's drill with the selected skill, seed and bench "
        "tuning.");
  }
  if (ImGui::BeginTable("##watch_fields", 2, ImGuiTableFlags_SizingStretchSame,
                        ImVec2(std::min(FIELDS_TABLE_WIDTH * Theme::scale(),
                                        ImGui::GetContentRegionAvail().x),
                               0)))
  {
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    field("Skill");
    ImGui::SliderFloat("##watch_skill", &watchSkill, 1, MAX_WATCH_SKILL,
                       "%.0f");
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(0, 0));
    field("Seed");
    ImGui::InputInt("##watch_seed", &watchSeed);
    ImGui::EndTable();
  }

  // Per skill: the rate, the comparison, why it failed.
  std::vector<const char*> extras;
  for (const char* name : DETAIL_METRICS)
  {
    if (std::ranges::any_of(run.skills,
                            [&](float skill)
                            {
                              const BenchCell* cell = run.cell(test.id, skill);
                              return cell && cell->metrics.contains(name);
                            }))
    {
      extras.push_back(name);
    }
  }
  const int columns = 5 + static_cast<int>(extras.size());
  if (ImGui::BeginTable(
          "##detail", columns,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX,
          ImVec2(0,
                 (static_cast<float>(run.skills.size()) + DETAIL_EXTRA_ROWS) *
                     (ImGui::GetTextLineHeight() +
                      ImGui::GetStyle().CellPadding.y * 2))))
  {
    ImGui::TableSetupColumn("Skill");
    ImGui::TableSetupColumn("Runs");
    ImGui::TableSetupColumn("Success");
    ImGui::TableSetupColumn(
        (other != nullptr) ? std::format("Run {}", compareIndex + 1).c_str()
                           : "Compare");
    ImGui::TableSetupColumn("Tolerance (m)");
    for (const char* name : extras)
    {
      ImGui::TableSetupColumn(name);
    }
    ImGui::PushFont(boldFont, SandboxPageUI::BOLD_FONT_SIZE);
    ImGui::TableHeadersRow();
    ImGui::PopFont();
    for (const float skill : run.skills)
    {
      const BenchCell* cell = run.cell(test.id, skill);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%g", static_cast<double>(skill));
      ImGui::TableNextColumn();
      if (cell == nullptr)
      {
        ImGui::TextDisabled("-");
        continue;
      }
      ImGui::Text("%d / %d", cell->completed, cell->runs);
      ImGui::TableNextColumn();
      const auto rate = cell->rate();
      if (rate)
      {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, rateColour(*rate));
        ImGui::PushStyleColor(ImGuiCol_Text, *rate > DARK_TEXT_RATE
                                                 ? IM_COL32(16, 20, 28, 255)
                                                 : TEXT);
        ImGui::Text("%.0f%%", *rate * 100.0);
        ImGui::PopStyleColor();
      }
      ImGui::TableNextColumn();
      const BenchCell* then =
          (other != nullptr) ? other->cell(test.id, skill) : nullptr;
      if ((then != nullptr) && then->rate() && rate)
      {
        ImGui::Text("%.0f%% (%+.0f)", *then->rate() * 100.0,
                    (*rate - *then->rate()) * 100.0);
      }
      else
      {
        ImGui::TextDisabled("-");
      }
      ImGui::TableNextColumn();
      if (cell->tolerance.count > 0)
      {
        ImGui::Text("%.2f", cell->tolerance.mean());
      }
      else
      {
        ImGui::TextDisabled("-");
      }
      for (const char* name : extras)
      {
        ImGui::TableNextColumn();
        const auto metric = cell->metrics.find(name);
        if (metric == cell->metrics.end() || metric->second.count == 0)
        {
          ImGui::TextDisabled("-");
        }
        else
        {
          ImGui::Text("%.2f", metric->second.mean());
        }
      }
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled(
      "Failed columns are shares of the finished runs (0-1); "
      "misses and speeds are means.");
}

void TechniqueBenchScene::watch(const BenchTest& test, float skill, int seed)
{
  const auto found =
      std::ranges::find_if(watchDrills, [&](const auto& drill)
                           { return test.drill == drill->name(); });
  if (found == watchDrills.end())
  {
    return;
  }
  Drill& drill = **found;
  std::map<std::string, float> values = watchDefaults[&drill];
  for (const auto& [name, value] : test.settings)
  {
    values[name] = value;
  }
  values[test.skill] = std::round(skill);
  for (const DrillParameter& parameter : drill.parameters())
  {
    if ((parameter.value != nullptr) && values.contains(parameter.name))
    {
      *parameter.value = values.at(parameter.name);
    }
  }
  drill.seed = static_cast<std::uint32_t>(std::max(0, seed));
  guiView->overlayScene(
      std::make_unique<DrillScene>(guiView, drill, [](MatchEngine& engine)
                                   { sandboxTuning().applyTo(engine); }));
}

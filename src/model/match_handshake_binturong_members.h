// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong handshake's MatchEngine members (defined in
// match_handshake_binturong.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Binturong handshake ---------------------------------------------------
  /** Match level from both elevens (see MatchTuning::Player). */
  void computeLevelBinturong(const Lineup& home, const Lineup& away);
  /** Rating of an attribute in [0, 1] before the stretch. */
  float rawAttributeBinturong(const Player* player, std::string_view name) const;
  float attributeBinturong(const Player* player, std::string_view name) const;
  void loadAttributesBinturong(MatchPlayer& matchPlayer, const Player* player) const;
  static const DerivedRecipes& derivedRecipesBinturong();
  void initializePlayersBinturong(const Lineup& lineup, bool isHomeTeam);
  // Roles, opposition instructions and team talks (see model/tactics.h).
  /** Reads roles and opposition instructions from a side's tactics. */
  void resolveTacticsBinturong(bool homeTeam);
  /** Base positions of a side's outfield players from its formation slots
   * (a side short of players also sits a little deeper). */
  void placeFormationBinturong(bool homeTeam);
  /** Per-match referee strictness from the draw and the context. */
  float drawRefereeStrictnessBinturong() const;
  /** How clearly each side is the weaker one in [0, 1] (see Touchline). */
  void computeUnderdogSharesBinturong();
  /** The match context, sanitised, and the finishing precision it sets. */
  void convertMatchContextBinturong(const MatchContext& context);
  /** A player's starting condition (0-1) as match stamina. */
  float convertConditionBinturong(float condition) const;
  /** A side's tactical familiarity (0-1). */
  float convertFamiliarityBinturong(float familiarity) const;
  /** A team talk's effect, within its allowed swing. */
  float convertTeamTalkBinturong(float modifier) const;
// clang-format on

-- Leagues table
CREATE TABLE IF NOT EXISTS Leagues (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  parent_league_id INTEGER NULL,
  tiebreak INTEGER NOT NULL DEFAULT 0, -- 0: goal difference first, 1: head-to-head first
  FOREIGN KEY(parent_league_id) REFERENCES Leagues(id)
);

-- Teams table
CREATE TABLE IF NOT EXISTS Teams (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  league_id INTEGER NOT NULL,
  name TEXT NOT NULL UNIQUE,
  balance INTEGER NOT NULL DEFAULT 0,
  strategy TEXT DEFAULT '{}',   -- JSON
  lineup TEXT DEFAULT '{}',     -- JSON
  reputation INTEGER NOT NULL DEFAULT 0,          -- 1-100 (0: legacy save)
  stadium_capacity INTEGER NOT NULL DEFAULT 0,
  ticket_price INTEGER NOT NULL DEFAULT 0,        -- average yield per seat
  training_facilities INTEGER NOT NULL DEFAULT 0, -- 1-100
  youth_facilities INTEGER NOT NULL DEFAULT 0,    -- 1-100
  transfer_budget INTEGER NOT NULL DEFAULT 0,
  wage_budget INTEGER NOT NULL DEFAULT 0,         -- weekly
  recent_form TEXT NOT NULL DEFAULT '',           -- e.g. 'WDLWW', newest first
  FOREIGN KEY(league_id) REFERENCES Leagues(id)
);

-- Players table
CREATE TABLE IF NOT EXISTS Players (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  team_id INTEGER NOT NULL,
  first_name TEXT NOT NULL,
  last_name TEXT NOT NULL,
  age INTEGER NOT NULL DEFAULT 18,
  role TEXT NOT NULL,
  nationality TEXT NOT NULL,
  wage INTEGER NOT NULL DEFAULT 0,
  contract_years INTEGER NOT NULL DEFAULT 1,
  height INTEGER NOT NULL DEFAULT 175,
  foot TEXT NOT NULL DEFAULT 'Right', -- 'Left' or 'Right'
  stats TEXT NOT NULL,                -- JSON string
  status INTEGER DEFAULT 0,           -- bitmask: injured, transfer, etc.
  potential REAL NOT NULL DEFAULT 0,  -- hidden, overall scale (0: legacy)
  traits TEXT NOT NULL DEFAULT '',    -- professionalism,ambition,temperament,loyalty,injury_proneness
  dynamics TEXT NOT NULL DEFAULT '',  -- condition, sharpness, morale, injury, form (CSV)
  squad_number INTEGER NOT NULL DEFAULT 0, -- 1-99, unique per club; 0: none
  nickname TEXT NOT NULL DEFAULT '',
  FOREIGN KEY(team_id) REFERENCES Teams(id)
);

-- Fixtures table for match scheduling
CREATE TABLE IF NOT EXISTS Fixtures (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    game_date TEXT NOT NULL,
    home_team_id INTEGER NOT NULL,
    away_team_id INTEGER NOT NULL,
    match_type INTEGER NOT NULL DEFAULT 0, -- 0: LEAGUE, 1: FRIENDLY, 2: CUP 
    home_goals INTEGER,
    away_goals INTEGER,
    played INTEGER NOT NULL DEFAULT 0,
    competition_id INTEGER NOT NULL DEFAULT 0, -- league ID, or country root league ID for cups
    stage INTEGER NOT NULL DEFAULT 0,          -- league round / cup round (1-based)
    extra_time INTEGER NOT NULL DEFAULT 0,
    home_penalties INTEGER,                    -- NULL unless decided on penalties
    away_penalties INTEGER,
    kickoff INTEGER NOT NULL DEFAULT 0,        -- minutes after midnight; 0: usual time of the competition
    FOREIGN KEY(home_team_id) REFERENCES Teams(id),
    FOREIGN KEY(away_team_id) REFERENCES Teams(id),
    UNIQUE(game_date, home_team_id, away_team_id)
);

-- Game state management
CREATE TABLE IF NOT EXISTS GameState (
  id INTEGER PRIMARY KEY,
  managed_team_id INTEGER,
  game_date TEXT,
  current_season INTEGER
);


-- League points/standings
CREATE TABLE IF NOT EXISTS LeaguePoints (
  league_id INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  points INTEGER NOT NULL DEFAULT 0,
  goal_difference INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(league_id, team_id),
  FOREIGN KEY(league_id) REFERENCES Leagues(id),
  FOREIGN KEY(team_id) REFERENCES Teams(id)
);

-- Structured summary of every played fixture (natural key: date + teams)
CREATE TABLE IF NOT EXISTS MatchReports (
  game_date TEXT NOT NULL,
  home_team_id INTEGER NOT NULL,
  away_team_id INTEGER NOT NULL,
  season INTEGER NOT NULL,
  match_type INTEGER NOT NULL DEFAULT 0,
  competition_id INTEGER NOT NULL DEFAULT 0,
  stage INTEGER NOT NULL DEFAULT 0,
  home_goals INTEGER NOT NULL DEFAULT 0,
  away_goals INTEGER NOT NULL DEFAULT 0,
  extra_time INTEGER NOT NULL DEFAULT 0,
  home_penalties INTEGER,
  away_penalties INTEGER,
  attendance INTEGER NOT NULL DEFAULT 0,
  stats TEXT NOT NULL DEFAULT '{}',   -- JSON team totals
  events TEXT NOT NULL DEFAULT '[]',  -- JSON scorers/cards with minutes
  players TEXT NOT NULL DEFAULT '[]', -- JSON per-player lines
  PRIMARY KEY(game_date, home_team_id, away_team_id)
);
CREATE INDEX IF NOT EXISTS idx_match_reports_season ON MatchReports(season);

-- Competitive per-player totals per season, team and competition type
CREATE TABLE IF NOT EXISTS PlayerSeasonStats (
  season INTEGER NOT NULL,
  player_id INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  competition_type INTEGER NOT NULL,
  appearances INTEGER NOT NULL DEFAULT 0,
  starts INTEGER NOT NULL DEFAULT 0,
  minutes INTEGER NOT NULL DEFAULT 0,
  goals INTEGER NOT NULL DEFAULT 0,
  assists INTEGER NOT NULL DEFAULT 0,
  yellow_cards INTEGER NOT NULL DEFAULT 0,
  red_cards INTEGER NOT NULL DEFAULT 0,
  rating_total REAL NOT NULL DEFAULT 0,
  rated_matches INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(season, player_id, team_id, competition_type)
);

-- Card counts and outstanding suspensions per competition type
CREATE TABLE IF NOT EXISTS PlayerDiscipline (
  player_id INTEGER NOT NULL,
  competition_type INTEGER NOT NULL, -- 0: LEAGUE, 2: CUP
  season_yellows INTEGER NOT NULL DEFAULT 0,
  season_reds INTEGER NOT NULL DEFAULT 0,
  ban_matches INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(player_id, competition_type)
);

-- Final outcome of every competition per season
CREATE TABLE IF NOT EXISTS SeasonHistory (
  season INTEGER NOT NULL,
  start_year INTEGER NOT NULL,
  competition_type INTEGER NOT NULL, -- 0: LEAGUE, 2: CUP
  competition_id INTEGER NOT NULL,
  competition_name TEXT NOT NULL,
  champion_id INTEGER,
  runner_up_id INTEGER,
  promoted TEXT NOT NULL DEFAULT '[]',  -- JSON team IDs promoted out of the league
  relegated TEXT NOT NULL DEFAULT '[]', -- JSON team IDs relegated out of the league
  top_scorer_id INTEGER,
  top_scorer_goals INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(season, competition_type, competition_id)
);

-- Final league tables of past seasons, before promotion and relegation
CREATE TABLE IF NOT EXISTS SeasonTables (
  season INTEGER NOT NULL,
  start_year INTEGER NOT NULL,
  league_id INTEGER NOT NULL,
  position INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  played INTEGER NOT NULL DEFAULT 0,
  won INTEGER NOT NULL DEFAULT 0,
  drawn INTEGER NOT NULL DEFAULT 0,
  lost INTEGER NOT NULL DEFAULT 0,
  goals_for INTEGER NOT NULL DEFAULT 0,
  goals_against INTEGER NOT NULL DEFAULT 0,
  points INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(season, league_id, team_id)
);

-- The managed club's season-end summary and the board's verdict
CREATE TABLE IF NOT EXISTS SeasonReviews (
  season INTEGER PRIMARY KEY,
  start_year INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  league_id INTEGER NOT NULL,
  next_league_id INTEGER NOT NULL DEFAULT 0,
  position INTEGER NOT NULL DEFAULT 0,
  league_size INTEGER NOT NULL DEFAULT 0,
  target_position INTEGER NOT NULL DEFAULT 0,
  objective INTEGER NOT NULL DEFAULT 0, -- BoardObjective
  cup_objective INTEGER NOT NULL DEFAULT 0, -- CupObjective
  finance_objective INTEGER NOT NULL DEFAULT 0, -- FinanceObjective
  youth_target INTEGER NOT NULL DEFAULT 0,
  young_regulars INTEGER NOT NULL DEFAULT 0,
  played INTEGER NOT NULL DEFAULT 0,
  won INTEGER NOT NULL DEFAULT 0,
  drawn INTEGER NOT NULL DEFAULT 0,
  lost INTEGER NOT NULL DEFAULT 0,
  goals_for INTEGER NOT NULL DEFAULT 0,
  goals_against INTEGER NOT NULL DEFAULT 0,
  points INTEGER NOT NULL DEFAULT 0,
  champion INTEGER NOT NULL DEFAULT 0,
  promoted INTEGER NOT NULL DEFAULT 0,
  relegated INTEGER NOT NULL DEFAULT 0,
  cup_won INTEGER NOT NULL DEFAULT 0,
  continental_id INTEGER NOT NULL DEFAULT 0,
  top_scorer TEXT NOT NULL DEFAULT '',
  top_scorer_goals INTEGER NOT NULL DEFAULT 0,
  prize_money INTEGER NOT NULL DEFAULT 0,
  balance INTEGER NOT NULL DEFAULT 0,
  confidence_before REAL NOT NULL DEFAULT 0,
  verdict INTEGER NOT NULL DEFAULT 2, -- SeasonVerdict
  grade_league INTEGER NOT NULL DEFAULT 1, -- ObjectiveGrade
  grade_cup INTEGER NOT NULL DEFAULT 1,
  grade_finances INTEGER NOT NULL DEFAULT 1,
  grade_youth INTEGER NOT NULL DEFAULT 1,
  confidence_after REAL NOT NULL DEFAULT 0,
  next_objective INTEGER, -- NULL after a sacking
  next_target INTEGER NOT NULL DEFAULT 0,
  next_cup INTEGER NOT NULL DEFAULT 0,
  next_finances INTEGER NOT NULL DEFAULT 0,
  next_youth INTEGER NOT NULL DEFAULT 0,
  seen INTEGER NOT NULL DEFAULT 0
);

-- Free agent team
INSERT OR IGNORE INTO Teams (id, league_id, name, balance)
VALUES (0, -1, 'Free agents', -1);

-- Transfer list for the transfer market
CREATE TABLE IF NOT EXISTS TransferList (
    player_id INTEGER PRIMARY KEY,
    asking_price INTEGER NOT NULL DEFAULT 0,
    listing_date TEXT NOT NULL,
    highest_bidder_id INTEGER,
    highest_bid INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(player_id) REFERENCES Players(id)
);

-- Club ledgers: every balance change as a dated transaction
CREATE TABLE IF NOT EXISTS FinanceLedger (
  team_id INTEGER NOT NULL,
  seq INTEGER NOT NULL,          -- position in the club's ledger
  game_date INTEGER NOT NULL,    -- YYYYMMDD
  category INTEGER NOT NULL,     -- FinanceCategory
  amount INTEGER NOT NULL,       -- positive = income
  PRIMARY KEY(team_id, seq)
) WITHOUT ROWID;

-- Managed club inbox (text stored as language keys + JSON arguments)
CREATE TABLE IF NOT EXISTS InboxMessages (
  id INTEGER PRIMARY KEY,
  game_date INTEGER NOT NULL,    -- YYYYMMDD
  category INTEGER NOT NULL,     -- InboxCategory
  title_key TEXT NOT NULL,
  body_key TEXT NOT NULL,
  args TEXT NOT NULL DEFAULT '[]',
  is_read INTEGER NOT NULL DEFAULT 0,
  player_id INTEGER,
  team_id INTEGER
);

-- Board of the managed club (single row)
CREATE TABLE IF NOT EXISTS BoardState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  team_id INTEGER NOT NULL,
  season_year INTEGER NOT NULL,
  objective INTEGER NOT NULL,
  expected_position INTEGER NOT NULL,
  target_position INTEGER NOT NULL,
  confidence REAL NOT NULL,
  low_reviews INTEGER NOT NULL DEFAULT 0,
  league_matches INTEGER NOT NULL DEFAULT 0,
  dismissed INTEGER NOT NULL DEFAULT 0,
  recent_deltas TEXT NOT NULL DEFAULT '[]',
  cup_objective INTEGER NOT NULL DEFAULT 0,     -- CupObjective
  finance_objective INTEGER NOT NULL DEFAULT 0, -- FinanceObjective
  youth_target INTEGER NOT NULL DEFAULT 0,      -- young regulars expected
  start_balance INTEGER NOT NULL DEFAULT 0,     -- cash when the season began
  targets_set INTEGER NOT NULL DEFAULT 0        -- cup/finance/youth set this season
);

-- World simulation state (single row): RNG seed and player and staff id
-- counters (ids are never reused, even after the member leaves)
CREATE TABLE IF NOT EXISTS WorldState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  seed INTEGER NOT NULL,
  next_player_id INTEGER NOT NULL,
  next_staff_id INTEGER NOT NULL DEFAULT 0  -- 0: derive from the stored staff
);

-- Scouting department of the managed club (single row)
CREATE TABLE IF NOT EXISTS ScoutingState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  team_id INTEGER NOT NULL,
  next_assignment_id INTEGER NOT NULL,
  next_report_id INTEGER NOT NULL,
  next_focus_id INTEGER NOT NULL
);

-- Knowledge beyond the relation baseline (own club, league, country)
CREATE TABLE IF NOT EXISTS ScoutingKnowledge (
  player_id INTEGER PRIMARY KEY,
  knowledge REAL NOT NULL,       -- 0-100
  judging_ability INTEGER NOT NULL,
  judging_potential INTEGER NOT NULL,
  last_report_day INTEGER NOT NULL DEFAULT 0  -- day ordinal, 0 = none
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS ScoutAssignments (
  id INTEGER PRIMARY KEY,
  scout_id INTEGER NOT NULL,
  kind INTEGER NOT NULL,         -- ScoutTargetKind
  target_id INTEGER NOT NULL,
  start_date INTEGER NOT NULL,   -- YYYYMMDD
  duration_days INTEGER NOT NULL,
  days_done INTEGER NOT NULL,
  cost INTEGER NOT NULL,
  players_observed INTEGER NOT NULL,
  reports_filed INTEGER NOT NULL,
  finished INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS ScoutReports (
  id INTEGER PRIMARY KEY,
  game_date INTEGER NOT NULL,    -- YYYYMMDD
  player_id INTEGER NOT NULL,
  assignment_id INTEGER NOT NULL,
  scout_id INTEGER NOT NULL,
  scout_name TEXT NOT NULL,
  knowledge INTEGER NOT NULL,
  confidence INTEGER NOT NULL,
  overall REAL NOT NULL,
  potential_low REAL NOT NULL,
  potential_high REAL NOT NULL,
  estimated_fee INTEGER NOT NULL,
  grade INTEGER NOT NULL,        -- ScoutGrade
  reasons INTEGER NOT NULL,      -- bits: 1 fits need, 2 affordable, 4 available
  overall_low REAL NOT NULL DEFAULT 0,   -- 80% range of the estimate
  overall_high REAL NOT NULL DEFAULT 0,
  seen INTEGER NOT NULL DEFAULT 1        -- opened on the scout's page
);

CREATE TABLE IF NOT EXISTS RecruitmentFocus (
  id INTEGER PRIMARY KEY,
  role INTEGER NOT NULL,         -- PlayerRole, UNKNOWN = any
  min_age INTEGER NOT NULL,
  max_age INTEGER NOT NULL,
  max_fee INTEGER NOT NULL,      -- 0 = no limit
  max_wage INTEGER NOT NULL,     -- 0 = no limit
  min_ability INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS ScoutShortlist (
  player_id INTEGER PRIMARY KEY,
  added_date INTEGER NOT NULL,   -- YYYYMMDD
  last_team INTEGER NOT NULL,
  last_flags INTEGER NOT NULL    -- ShortlistFlag bits of the last check
);

-- Scouts' background (keyed by staff id): nationality and languages
CREATE TABLE IF NOT EXISTS ScoutExpertise (
  scout_id INTEGER PRIMARY KEY,
  nationality INTEGER NOT NULL,  -- Language (nationality code)
  languages INTEGER NOT NULL     -- SpokenLanguage bit set
);

-- Days each scout has worked in a league (background + assignments)
CREATE TABLE IF NOT EXISTS ScoutLeagueExperience (
  scout_id INTEGER NOT NULL,
  league_id INTEGER NOT NULL,
  days INTEGER NOT NULL,
  PRIMARY KEY(scout_id, league_id)
) WITHOUT ROWID;

-- Football staff of every club (team_id 0: available on the staff market)
CREATE TABLE IF NOT EXISTS Staff (
  id INTEGER PRIMARY KEY,
  team_id INTEGER NOT NULL,
  first_name TEXT NOT NULL,
  last_name TEXT NOT NULL,
  nationality INTEGER NOT NULL,     -- Language
  age INTEGER NOT NULL,
  role INTEGER NOT NULL,            -- StaffRole
  attributes TEXT NOT NULL,         -- CSV of StaffAttribute values (1-100)
  wage INTEGER NOT NULL DEFAULT 0,  -- weekly
  contract_years INTEGER NOT NULL DEFAULT 0
);

-- Training plan, tactical familiarity and training load of every club
CREATE TABLE IF NOT EXISTS TeamTraining (
  team_id INTEGER PRIMARY KEY,
  preset INTEGER NOT NULL,          -- TrainingPreset
  intensity INTEGER NOT NULL,       -- TrainingIntensity (squad-wide)
  auto_congestion INTEGER NOT NULL,
  schedule TEXT NOT NULL,           -- CSV session,intensity for MD+1..MD-1
  familiarity REAL NOT NULL,        -- 0-100
  tactic TEXT NOT NULL DEFAULT '',  -- CSV snapshot of the drilled tactic
  week_load REAL NOT NULL DEFAULT 0,
  last_week_load REAL NOT NULL DEFAULT 0,
  last_match_day INTEGER NOT NULL DEFAULT 0 -- day ordinal
);

-- Individual training focus and workload of every player
CREATE TABLE IF NOT EXISTS PlayerTraining (
  player_id INTEGER PRIMARY KEY,
  focus INTEGER NOT NULL DEFAULT 0, -- TrainingFocus
  acute REAL NOT NULL DEFAULT 0,    -- 7-day load
  chronic REAL NOT NULL DEFAULT 0,  -- 28-day load
  pending REAL NOT NULL DEFAULT 0,  -- load of the current day
  trend REAL NOT NULL DEFAULT 0     -- smoothed weekly overall change
);

-- Transfer market: every completed move, oldest first (append-only)
CREATE TABLE IF NOT EXISTS TransferHistory (
  seq INTEGER PRIMARY KEY,
  player_id INTEGER NOT NULL,
  game_date INTEGER NOT NULL,     -- YYYYMMDD
  from_team_id INTEGER NOT NULL,
  to_team_id INTEGER NOT NULL,
  fee INTEGER NOT NULL DEFAULT 0, -- fee, loan fee or release compensation
  kind INTEGER NOT NULL           -- TransferKind
);
CREATE INDEX IF NOT EXISTS idx_transfer_history_player ON TransferHistory(player_id);

-- Instalments, add-ons and sell-on clauses still to be paid
CREATE TABLE IF NOT EXISTS TransferObligations (
  id INTEGER PRIMARY KEY,
  kind INTEGER NOT NULL,          -- ObligationKind
  player_id INTEGER NOT NULL,
  payer_id INTEGER NOT NULL,
  payee_id INTEGER NOT NULL,
  amount INTEGER NOT NULL,        -- money, or percent for sell-on clauses
  due_date INTEGER NOT NULL,      -- YYYYMMDD (signing date for conditions)
  target INTEGER NOT NULL DEFAULT 0,
  baseline INTEGER NOT NULL DEFAULT 0
);

-- Active loans (the player sits in the borrower's squad)
CREATE TABLE IF NOT EXISTS Loans (
  player_id INTEGER PRIMARY KEY,
  parent_id INTEGER NOT NULL,
  borrower_id INTEGER NOT NULL,
  start_date INTEGER NOT NULL,
  end_date INTEGER NOT NULL,
  wage_share INTEGER NOT NULL,    -- percent paid by the borrower
  full_wage INTEGER NOT NULL,
  option_fee INTEGER NOT NULL DEFAULT 0,
  obligation INTEGER NOT NULL DEFAULT 0,
  recall_clause INTEGER NOT NULL DEFAULT 0
);

-- Pre-contracts executed on 1 July
CREATE TABLE IF NOT EXISTS PreContracts (
  player_id INTEGER PRIMARY KEY,
  from_team_id INTEGER NOT NULL,
  to_team_id INTEGER NOT NULL,
  agreed_date INTEGER NOT NULL,
  weekly_wage INTEGER NOT NULL,
  years INTEGER NOT NULL,
  signing_bonus INTEGER NOT NULL DEFAULT 0,
  release_clause INTEGER NOT NULL DEFAULT 0,
  promised_role INTEGER           -- SquadRole, NULL = no promise
);

-- Release clauses, playing-time promises and the loan list
CREATE TABLE IF NOT EXISTS PlayerMarketFlags (
  player_id INTEGER PRIMARY KEY,
  release_clause INTEGER NOT NULL DEFAULT 0,
  promised_role INTEGER,
  promise_date INTEGER NOT NULL DEFAULT 0,
  loan_listed INTEGER NOT NULL DEFAULT 0,
  not_for_sale_until INTEGER NOT NULL DEFAULT 0 -- day ordinal, 0 = none
);

-- AI offers for the managed club's players and its own open talks
CREATE TABLE IF NOT EXISTS TransferOffers (
  id INTEGER NOT NULL,
  kind INTEGER NOT NULL,          -- 0 transfer offer, 1 loan offer, 2 talks,
                                  -- 3 a club's ended talks (cooldown until expires)
  player_id INTEGER NOT NULL,
  club_id INTEGER NOT NULL,
  created INTEGER NOT NULL,
  expires INTEGER NOT NULL,
  rounds INTEGER NOT NULL DEFAULT 0,
  terms TEXT NOT NULL DEFAULT '{}', -- JSON offer / loan terms and talks state
  status INTEGER NOT NULL DEFAULT 0,     -- OfferStatus
  respond_on INTEGER NOT NULL DEFAULT 0  -- YYYYMMDD of the buyer's answer
);

-- Rounds of the talks over an incoming offer, oldest first
CREATE TABLE IF NOT EXISTS TransferOfferRounds (
  offer_id INTEGER NOT NULL,
  seq INTEGER NOT NULL,
  game_date INTEGER NOT NULL,       -- YYYYMMDD
  move INTEGER NOT NULL,            -- BuyerNegotiation::Move
  terms TEXT NOT NULL DEFAULT '{}', -- JSON offer terms
  PRIMARY KEY (offer_id, seq)
);

-- Save versioning (owned by src/database/migrations; keep both in sync).
-- New columns on existing tables need a numbered migration there.
CREATE TABLE IF NOT EXISTS schema_migrations (
  number INTEGER PRIMARY KEY,           -- migration number
  name TEXT NOT NULL,                   -- e.g. 0002_league_tiebreak
  applied_at_game_version TEXT NOT NULL,
  checksum TEXT NOT NULL
);

-- Provenance of the save (single row)
CREATE TABLE IF NOT EXISTS save_meta (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  format_id TEXT NOT NULL,
  schema_version INTEGER NOT NULL,      -- highest applied migration
  min_reader_version INTEGER NOT NULL,
  game_version TEXT NOT NULL DEFAULT '',
  engine_version TEXT NOT NULL DEFAULT '',
  rules_edition TEXT NOT NULL DEFAULT '',
  rng_version INTEGER NOT NULL DEFAULT 0,
  sim_version INTEGER NOT NULL DEFAULT 0,
  world_seed INTEGER NOT NULL DEFAULT 0, -- uint64 bit pattern
  seed_unknown INTEGER NOT NULL DEFAULT 0, -- 1: legacy save, seed derived
  tuning_profiles TEXT NOT NULL DEFAULT '[]',
  packs TEXT NOT NULL DEFAULT '[]',
  created_at_utc TEXT NOT NULL DEFAULT '', -- '' for legacy saves
  updated_at_utc TEXT NOT NULL DEFAULT '',
  edited INTEGER NOT NULL DEFAULT 0,
  last_saved_game_date TEXT NOT NULL DEFAULT '',
  playtime_seconds INTEGER NOT NULL DEFAULT 0
);

-- Human side of management (src/model/interactions.*, src/model/stories.*).
-- Day columns are day ordinals (days since 1970-01-01), 0 = never.
CREATE TABLE IF NOT EXISTS InteractionState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  next_promise_id INTEGER NOT NULL,
  last_evaluated_day INTEGER NOT NULL,
  last_request_day INTEGER NOT NULL
);

-- A player's relationship with the manager
CREATE TABLE IF NOT EXISTS PlayerRelations (
  player_id INTEGER PRIMARY KEY,
  trust REAL NOT NULL,                  -- -100..100
  talk_form INTEGER NOT NULL,           -- last conversation per topic group
  talk_playing_time INTEGER NOT NULL,
  talk_promise INTEGER NOT NULL,
  talk_patience INTEGER NOT NULL,
  talk_transfer INTEGER NOT NULL,
  request INTEGER NOT NULL,             -- TalkRequest
  request_day INTEGER NOT NULL,
  quiet_until INTEGER NOT NULL,
  escalations INTEGER NOT NULL,
  low_morale_weeks INTEGER NOT NULL,
  joined_day INTEGER NOT NULL,          -- arrival at the club
  broken_promise_day INTEGER NOT NULL
);

-- Promises made in conversations (active and recently resolved)
CREATE TABLE IF NOT EXISTS Promises (
  id INTEGER PRIMARY KEY,
  player_id INTEGER NOT NULL,
  type INTEGER NOT NULL,                -- PromiseType
  state INTEGER NOT NULL,               -- PromiseState
  void_reason INTEGER NOT NULL,
  made_day INTEGER NOT NULL,
  deadline_day INTEGER NOT NULL,
  resolved_day INTEGER NOT NULL,
  target REAL NOT NULL,
  baseline REAL NOT NULL,
  team_minutes INTEGER NOT NULL,
  player_minutes INTEGER NOT NULL,
  matches INTEGER NOT NULL,
  fulfilled INTEGER NOT NULL
);

-- Stories already told (dedupe and cooldowns)
CREATE TABLE IF NOT EXISTS StoryRecords (
  kind INTEGER NOT NULL,                -- StoryKind
  entity INTEGER NOT NULL,              -- player or club
  story_key INTEGER NOT NULL,
  day INTEGER NOT NULL,
  posted INTEGER NOT NULL
);

-- Stories waiting for the manager's answer
CREATE TABLE IF NOT EXISTS StoryChoices (
  player_id INTEGER PRIMARY KEY,
  kind INTEGER NOT NULL,
  day INTEGER NOT NULL,
  expires_day INTEGER NOT NULL
);

-- Long injuries of the managed squad (comeback stories)
CREATE TABLE IF NOT EXISTS StoryInjuries (
  player_id INTEGER PRIMARY KEY,
  start_day INTEGER NOT NULL,           -- 0 once recovered
  comeback_days INTEGER NOT NULL        -- > 0: story due at next appearance
);

-- Recent bids for managed players (transfer sagas)
CREATE TABLE IF NOT EXISTS StoryBids (
  player_id INTEGER NOT NULL,
  day INTEGER NOT NULL
);

-- Decision moments: open (chosen -1), answered (0 or 1) or lapsed (2)
CREATE TABLE IF NOT EXISTS StoryDilemmas (
  day INTEGER PRIMARY KEY,              -- day ordinal it was raised
  kind INTEGER NOT NULL,                -- StoryKind
  subject INTEGER NOT NULL,             -- player, 0 for club-wide moments
  other INTEGER NOT NULL,               -- second player or rival club
  expires_day INTEGER NOT NULL,
  chosen INTEGER NOT NULL,
  resolved_day INTEGER NOT NULL,
  money INTEGER NOT NULL                -- club balance change applied
);

-- Players the manager follows, with the state last reported
CREATE TABLE IF NOT EXISTS StoryFollows (
  player_id INTEGER PRIMARY KEY,
  since_day INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  injured INTEGER NOT NULL,
  contract_years INTEGER NOT NULL,
  wage INTEGER NOT NULL,
  honours INTEGER NOT NULL,
  last_match_day INTEGER NOT NULL
);

-- Inbox screen filters of the career (single row)
CREATE TABLE IF NOT EXISTS InboxView (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  tab INTEGER NOT NULL,                 -- -1 pick, 0 decisions, 1 information
  category INTEGER NOT NULL,            -- InboxCategory, -1 all
  unread_only INTEGER NOT NULL,
  followed_only INTEGER NOT NULL,
  player_id INTEGER,
  team_id INTEGER
);

-- Manager career: the human manager (single row, only once created)
CREATE TABLE IF NOT EXISTS ManagerProfile (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  first_name TEXT NOT NULL,
  last_name TEXT NOT NULL,
  nationality INTEGER NOT NULL,
  age INTEGER NOT NULL,
  reputation REAL NOT NULL,             -- 1-100
  licence INTEGER NOT NULL,             -- CoachingLicence
  licence_days INTEGER NOT NULL,
  style INTEGER NOT NULL,               -- ManagerStyle
  background INTEGER NOT NULL,          -- ManagerBackground
  club_id INTEGER NOT NULL,             -- 0 while out of work
  contract_wage INTEGER NOT NULL,       -- weekly
  contract_start INTEGER NOT NULL,      -- YYYYMMDD
  contract_expires INTEGER NOT NULL,    -- YYYYMMDD
  release_compensation INTEGER NOT NULL,
  unemployed_since INTEGER NOT NULL,    -- YYYYMMDD
  career_earnings INTEGER NOT NULL,
  seasons_managed INTEGER NOT NULL
);

-- Id counters of the manager market
CREATE TABLE IF NOT EXISTS ManagerMarketState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  next_manager_id INTEGER NOT NULL,
  next_offer_id INTEGER NOT NULL
);

-- Spells of the human manager in charge of a club
CREATE TABLE IF NOT EXISTS ManagerStints (
  seq INTEGER PRIMARY KEY,
  team_id INTEGER NOT NULL,
  club_name TEXT NOT NULL,
  league_id INTEGER NOT NULL,
  start_date INTEGER NOT NULL,
  end_date INTEGER NOT NULL,
  reason INTEGER NOT NULL,              -- DepartureReason
  played INTEGER NOT NULL,
  won INTEGER NOT NULL,
  drawn INTEGER NOT NULL,
  lost INTEGER NOT NULL,
  trophies INTEGER NOT NULL
);

-- League finish of the human manager's club per season
CREATE TABLE IF NOT EXISTS ManagerSeasons (
  seq INTEGER PRIMARY KEY,
  start_year INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  club_name TEXT NOT NULL,
  league_id INTEGER NOT NULL,
  position INTEGER NOT NULL,
  league_size INTEGER NOT NULL,
  expected_position INTEGER NOT NULL
);

-- Honours of the human manager
CREATE TABLE IF NOT EXISTS ManagerAwards (
  seq INTEGER PRIMARY KEY,
  start_year INTEGER NOT NULL,
  kind INTEGER NOT NULL,                -- ManagerAwardKind
  team_id INTEGER NOT NULL,
  club_name TEXT NOT NULL
);

-- Computer managers, employed (team_id) or out of work (team_id = 0)
CREATE TABLE IF NOT EXISTS AiManagers (
  id INTEGER PRIMARY KEY,
  first_name TEXT NOT NULL,
  last_name TEXT NOT NULL,
  nationality INTEGER NOT NULL,
  age INTEGER NOT NULL,
  reputation REAL NOT NULL,
  ability REAL NOT NULL,
  style INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  appointed INTEGER NOT NULL,           -- YYYYMMDD
  matches INTEGER NOT NULL,
  confidence REAL NOT NULL,
  form REAL NOT NULL
);

-- Clubs looking for a manager
CREATE TABLE IF NOT EXISTS ManagerVacancies (
  team_id INTEGER PRIMARY KEY,
  opened INTEGER NOT NULL,
  fill_date INTEGER NOT NULL
);

-- The human manager's job applications
CREATE TABLE IF NOT EXISTS ManagerApplications (
  team_id INTEGER PRIMARY KEY,
  applied INTEGER NOT NULL,
  respond_date INTEGER NOT NULL,
  stage INTEGER NOT NULL                -- ApplicationStage
);

-- Contract offers to the human manager
CREATE TABLE IF NOT EXISTS ManagerJobOffers (
  id INTEGER PRIMARY KEY,
  team_id INTEGER NOT NULL,
  made INTEGER NOT NULL,
  expires INTEGER NOT NULL,
  weekly_wage INTEGER NOT NULL,
  years INTEGER NOT NULL,
  release_compensation INTEGER NOT NULL,
  max_wage INTEGER NOT NULL,
  max_years INTEGER NOT NULL,
  rounds_left INTEGER NOT NULL,
  unsolicited INTEGER NOT NULL,
  compensation INTEGER NOT NULL
);

-- Youth academies: recruitment network, the board-approved project being
-- built and the club's rows in its U18 and U21 leagues this season
CREATE TABLE IF NOT EXISTS YouthAcademies (
  team_id INTEGER PRIMARY KEY,
  recruitment INTEGER NOT NULL,         -- 1-100
  project INTEGER NOT NULL,             -- AcademyUpgrade (0: none)
  project_start_day INTEGER NOT NULL,   -- day ordinals
  project_done_day INTEGER NOT NULL,
  project_target INTEGER NOT NULL,
  last_request_day INTEGER NOT NULL,
  played INTEGER NOT NULL,
  won INTEGER NOT NULL,
  drawn INTEGER NOT NULL,
  lost INTEGER NOT NULL,
  goals_for INTEGER NOT NULL,
  goals_against INTEGER NOT NULL,
  reserve_played INTEGER NOT NULL DEFAULT 0,       -- U21 league row
  reserve_won INTEGER NOT NULL DEFAULT 0,
  reserve_drawn INTEGER NOT NULL DEFAULT 0,
  reserve_lost INTEGER NOT NULL DEFAULT 0,
  reserve_goals_for INTEGER NOT NULL DEFAULT 0,
  reserve_goals_against INTEGER NOT NULL DEFAULT 0,
  reserves_ready INTEGER NOT NULL DEFAULT 0        -- U21 squad set up
);

-- Academy players: intake trialists, U18 and U21 squads, recent graduates
CREATE TABLE IF NOT EXISTS YouthPlayers (
  player_id INTEGER PRIMARY KEY,
  team_id INTEGER NOT NULL,
  status INTEGER NOT NULL,              -- YouthStatus
  contract INTEGER NOT NULL,            -- YouthContract
  joined_age INTEGER NOT NULL,
  appearances INTEGER NOT NULL,         -- U18 matches this season
  minutes INTEGER NOT NULL,
  goals INTEGER NOT NULL,
  rating_total REAL NOT NULL,
  progress TEXT NOT NULL                -- "day:overall*10;" monthly points
);

-- U18 and U21 results of the managed club this season
CREATE TABLE IF NOT EXISTS YouthResults (
  seq INTEGER PRIMARY KEY,
  date INTEGER NOT NULL,                -- YYYYMMDD
  opponent_id INTEGER NOT NULL,
  home INTEGER NOT NULL,
  goals_for INTEGER NOT NULL,
  goals_against INTEGER NOT NULL,
  squad INTEGER NOT NULL DEFAULT 0      -- 0: U18, 1: U21
);

-- The human manager's national-team job (single row) and the id counter of
-- national-team offers
CREATE TABLE IF NOT EXISTS NationalJobState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  active INTEGER NOT NULL,
  nation INTEGER NOT NULL,              -- Language of the national team
  start_date INTEGER NOT NULL,          -- YYYYMMDD
  expires INTEGER NOT NULL,             -- YYYYMMDD
  weekly_wage INTEGER NOT NULL,
  played INTEGER NOT NULL,
  won INTEGER NOT NULL,
  drawn INTEGER NOT NULL,
  lost INTEGER NOT NULL,
  trophies INTEGER NOT NULL,
  qualified INTEGER NOT NULL,           -- reached the finals this cycle
  next_offer_id INTEGER NOT NULL
);

-- Earlier national-team jobs of the human manager
CREATE TABLE IF NOT EXISTS NationalJobHistory (
  seq INTEGER PRIMARY KEY,
  nation INTEGER NOT NULL,
  start_date INTEGER NOT NULL,
  end_date INTEGER NOT NULL,
  reason INTEGER NOT NULL,              -- DepartureReason
  played INTEGER NOT NULL,
  won INTEGER NOT NULL,
  drawn INTEGER NOT NULL,
  lost INTEGER NOT NULL,
  trophies INTEGER NOT NULL
);

-- National teams looking for a head coach
CREATE TABLE IF NOT EXISTS NationalVacancies (
  nation INTEGER PRIMARY KEY,
  opened INTEGER NOT NULL,              -- YYYYMMDD
  fill_date INTEGER NOT NULL
);

-- The human manager's applications for national-team jobs
CREATE TABLE IF NOT EXISTS NationalApplications (
  nation INTEGER PRIMARY KEY,
  applied INTEGER NOT NULL,
  respond_date INTEGER NOT NULL,
  stage INTEGER NOT NULL                -- 0 pending, 1 rejected, 2 offered
);

-- National federations' offers to the human manager
CREATE TABLE IF NOT EXISTS NationalJobOffers (
  id INTEGER PRIMARY KEY,
  nation INTEGER NOT NULL,
  made INTEGER NOT NULL,
  expires INTEGER NOT NULL,
  weekly_wage INTEGER NOT NULL,
  years INTEGER NOT NULL,
  unsolicited INTEGER NOT NULL
);

-- Squad status the manager gave a player; only valid while the player is
-- still at team_id
CREATE TABLE IF NOT EXISTS SquadStatuses (
  player_id INTEGER PRIMARY KEY,
  team_id INTEGER NOT NULL,
  status INTEGER NOT NULL               -- SquadStatus
);

-- Guidance features (src/model/guidance.*): first-week checklist, delegation,
-- opposition instructions and analytics of the managed club's matches
CREATE TABLE IF NOT EXISTS GuidanceState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  onboarding_done INTEGER NOT NULL DEFAULT 0,      -- bit per OnboardingTask
  onboarding_dismissed INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS DelegatedDuties (
  duty INTEGER PRIMARY KEY,             -- Duty
  owner INTEGER NOT NULL                -- DutyOwner
);

CREATE TABLE IF NOT EXISTS OppositionInstructions (
  opponent_id INTEGER NOT NULL,
  player_id INTEGER NOT NULL,
  instruction INTEGER NOT NULL,         -- OppositionInstruction
  PRIMARY KEY (opponent_id, player_id)
);

CREATE TABLE IF NOT EXISTS ManagedMatchAnalytics (
  game_date INTEGER NOT NULL,           -- YYYYMMDD
  home_id INTEGER NOT NULL,
  away_id INTEGER NOT NULL,
  data TEXT NOT NULL,                   -- ManagedMatchSnapshot JSON
  detail BLOB,                          -- MatchDetail (NULL: not tracked)
  PRIMARY KEY (game_date, home_id, away_id)
);

-- Continental club competitions (src/model/continental.*): seasons,
-- knockout ties, draws, coefficients and next season's qualified clubs.
-- Their matches are ordinary Fixtures rows (match_type 3).
CREATE TABLE IF NOT EXISTS ContinentalState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  data TEXT NOT NULL                    -- ContinentalCompetitions JSON
);

-- National teams (src/model/national_teams.*): calendar, squads, finals,
-- ratings, caps and goals of every capped player.
CREATE TABLE IF NOT EXISTS InternationalState (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  data TEXT NOT NULL                    -- NationalTeams JSON
);

-- League honours (src/model/awards.*): history and running tallies
CREATE TABLE IF NOT EXISTS AwardHistory (
  season_year INTEGER NOT NULL,
  month INTEGER NOT NULL,               -- 0 for season awards
  league_id INTEGER NOT NULL,
  type INTEGER NOT NULL,                -- AwardType
  player_id INTEGER NOT NULL,           -- 0 for manager awards
  team_id INTEGER NOT NULL,
  opponent_id INTEGER NOT NULL,
  name TEXT NOT NULL,
  value REAL NOT NULL,
  count INTEGER NOT NULL,
  slot INTEGER NOT NULL,                -- Team of the Season position
  match_date TEXT NOT NULL              -- Goal of the Month only
);
CREATE TABLE IF NOT EXISTS AwardTallies (
  scope INTEGER NOT NULL,               -- 0 month, 1 season
  league_id INTEGER NOT NULL,
  player_id INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  appearances INTEGER NOT NULL,
  minutes INTEGER NOT NULL,
  goals INTEGER NOT NULL,
  assists INTEGER NOT NULL,
  clean_sheets INTEGER NOT NULL,
  rating_total REAL NOT NULL,
  rated INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS AwardClubTallies (
  scope INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  league_id INTEGER NOT NULL,
  matches INTEGER NOT NULL,
  points REAL NOT NULL,
  expected_points REAL NOT NULL,
  goals_for INTEGER NOT NULL,
  goals_against INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS AwardGoals (
  league_id INTEGER PRIMARY KEY,
  match_date TEXT NOT NULL,
  player_id INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  opponent_id INTEGER NOT NULL,
  minute INTEGER NOT NULL,
  score REAL NOT NULL
);

-- Records book (src/model/records.*); RecordMeta marks a built book
CREATE TABLE IF NOT EXISTS RecordMeta (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  version INTEGER NOT NULL,
  season_year INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS RecordEntries (
  scope INTEGER NOT NULL,               -- 0 club, 1 league
  scope_id INTEGER NOT NULL,
  kind INTEGER NOT NULL,                -- RecordKind
  value INTEGER NOT NULL,
  value2 INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  opponent_id INTEGER NOT NULL,
  player_id INTEGER NOT NULL,
  name TEXT NOT NULL,
  match_date TEXT NOT NULL,
  season_year INTEGER NOT NULL,
  goals_for INTEGER NOT NULL,
  goals_against INTEGER NOT NULL,
  home INTEGER NOT NULL,
  PRIMARY KEY(scope, scope_id, kind)
);
CREATE TABLE IF NOT EXISTS ClubPlayerTotals (
  team_id INTEGER NOT NULL,
  player_id INTEGER NOT NULL,
  name TEXT NOT NULL,
  appearances INTEGER NOT NULL,
  goals INTEGER NOT NULL,
  first_year INTEGER NOT NULL,
  last_year INTEGER NOT NULL,
  PRIMARY KEY(team_id, player_id)
);
CREATE TABLE IF NOT EXISTS AllTimeTable (
  league_id INTEGER NOT NULL,
  team_id INTEGER NOT NULL,
  played INTEGER NOT NULL,
  won INTEGER NOT NULL,
  drawn INTEGER NOT NULL,
  lost INTEGER NOT NULL,
  goals_for INTEGER NOT NULL,
  goals_against INTEGER NOT NULL,
  points INTEGER NOT NULL,
  PRIMARY KEY(league_id, team_id)
);
CREATE TABLE IF NOT EXISTS RecordSeason (
  team_id INTEGER PRIMARY KEY,
  league_id INTEGER NOT NULL,
  played INTEGER NOT NULL,
  goals_for INTEGER NOT NULL,
  points INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS RecordSeasonScorers (
  team_id INTEGER NOT NULL,
  player_id INTEGER NOT NULL,
  goals INTEGER NOT NULL,
  PRIMARY KEY(team_id, player_id)
);

-- Facility projects funded by the board (src/model/facility_projects.*)
CREATE TABLE IF NOT EXISTS FacilityProjects (
  id INTEGER PRIMARY KEY,
  team_id INTEGER NOT NULL,
  type INTEGER NOT NULL,                -- FacilityProjectType
  start_date TEXT NOT NULL,
  end_date TEXT NOT NULL,
  cost INTEGER NOT NULL,
  paid INTEGER NOT NULL,
  amount INTEGER NOT NULL,              -- levels or seats
  disruption INTEGER NOT NULL,          -- seats closed during the works
  completed INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS ClubFacilities (
  team_id INTEGER PRIMARY KEY,
  medical INTEGER NOT NULL              -- medical centre level (50 standard)
);
CREATE TABLE IF NOT EXISTS ProjectCooldowns (
  team_id INTEGER NOT NULL,
  type INTEGER NOT NULL,
  until_date TEXT NOT NULL,
  PRIMARY KEY(team_id, type)
);

-- Pre-season plan of the managed club (src/model/preseason.*)
CREATE TABLE IF NOT EXISTS PreseasonPlan (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  season_year INTEGER NOT NULL,
  camp INTEGER NOT NULL,                -- TrainingCamp
  camp_cost INTEGER NOT NULL,
  camp_applied INTEGER NOT NULL,
  tour_matches INTEGER NOT NULL,
  tour_reputation INTEGER NOT NULL,
  camp_end TEXT NOT NULL                -- empty without a camp
);
CREATE TABLE IF NOT EXISTS PreseasonTours (
  match_date TEXT NOT NULL
);

-- Mentoring groups of the managed club (src/model/mentoring.*)
CREATE TABLE IF NOT EXISTS MentoringGroups (
  id INTEGER PRIMARY KEY,
  team_id INTEGER NOT NULL,
  mentor_id INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS MentoringMentees (
  group_id INTEGER NOT NULL,
  player_id INTEGER NOT NULL,
  professionalism_shift REAL NOT NULL,
  temperament_shift REAL NOT NULL,
  professionalism_pending REAL NOT NULL,
  temperament_pending REAL NOT NULL
);

-- Holiday stop rules and delegation (src/model/holiday.*)
CREATE TABLE IF NOT EXISTS HolidayPreferences (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  stop_big_bid INTEGER NOT NULL,
  big_bid_threshold INTEGER NOT NULL,
  stop_sacking_warning INTEGER NOT NULL,
  stop_injury_crisis INTEGER NOT NULL,
  injury_crisis_count INTEGER NOT NULL,
  stop_key_injury INTEGER NOT NULL,
  assistant_lineup INTEGER NOT NULL,
  assistant_training INTEGER NOT NULL,
  assistant_inbox INTEGER NOT NULL
);

-- Medical staff instructions for the managed squad (src/model/medical_centre.*)
CREATE TABLE IF NOT EXISTS MedicalFlags (
  player_id INTEGER PRIMARY KEY,
  flags INTEGER NOT NULL,                -- MedicalFlag bits
  warned_day INTEGER NOT NULL DEFAULT 0  -- day ordinal of the last warning
);

-- Daily training and match load of the managed squad (last 28 days)
CREATE TABLE IF NOT EXISTS MedicalLoad (
  player_id INTEGER NOT NULL,
  day INTEGER NOT NULL,  -- day ordinal
  load REAL NOT NULL,    -- session units
  ratio REAL NOT NULL,   -- acute:chronic workload ratio
  PRIMARY KEY (player_id, day)
);

-- Supporters' mood of the managed club (single row, src/model/supporters.*)
CREATE TABLE IF NOT EXISTS SupporterMood (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  team_id INTEGER NOT NULL,
  mood REAL NOT NULL,              -- 0-100
  updated_day INTEGER NOT NULL,    -- day ordinal of the weekly update
  reasons TEXT NOT NULL DEFAULT '' -- "factor:points" pairs, strongest first
);

-- Enable WAL mode
PRAGMA journal_mode=WAL;

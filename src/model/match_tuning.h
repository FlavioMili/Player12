// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/**
 * Central tuning values for the renderer-independent match simulation.
 *
 * The simulation runs in real match time: one simulated second is one second
 * of the match, speeds are in metres per second, accelerations in m/s^2 and
 * durations in seconds unless a name says otherwise. Pitch positions are
 * metres (x along the 105 m length, home attacking +x; y across the 68 m
 * width). Tactical offsets (Shape) and other `_X`/`_Y`/fraction tuning stay
 * pitch fractions, taken into metres through MatchPitch::at/atX/atY,
 * while radii, reaches and distances compared with the metric distance
 * between players are in metres.
 */
struct MatchTuning final
{
  MatchTuning(const MatchTuning&) = default;
  MatchTuning(MatchTuning&&) = default;
  MatchTuning& operator=(const MatchTuning&) = default;
  MatchTuning& operator=(MatchTuning&&) = default;

  /** A body (collision plan): an upright cylinder 50 cm across (user,
   * 2026-11-04; was 40 cm). The one size everywhere: two touch with their
   * centres a diameter apart, and a body meets the boards, walls and goal
   * frames at its radius. */
  struct Body final
  {
    static constexpr float DIAMETER_METRES = 0.5f;
    static constexpr float RADIUS_METRES = 0.5f * DIAMETER_METRES;
  };

  /**
   * Where his legs naturally are (shot block plan Â§3c, agreed for a first
   * try 2026-11-06): no legs exist until a ball needs them ("quantum
   * legs"); then a point of his stride is rolled by the time spent there.
   * Lengths are for a 1.80 m player and scale with his height.
   */
  struct Stride final
  {
    static constexpr float REFERENCE_HEIGHT_METRES = 1.80f;
    /** Movement types by speed (m/s) and his travel against his facing. */
    static constexpr float STANDING_SPEED = 0.5f;
    static constexpr float WALK_TOP_SPEED = 2.0f;
    static constexpr float JOG_TOP_SPEED = 4.0f;
    static constexpr float RUN_TOP_SPEED = 6.5f;
    static constexpr float SHUFFLE_TOP_SPEED = 3.0f;
    static constexpr float SIDESTEP_DEGREES = 60.0f;
    static constexpr float BACKPEDAL_DEGREES = 120.0f;
    /** Foot to foot at full spread (m); standing, side by side. */
    static constexpr float STANDING_FEET_APART = 0.3f;
    static constexpr float STEP_WALK = 0.7f;
    static constexpr float STEP_JOG = 1.1f;
    static constexpr float STEP_RUN = 1.6f;
    static constexpr float STEP_SPRINT = 2.1f;
    static constexpr float STEP_SIDESTEP = 0.6f;
    static constexpr float STEP_BACKPEDAL = 0.6f;
    /** Running feet land this far apart side to side (m). */
    static constexpr float RUNNING_FEET_APART = 0.1f;
    /** Opening or closing: this share of the full spread. */
    static constexpr float PART_SPREAD_SHARE = 0.6f;
    /** Sidestep feet together (centre to centre, m). */
    static constexpr float SIDESTEP_TOGETHER = 0.12f;
    /** The swing foot's rise (m), walk / jog / run / sprint: opening or
     * closing, and passing under him (heel folding up). */
    static constexpr float SWING_RISE_PART[4] = {0.02f, 0.05f, 0.1f, 0.15f};
    static constexpr float SWING_RISE_PASSING[4] = {0.05f, 0.15f, 0.3f, 0.4f};
    /** Passing: the swing knee this far ahead of the hip, the swing ankle
     * this far behind it (m). */
    static constexpr float PASSING_KNEE_AHEAD = 0.2f;
    static constexpr float PASSING_ANKLE_BEHIND = 0.1f;
    /** Time share at full spread and opening or closing (passing is the
     * rest), walk / jog / run / sprint; sidestep wide / half (together the
     * rest; my placeholder). Backpedal as a walk. */
    static constexpr float SHARE_FULL[4] = {0.30f, 0.35f, 0.40f, 0.45f};
    static constexpr float SHARE_PART[4] = {0.40f, 0.40f, 0.40f, 0.40f};
    static constexpr float SIDESTEP_SHARE_WIDE = 0.35f;
    static constexpr float SIDESTEP_SHARE_HALF = 0.40f;
    /** Noise: on the spread (share), each foot side to side (m), the legs'
     * direction (degrees; the user cut it from 15). */
    static constexpr float NOISE_SPREAD = 0.1f;
    static constexpr float NOISE_SIDEWAYS = 0.05f;
    static constexpr float NOISE_DEGREES = 5.0f;
    /** What a leg is (agreed 2026-11-06): hip joints apart; thigh and shin
     * as rods of their mean thickness (thigh 0.16 m â†’ 0.11 m, shin
     * 0.11 m â†’ 0.07 m); the booted foot a box, heel behind the ankle. */
    static constexpr float HIPS_APART = 0.18f;
    static constexpr float THIGH_RADIUS = 0.068f;
    static constexpr float SHIN_RADIUS = 0.045f;
    static constexpr float FOOT_LENGTH = 0.28f;
    static constexpr float FOOT_WIDTH = 0.10f;
    static constexpr float FOOT_HEIGHT = 0.08f;
    static constexpr float HEEL_BEHIND_ANKLE = 0.07f;
  };

  /**
   * A leg put in a shot's way (shot block plan Â§3d, agreed 2026-11-06):
   * reaction blocks only; his reaction from Defending, the leg's speed and
   * aim from Tackling (each poor at 1, best at 99), out from under his
   * centre, across the ball's path. Placement error to tune in the Shot
   * block drill.
   */
  struct ShotBlock final
  {
    /** From the strike to his leg starting to move (s). */
    static constexpr float REACTION_SECONDS_POOR = 0.35f;
    static constexpr float REACTION_SECONDS_BEST = 0.15f;
    /** From under him to full reach (s); a shorter reach, a share of it. */
    static constexpr float LEG_OUT_SECONDS_POOR = 0.45f;
    static constexpr float LEG_OUT_SECONDS_BEST = 0.25f;
    /** His ankle's reach from his centre (m), on the ground to his knee;
     * above, narrowing to his body (radius + ball) at his hip. */
    static constexpr float REACH_METRES = 1.0f;
    /** Running: full reach up to this share of his top speed, falling to
     * SPRINT_REACH_SHARE of it at top speed (step 2's limit). */
    static constexpr float FULL_REACH_SPEED_SHARE = 0.4f;
    static constexpr float SPRINT_REACH_SHARE = 0.5f;
    /** How far his ankle lands from where he aimed it (m, at most, any
     * direction), with time to spare; RUSHED_ERROR_FACTOR times that with
     * none (the leg arriving just as the ball does, or not at all),
     * straight line between over RUSHED_SECONDS of spare time. */
    static constexpr float ERROR_METRES_POOR = 0.25f;
    static constexpr float ERROR_METRES_BEST = 0.05f;
    static constexpr float RUSHED_ERROR_FACTOR = 2.0f;
    static constexpr float RUSHED_SECONDS = 0.3f;

    /**
     * The meeting (shot block plan step 3, agreed for a first pass
     * 2026-11-07): off the surface it meets, against his moving body. Of
     * its pace straight into the surface this share springs back; of its
     * pace along it, a ball with no spin keeps this share, the grip taking
     * the rest as spin (as the grass does). Torso, thigh, shin, foot, head
     * (the head's as the heading model's spring, Aerial::HEADER_SPRING; its
     * along-kept as a shin's, my placeholder).
     */
    static constexpr float SPRING_BACK[7] = {0.35f, 0.35f, 0.50f, 0.55f, 0.60f, 0.35f, 0.35f};
    static constexpr float ALONG_KEPT[7] = {0.80f, 0.80f, 0.85f, 0.85f, 0.85f, 0.80f, 0.80f};
    /** A leg put out with the foot meeting it (agreed: a little aim): its
     * face turned up to this far (degrees, about the vertical) to send it
     * away from his goal; Tackling 1 to 99 (my placeholder). */
    static constexpr float FOOT_AIM_DEGREES_POOR = 5.0f;
    static constexpr float FOOT_AIM_DEGREES_BEST = 20.0f;

    /** Afterwards (shot block plan step 4, agreed 2026-11-07): how long
     * before he can play the ball again (s). Off his torso or his stride,
     * just the ball bouncing off him; off a leg he put out, until it is
     * back under him (as long as it took to go out) and this more. */
    /** A ball already inside a body when a stretch begins (he stepped into
     * its path, or two bodies closer than their width) meets it only if
     * they are coming together at least this fast (m/s; my choice: a slow
     * drift, as between two heads in a jump, would hold it there). */
    static constexpr float INSIDE_MEETS_SPEED = 1.0f;
    static constexpr float RECOVERY_BODY_SECONDS = 0.2f;
    static constexpr float RECOVERY_LEG_EXTRA_SECONDS = 0.1f;
  };

  /**
   * A keeper's reaction hands (goalkeeper plan step 2, agreed 2026-11-07 as
   * a first pass): standing saves only, no dive yet. Each stat poor at 1,
   * best at 99. Marked placeholders are mine.
   */
  struct KeeperHands final
  {
    /** From the strike to his hands starting to move (s; Reflexes): pure
     * reaction, no reading of the striker. */
    static constexpr float REACTION_SECONDS_POOR = 0.28f;
    static constexpr float REACTION_SECONDS_BEST = 0.16f;
    /** How fast a hand moves out (m/s; Agility). */
    static constexpr float HAND_SPEED_POOR = 4.0f;
    static constexpr float HAND_SPEED_BEST = 7.0f;
    /** Sideways reach from his centre to the hand's centre, as a share of
     * his height (half his span plus a lean), from the ground up to his
     * standing fingertip reach (Aerial::GOALKEEPER_ARM_REACH_RATIO). */
    static constexpr float REACH_HEIGHT_SHARE = 0.65f;
    /** Each shoulder off his centre line (share of height) and its height
     * (Goalkeeper::SHOULDER_HEIGHT_SHARE); a hand reaches from its own
     * shoulder, so the far hand reaches less (placeholder). */
    static constexpr float SHOULDER_OFF_CENTRE_SHARE = 0.11f;
    /** No further behind his centre than this (m; Agility, user 2026-11-07;
     * the figures are my placeholders). */
    static constexpr float BEHIND_METRES_POOR = 0.05f;
    static constexpr float BEHIND_METRES_BEST = 0.25f;
    /** A gloved hand as a ball (m radius), the two side by side this far
     * apart (centres, m), an arm as a rod (m radius). Placeholders. */
    static constexpr float HAND_RADIUS_METRES = 0.09f;
    static constexpr float HANDS_APART_METRES = 0.15f;
    static constexpr float ARM_RADIUS_METRES = 0.045f;
    /** Ready: in front of him at this share of his height, this far ahead
     * and this far either side (m). Placeholders. */
    static constexpr float READY_HEIGHT_SHARE = 0.55f;
    static constexpr float READY_AHEAD_METRES = 0.25f;
    static constexpr float READY_SIDE_METRES = 0.2f;
    /** Where the hand lands from where he aimed it (m at most, any
     * direction; Reflexes), with time to spare; ShotBlock's rushed factor
     * with none. */
    static constexpr float MISS_METRES_POOR = 0.15f;
    static constexpr float MISS_METRES_BEST = 0.03f;
    /** Of its pace straight into a hand, this share springs back
     * (Handling: soft hands to firm wrists). */
    static constexpr float SPRING_BACK_POOR = 0.25f;
    static constexpr float SPRING_BACK_BEST = 0.45f;
    /** Standing fingertip reach, arm straight up, as a share of his height
     * (the step 3 anchors: Aerial Reach is this plus his jump). */
    static constexpr float STANDING_REACH_HEIGHT_SHARE = 1.33f;
    /** Step 4 (user 2026-11-07): only both hands hold a shot. The fastest
     * shot he holds at his chest (m/s of the ball against his hands;
     * Handling), falling in a straight line with his stretch to this share
     * at full stretch. */
    static constexpr float HOLD_SPEED_POOR = 12.0f;
    static constexpr float HOLD_SPEED_BEST = 28.0f;
    static constexpr float HOLD_FULL_STRETCH_SHARE = 0.4f;
    /** Both hands on it: the other hand's centre within reach of the ball's
     * surface by this much (m; placeholder). */
    static constexpr float BOTH_HANDS_SLACK_METRES = 0.03f;
    /** A fumble on any catch (user: very small, more near his limit, still
     * low): this chance, rising in a straight line from this share of his
     * limit to the chance at it (placeholders). */
    static constexpr float FUMBLE_CHANCE = 0.01f;
    static constexpr float FUMBLE_CHANCE_AT_LIMIT = 0.06f;
    static constexpr float FUMBLE_RISES_FROM_SHARE = 0.7f;
    /** A parry: the hand's face turned at most this far (degrees;
     * Handling) to send it as far from the goal frame as it can
     * (placeholders). */
    static constexpr float PARRY_AIM_DEGREES_POOR = 10.0f;
    static constexpr float PARRY_AIM_DEGREES_BEST = 35.0f;
  };

  /**
   * A keeper's dive (goalkeeper plan step 3, agreed 2026-11-07 as a first
   * pass): only for a shot on target whose line passes beyond his standing
   * reach, aimed as his hands are (its real line plus the Reflexes miss).
   * Marked placeholders are mine.
   */
  struct KeeperDive final
  {
    /** Fingertips' reach to the side from where he stood: this share of his
     * height plus up to this much more (m, Agility). */
    static constexpr float REACH_HEIGHT_SHARE = 1.35f;
    static constexpr float REACH_AGILITY_METRES = 0.4f;
    /** Push-off to full stretch (s; Agility); a shorter dive takes its
     * share of it, never less than this share (placeholder). */
    static constexpr float STRETCH_SECONDS_POOR = 0.75f;
    static constexpr float STRETCH_SECONDS_BEST = 0.55f;
    static constexpr float SHORTEST_STRETCH_SHARE = 0.5f;
    /** On the floor, from landing to back on his feet (s; Agility). */
    static constexpr float DOWN_SECONDS_POOR = 3.0f;
    static constexpr float DOWN_SECONDS_BEST = 0.8f;
    /** His legs together as a rod (m radius; placeholder). */
    static constexpr float LEGS_RADIUS_METRES = 0.1f;
    /** A penalty guess (step 9, a blind guess): he dives for a spot this
     * far to the side (just inside the post; as far as his dive reaches),
     * low or high with this chance, at these heights (m; placeholders). */
    static constexpr float PENALTY_SIDE_METRES = 3.2f;
    static constexpr float PENALTY_HIGH_CHANCE = 0.5f;
    static constexpr float PENALTY_LOW_METRES = 0.4f;
    static constexpr float PENALTY_HIGH_METRES = 1.8f;
    /** When he pushes off for it: this long before the strike (s; the
     * typical onset, Dicks et al. 2010 as cited), give or take up to this
     * much either way, drawn per kick (s; Goalkeeping standing in until the
     * brain has Anticipation: the best about on it, the worst anywhere
     * across the reported 50-350 ms range and as late after the strike).
     * Going early is no better: a taker could adjust (user 2026-11-08). The
     * spreads are placeholders. */
    /** The taker's run-up: from when he sets off to the strike (s;
     * placeholder, long enough for the earliest push-off; his run itself
     * not played out). */
    static constexpr float PENALTY_RUN_UP_SECONDS = 0.4f;
    static constexpr float PENALTY_LEAD_SECONDS = 0.05f;
    static constexpr float PENALTY_LEAD_SPREAD_POOR = 0.3f;
    static constexpr float PENALTY_LEAD_SPREAD_BEST = 0.02f;
  };

  /**
   * A keeper's high claim (goalkeeper plan step 7, agreed 2026-11-08): he
   * goes to where the ball comes down through his hands' reach if he gets
   * there first, jumps (his jump from Aerial Reach), and his hands reach
   * for it anywhere within an arm of each shoulder (up, forward, a punch).
   * Marked placeholders are mine.
   */
  struct KeeperClaim final
  {
    /** He punches if an opponent is this close to the ball (m; Command of
     * Area: a better keeper still catches with an attacker closer; user
     * 2026-11-09, placeholder range). */
    static constexpr float PUNCH_NEAR_METRES_POOR = 2.0f;
    static constexpr float PUNCH_NEAR_METRES_BEST = 0.5f;
    /** Both fists driven through it this fast (m/s; Handling; user's
     * choice of my placeholders). */
    static constexpr float PUNCH_SPEED_POOR = 5.0f;
    static constexpr float PUNCH_SPEED_BEST = 8.0f;
  };

  /**
   * A keeper's smother (goalkeeper plan step 8, agreed 2026-11-09): with a
   * loose ball in his area an attacker would reach first on foot, he dives
   * for it (step 3's dive, aimed low at its line) if that gets him there
   * first. Placeholders, mine.
   */
  struct KeeperSmother final
  {
    /** How far ahead he reads its roll (s), at what interval (s). */
    static constexpr float LOOK_SECONDS = 1.5f;
    static constexpr float LOOK_STEP_SECONDS = 0.04f;
    /** He goes as late as he still beats the attacker by no more than this
     * (s): any earlier and he keeps running. */
    static constexpr float DECIDE_SLACK_SECONDS = 0.2f;
    /** Only a ball below this share of his height (a low ball). */
    static constexpr float HIGHEST_HEIGHT_SHARE = 0.5f;
    /** His hands gather a loose ball only at walking pace or slower
     * (Stride::WALK_TOP_SPEED; user 2026-11-09: he can't bend to it
     * running), and only once he has been that slow this long, bending to
     * it (s; user: a gather period; the figure a placeholder). */
    static constexpr float GATHER_SECONDS = 0.3f;
  };

  /**
   * A ball in a keeper's hands (goalkeeper plan step 5, agreed 2026-11-08):
   * saves, claims and gathers; back-passes and goal kicks stay at his feet.
   * Marked placeholders are mine.
   */
  struct KeeperHeld final
  {
    /** Held this share of his height up, this far in front (m). Brought
     * there from where he took it over this long (s; placeholder). */
    static constexpr float HEIGHT_SHARE = 0.55f;
    static constexpr float AHEAD_METRES = 0.25f;
    static constexpr float GATHER_SECONDS = 0.3f;
    /** A ground pass up to this long is rolled, beyond it thrown overarm
     * (m; placeholder). */
    static constexpr float ROLL_LONGEST_METRES = 25.0f;
    /** Released: a roll this high (m), in front; an overarm throw at this
     * share of his height, this far behind his centre (m); a punt struck
     * this high (m), in front (the drop from his hands not played out). */
    static constexpr float ROLL_RELEASE_METRES = 0.15f;
    static constexpr float OVERARM_HEIGHT_SHARE = 1.15f;
    static constexpr float OVERARM_BEHIND_METRES = 0.3f;
    static constexpr float PUNT_STRIKE_METRES = 0.5f;
    /** An overarm throw goes no higher than this (degrees; placeholder,
     * user 2026-11-08: flatter than a lofted pass). */
    static constexpr float THROW_MAX_ANGLE_DEGREES = 25.0f;
    /** His fastest overarm throw (m/s; Throwing): set so his longest throw
     * (at the angle above, from a 1.88 m keeper's release) first comes
     * down 20 m (1) and 50 m (99) from where he let it go, measured in
     * the Keeper throw drill. */
    static constexpr float THROW_SPEED_POOR = 16.5f;
    static constexpr float THROW_SPEED_BEST = 31.5f;
  };

  struct Pitch final
  {
    /** The match pitch: the size every match is played on, and each
     * engine's default (MatchPitch holds the engine's own). */
    static constexpr float MATCH_LENGTH_METRES = 105.0f;
    static constexpr float MATCH_WIDTH_METRES = 68.0f;
    static constexpr float CENTRE = 0.5f;
    /** The centre spot of a match pitch in metres: where points and the
     * ball start before the engine places them. */
    static constexpr float MATCH_CENTRE_X_METRES = MATCH_LENGTH_METRES * CENTRE;
    static constexpr float MATCH_CENTRE_Y_METRES = MATCH_WIDTH_METRES * CENTRE;
    // The goal's posts are Shooting::GOAL_HALF_WIDTH_METRES either side of
    // the centre (Law 1: 7.32 m between the posts).
    /** The keeper's line-up spot, out from his goal line (m). */
    static constexpr float LINEUP_GOALKEEPER_METRES = 4.2f;
    /** The same spot as a share of the length, for the formation frame
     * (handshake line-up, substitutions panel). */
    static constexpr float LINEUP_GOALKEEPER_X_SHARE = 0.04f;
    /** A keeper stays within this of his goal's centre, across (m). */
    static constexpr float GOALKEEPER_SIDE_LIMIT_METRES = 8.16f;
    /** He sweeps a loose ball within this of his goal line (m), further for
     * a sweeper keeper. */
    static constexpr float GOALKEEPER_SWEEP_METRES = 18.9f;
    /** Law 1: the penalty mark 11 m from the goal line, and the penalty area
     * 16.5 m deep (m). A goal kick is placed on the goal area's line
     * (DeadBall::GOAL_AREA_DEPTH_METRES, Law 16: anywhere in the goal area). */
    static constexpr float PENALTY_SPOT_METRES = 11.0f;
    static constexpr float PENALTY_AREA_DEPTH_METRES = 16.5f;
    static constexpr float PENALTY_AREA_HALF_WIDTH_METRES = 20.16f;
    static constexpr float GOAL_AREA_HALF_WIDTH_METRES = 9.16f;
    /** A corner is placed this far in from both the goal line and the
     * touchline, inside the corner arc (Law 17: the 1 m quarter circle; the
     * exact spot is our own choice) (m). */
    static constexpr float CORNER_SPOT_METRES = 0.5f;
    /** Free kicks are kept this far from the goal lines and the touchlines
     * (m). A throw-in is taken from where the ball crossed (Law 15). */
    static constexpr float RESTART_END_MARGIN_METRES = 4.2f;
    static constexpr float RESTART_SIDE_MARGIN_METRES = 2.72f;
    /** The kick-off taker stands this far behind the centre spot (m). */
    static constexpr float KICKOFF_TAKER_BEHIND_METRES = 1.05f;
    /** The formation squeezed into its own half for kick-off: shares of the
     * length, on purpose (they scale with the pitch). */
    static constexpr float KICKOFF_FORMATION_INSET = 0.02f;
    static constexpr float KICKOFF_FORMATION_SCALE = 0.56f;
    /** The pitch edge as a player reads it: where he means to be, receive
     * or put the ball (MatchEngineInternal::clampToPitch), in metres in
     * from the goal lines (END) and the touchlines (SIDE). */
    static constexpr float PLAYER_EDGE_END_METRES = 0.525f;
    static constexpr float PLAYER_EDGE_SIDE_METRES = 0.68f;
    /** The same edge as shares of the pitch, for the formation frame
     * (building the shape, the handshake line-up, the substitutions view). */
    static constexpr float PLAYER_MIN_X_SHARE = 0.005f;
    static constexpr float PLAYER_MAX_X_SHARE = 0.995f;
    static constexpr float PLAYER_MIN_Y_SHARE = 0.01f;
    static constexpr float PLAYER_MAX_Y_SHARE = 0.99f;
    /** The ball, or a point it is aimed at, is kept this far inside the goal
     * lines (END) and the touchlines (SIDE) (m). */
    static constexpr float BALL_INSIDE_END_METRES = 0.105f;
    static constexpr float BALL_INSIDE_SIDE_METRES = 0.068f;
    /** A shot off the woodwork comes back this far in front of the goal
     * line (m). */
    static constexpr float OFF_WOODWORK_METRES = 0.42f;
    /** The advertising boards (a hard wall), out from the touchlines and
     * the goal lines; the 3D view draws them here too. */
    static constexpr float BOARDS_SIDE_METRES = 4.0f;
    static constexpr float BOARDS_END_METRES = 5.0f;
    static constexpr float BOARDS_HEIGHT_METRES = 0.9f;
    /** The stand's front wall (out from the touchlines and the goal lines)
     * and the lower tier's seating rising behind it. The 3D view draws
     * them here too. */
    static constexpr float STAND_SIDE_METRES = 7.0f;
    static constexpr float STAND_END_METRES = 9.0f;
    static constexpr float STAND_WALL_HEIGHT_METRES = 1.3f;
    static constexpr float LOWER_TIER_DEPTH_METRES = 20.0f;
    static constexpr float LOWER_TIER_TOP_METRES = 11.0f;
    /** The rows of crowd the match views show: this deep into the stand.
     * A ball that goes further has gone out of sight. */
    static constexpr float VISIBLE_CROWD_METRES = 4.0f;
    /** The touchline furniture, all on the bottom touchline (y = 1): the
     * tunnel in the stand's front wall on the halfway line, a dugout either
     * side of it (home on the left, x < 0.5) set against the stand wall,
     * and each dugout's technical area (Law 3 guidance: 1 m either side of
     * the seats, to 1 m from the touchline). The boards open in front of
     * the tunnel and of each technical area. MatchGround builds the shapes
     * from these; the views draw them from there too. */
    static constexpr float TUNNEL_WIDTH_METRES = 4.0f;
    static constexpr float TUNNEL_DEPTH_METRES = 8.0f;
    static constexpr float TUNNEL_HEIGHT_METRES = 2.4f;
    static constexpr float DUGOUT_FROM_HALFWAY_METRES = 5.0f;
    static constexpr float DUGOUT_LENGTH_METRES = 12.0f;
    static constexpr float DUGOUT_DEPTH_METRES = 2.0f;
    /** The dugout's perspex front wall, and its roof sloping down towards
     * the pitch (a ball on it rolls off the front). */
    static constexpr float DUGOUT_FRONT_HEIGHT_METRES = 1.2f;
    static constexpr float DUGOUT_ROOF_FRONT_METRES = 2.0f;
    static constexpr float DUGOUT_ROOF_BACK_METRES = 2.4f;
    static constexpr float TECHNICAL_AREA_SIDE_METRES = 1.0f;
    static constexpr float TECHNICAL_AREA_FROM_TOUCHLINE_METRES = 1.0f;
    /** The fourth official's place: on the halfway line, this far out. */
    static constexpr float FOURTH_OFFICIAL_OUT_METRES = 1.0f;
    /** The goal frame as a body meets it: posts (square, this thick, the
     * goal's width inside them), side nets and the back net this far
     * behind the line at the ground. The 3D view draws them here too. */
    static constexpr float GOAL_POST_METRES = 0.12f;
    static constexpr float GOAL_NET_DEPTH_METRES = 2.0f;
  };

  /** Binturong out of play: the ball after it has gone out, until the
   * restart taker has it on the spot. */
  struct DeadBall final
  {
    /** Share of its speed into them a ball keeps off: the padded
     * advertising boards (a hard wall keeps 60-70%, padding 30-40%), the
     * stand's concrete front wall, the goal's nets and its posts. */
    static constexpr float BOARD_RESTITUTION = 0.5f;
    static constexpr float WALL_RESTITUTION = 0.65f;
    static constexpr float NET_RESTITUTION = 0.1f;
    static constexpr float POST_RESTITUTION = 0.7f;
    /** A dugout's perspex (a stiff panel, a little livelier than the
     * padded boards) and the tunnel's concrete. */
    static constexpr float DUGOUT_RESTITUTION = 0.6f;
    static constexpr float TUNNEL_RESTITUTION = WALL_RESTITUTION;
    /** A fan catches a ball up to this far above the seating he stands on. */
    static constexpr float FAN_REACH_METRES = 2.0f;
    /** A throw back (a fan, a steward): this fast (an adult's two-handed
     * throw leaves the hand at 12-18 m/s), from this high above where he
     * stands, landing up to this far from the spot it is aimed at. */
    static constexpr float THROW_SPEED = 15.0f;
    static constexpr float THROW_RELEASE_METRES = 1.8f;
    static constexpr float THROW_ERROR_METRES = 3.0f;
    /** A fan throws it back this long after catching it. */
    static constexpr float CATCH_THROW_SECONDS = 2.0f;
    /** Out of sight: a replacement comes back from the front row after this
     * (a replacement ball usually reaches the pitch in 5-10 s). */
    static constexpr float OUT_OF_SIGHT_SECONDS = 8.0f;
    /** Behind the boards, out of the players' reach, a steward (standing in
     * for ball boys) throws it back once it settles, or after this. */
    static constexpr float STEWARD_SECONDS = 3.0f;
    /** Settled: slower than this on the ground. */
    static constexpr float SETTLED_SPEED = 0.3f;
    /** The taker picks it up (or catches it) within this reach, this low and
     * this slow, and carries it at this height. */
    static constexpr float PICKUP_REACH_METRES = 0.6f;
    static constexpr float PICKUP_MAX_HEIGHT_METRES = 2.0f;
    static constexpr float PICKUP_MAX_SPEED = 8.0f;
    static constexpr float CARRY_HEIGHT_METRES = 1.0f;
    /** A free kick or penalty: the player fetching it plays it along the
     * ground to the spot once it is within his reach this low and this
     * slow; stopping within this of the spot, the taker sets it there. */
    static constexpr float FOOT_PASS_MAX_HEIGHT_METRES = 0.5f;
    static constexpr float FOOT_PASS_MAX_SPEED = 4.0f;
    static constexpr float SPOT_TOLERANCE_METRES = 1.0f;
    /** After a goal: the conceding keeper goes for the ball in the net this
     * long after it went in, carries it out to his six-yard line (this far
     * out) and drop-kicks it at the centre spot: out of his hands from this
     * high, at this angle, landing up to this far from the spot (a keeper's
     * drop-kick carries 50-60 m). */
    static constexpr float GOAL_FETCH_DELAY_SECONDS = 3.0f;
    static constexpr float GOAL_AREA_DEPTH_METRES = 5.5f;
    static constexpr float DROP_KICK_RELEASE_METRES = 1.0f;
    static constexpr float DROP_KICK_ANGLE_DEGREES = 45.0f;
    static constexpr float DROP_KICK_ERROR_METRES = 8.0f;
    /** A throw-in (a Binturong body): the thrower stands this far behind the
     * touchline (his feet on or behind it) and holds it over his head, this
     * far above it and this far in front of him, so it leaves his hands
     * still behind the line. */
    static constexpr float THROW_IN_STAND_BEHIND_METRES = 0.4f;
    static constexpr float THROW_IN_ABOVE_HEAD_METRES = 0.25f;
    static constexpr float THROW_IN_FORWARD_METRES = 0.1f;
    /** He throws once within this of his spot (or once the throw is this
     * long overdue). */
    static constexpr float THROW_IN_SET_METRES = 0.15f;
    static constexpr float THROW_IN_SET_LIMIT_SECONDS = 5.0f;
    /** He lifts it from carrying height to over his head in this long. */
    static constexpr float THROW_IN_LIFT_SECONDS = 0.5f;
    /** Ready for the restart (Binturong): everyone within this of his
     * restart position, or no closer to it for this long (blocked, or
     * marking); the laws' distances kept, to within this. */
    static constexpr float READY_METRES = 1.0f;
    static constexpr float READY_STALLED_SECONDS = 2.0f;
    static constexpr float READY_SLACK_METRES = 0.3f;
    /** Pushed back from the ball (or out of the area) this far past the
     * laws' line, so that walking up to it keeps it. */
    static constexpr float KEEP_OUT_MARGIN_METRES = 0.5f;
    /** Once everyone is ready, the taker looks up for between these (not
     * in a short match). */
    static constexpr float THROW_IN_PAUSE_MIN_SECONDS = 1.0f;
    static constexpr float THROW_IN_PAUSE_MAX_SECONDS = 2.0f;
    static constexpr float KICK_PAUSE_MIN_SECONDS = 2.0f;
    static constexpr float KICK_PAUSE_MAX_SECONDS = 4.0f;
    static constexpr float CORNER_PAUSE_MIN_SECONDS = 3.0f;
    static constexpr float CORNER_PAUSE_MAX_SECONDS = 5.0f;
    static constexpr float PENALTY_PAUSE_MIN_SECONDS = 2.0f;
    static constexpr float PENALTY_PAUSE_MAX_SECONDS = 3.0f;
    /** A safety net: a restart not ready this long after the ball is back
     * on the spot is taken anyway. */
    static constexpr float READY_LIMIT_SECONDS = 30.0f;
    /** Goal to kick-off: the celebration (the walk back and the ball's
     * return then wait for readiness). */
    static constexpr float CELEBRATION_SECONDS = 25.0f;
    /** A safety net: a ball still not back on the spot after this is put
     * there, so it never stops the match. */
    static constexpr float GIVE_UP_SECONDS = 90.0f;
  };

  /** A training pitch's coach's serve (training ground plan section 20).
   * Our own estimates, agreed with the user; not researched. */
  struct Serve final
  {
    /** The coach's station: this far outside the wall, at the middle of a
     * long side. */
    static constexpr float STATION_OUT_METRES = 1.0f;
    /** The served ball starts this far inside the wall in front of him. */
    static constexpr float START_INSIDE_METRES = 0.5f;
    /** After a whistle, the wait before he serves (after a goal he serves
     * when the goal's stop is over). */
    static constexpr float WAIT_SECONDS = 2.0f;
    /** His passing, for the pace of the serve: the stand-in staff value. */
    static constexpr float COACH_PASSING = 0.5f;
    /** While he waits to serve, the ball lies this far in front of him
     * (towards the wall). */
    static constexpr float BALL_AHEAD_METRES = 0.6f;
    /** His serve: a step this long towards the ball over the last moments
     * before it, and back after. */
    static constexpr float STEP_METRES = 0.25f;
    static constexpr float STEP_SECONDS = 0.5f;
  };

  /** Training dummies (training ground plan section 21; their sizes are in
   * PitchKit::dummyShape). How much of the ball's speed into one it keeps
   * coming back off it: our own estimates, agreed with the user; not
   * researched (the posts' restitution is the only measured one). */
  struct Dummy final
  {
    static constexpr float MANNEQUIN_RESTITUTION = 0.4f;
    static constexpr float POLE_RESTITUTION = 0.3f;
  };

  struct Timing final
  {
    /** Decision and body-physics step (10 Hz). */
    static constexpr float FIXED_STEP_SECONDS = 0.1f;
    /**
     * Team shape and off-ball targets refresh every this many steps (5 Hz),
     * and at once whenever possession or the ball's flight changes; bodies
     * move every step. Running load is accounted at the same rate.
     */
    static constexpr std::uint64_t TACTICAL_REFRESH_STEPS = 2;
    /** The free ball is integrated in this many sub-steps per fixed step. */
    static constexpr int BALL_SUBSTEPS = 4;
    /**
     * MatchFidelity::BACKGROUND merges up to this many fixed steps into one
     * while the ball is dead before a throw-in, goal kick, corner, free kick
     * or penalty (players only walk to their restart spots). Live play keeps
     * the 10 Hz step: a coarser one measurably changes shots and goals.
     */
    static constexpr std::uint32_t BACKGROUND_STOPPAGE_TICKS = 3;
    static constexpr float SECONDS_PER_MINUTE = 60.0f;
    static constexpr int MIN_PLAY_HALF_MINUTES = 3;
    static constexpr int MAX_PLAY_HALF_MINUTES = 15;
    static constexpr int DEFAULT_PLAY_HALF_MINUTES = 5;
    static constexpr float PLAY_STOPPAGE_SECONDS = 3.0F;
    static constexpr float RESTART_SUPPORT_DISTANCE_METRES = 30.0F;
    static constexpr float RESTART_SUPPORT_BLEND = 0.35F;
    static constexpr float THROW_OPPONENT_DISTANCE_METRES = 2.0F;
    static constexpr float KICK_OPPONENT_DISTANCE_METRES = 9.15F;
    // update() bounds catch-up work so a stalled render frame cannot freeze
    // the live view; remaining whole steps are dropped. Headless code uses
    // advance()/simulateToEnd(), which never drop steps.
    static constexpr float MAX_FRAME_DELTA_SECONDS = 4.0f;
    static constexpr int MAX_FIXED_STEPS_PER_UPDATE = 30;
    static constexpr float HALF_TIME_MINUTE = 45.0f;
    static constexpr float FULL_TIME_MINUTE = 90.0f;
    /** Knockout ties still level play two halves of extra time. */
    static constexpr float EXTRA_TIME_HALF_MINUTES = 15.0f;
    /** Breaks before extra time and at its half-time (clock stopped). */
    static constexpr float EXTRA_TIME_BREAK_SECONDS = 10.0f;
    static constexpr float EXTRA_TIME_HALF_TIME_SECONDS = 5.0f;
    /** Penalty shootout pacing: before the first kick, between kicks, and
     * the longest a kick can travel before it counts as missed. */
    static constexpr float SHOOTOUT_START_SECONDS = 20.0f;
    static constexpr float SHOOTOUT_KICK_INTERVAL_SECONDS = 12.0f;
    static constexpr float SHOOTOUT_MAX_FLIGHT_SECONDS = 3.0f;
    /** Kicks per side before sudden death. */
    static constexpr int SHOOTOUT_KICKS = 5;
    /** Interval in the tunnel; the clock is stopped and nothing is live. */
    static constexpr float HALF_TIME_PAUSE_SECONDS = 10.0f;
    // Restart delays from the ball going dead to the restart (real averages:
    // throw-in ~18 s, goal kick ~30 s, corner ~37 s), slightly longer here
    // because fewer balls go out of play than in a real match.
    static constexpr float KICKOFF_DELAY_SECONDS = 4.0f;
    /** A kick-off after a goal waits for everyone to be in his own half
     * (opponents outside the centre circle), but no longer than this past
     * its delay: a safety net, so a player who cannot get there never
     * stops the match. */
    static constexpr float KICKOFF_WALK_IN_LIMIT_SECONDS = 30.0f;
    static constexpr float THROW_IN_DELAY_SECONDS = 21.0f;
    static constexpr float GOAL_KICK_DELAY_SECONDS = 33.0f;
    static constexpr float CORNER_DELAY_SECONDS = 40.0f;
    static constexpr float FREE_KICK_DELAY_SECONDS = 22.0f;
    static constexpr float SET_PIECE_FREE_KICK_DELAY_SECONDS = 28.0f;
    static constexpr float PENALTY_DELAY_SECONDS = 55.0f;
    static constexpr float CARD_DELAY_SECONDS = 15.0f;
    static constexpr float INJURY_DELAY_SECONDS = 50.0f;
    static constexpr float SUBSTITUTION_DELAY_SECONDS = 9.0f;
    /** Goal to kick-off: celebration and walk back (clock running). */
    static constexpr float GOAL_CELEBRATION_SECONDS = 55.0f;
    static constexpr float POSSESSION_TRANSITION_SECONDS = 5.0f;
    static constexpr float RATING_REFRESH_SECONDS = 6.0f;
    static constexpr std::size_t MAX_EVENTS = 1024;
  };

  /**
   * Playback helpers for the live view. The simulation itself never depends
   * on them: the viewer chooses how many simulated seconds pass per wall
   * second (1 = real time), and highlight playback skips (simulates
   * headless) between predicted highlight windows.
   */
  struct Playback final
  {
    static constexpr float MIN_SPEED = 1.0f;
    static constexpr float MAX_SPEED = 30.0f;
    static constexpr float DEFAULT_SPEED = 1.0f;
    static constexpr float DEFAULT_HIGHLIGHT_SPEED = 1.0f;
    /** Build-up shown before a highlight trigger (shot, card, penalty...). */
    static constexpr float HIGHLIGHT_LEAD_SECONDS = 15.0f;
    /** A build-up that starts with a restart opens this long before the
     * ball is played (the setup before it is dead time). */
    static constexpr double RESTART_LEAD_SECONDS = 1.2;
    /** Aftermath shown once the trigger happened. */
    static constexpr float HIGHLIGHT_TAIL_SECONDS = 6.0f;
    /** How far ahead a highlight prediction simulates before giving up. */
    static constexpr float PREDICTION_HORIZON_SECONDS = 900.0f;
    /** Highlight windows closer than this are merged into one. */
    static constexpr float MERGE_GAP_SECONDS = 4.0f;
  };

  /**
   * Player kinematics: an in-situ acceleration-speed profile
   * a(v) = A0 (1 - v / vmax) for speeding up, a stronger braking capacity and
   * a speed-dependent lateral (centripetal) limit so sharp turns at speed
   * force a player to slow down first.
   */
  struct Player final
  {
    static constexpr float DEFAULT_ATTRIBUTE = 0.5f;
    /** Binturong body: a touch or strike with his weaker foot has its error
     * (fail chances and jitter) raised by this share, at Technique 0, less
     * the better his technique (none at 1). */
    static constexpr float WEAKER_FOOT_ERROR_GAIN = 0.45f;
    static constexpr float RATING_SCALE = 100.0f;
    // Attributes are stretched around a typical professional level so that
    // quality differences between squads matter as much as in real results;
    // the stretch saturates far from it so extreme gaps stay plausible.
    static constexpr float ATTRIBUTE_PIVOT = 0.66f;
    static constexpr float ATTRIBUTE_CONTRAST = 3.6f;
    /**
     * The absolute level of a match (mean raw outfield attribute of both
     * elevens) is partly normalised toward the reference level before the
     * stretch, so quality gaps decide as before but a weaker league does not
     * turn into a shooting gallery; the level shows in finishing precision
     * instead (per unit of raw level).
     */
    static constexpr float REFERENCE_LEVEL = 0.65f;
    static constexpr float LEVEL_NORMALISATION = 0.7f;
    static constexpr float LEVEL_FINISHING_GAIN = 2.0f;
    /** Above the reference finishing improves more slowly. */
    static constexpr float ELITE_FINISHING_GAIN = 1.0f;
    static constexpr float MIN_LEVEL_PRECISION = 0.6f;
    static constexpr float ATTRIBUTE_SATURATION = 0.38f;
    static constexpr float MIN_ATTRIBUTE = 0.02f;
    // Real top speeds cluster at 8-9 m/s with a record near 10.4 m/s.
    static constexpr float TOP_SPEED_BASE = 7.4f;
    static constexpr float TOP_SPEED_PACE = 2.6f;
    // Theoretical maximum acceleration A0 (~7 m/s^2 for a typical pro).
    static constexpr float ACCELERATION_BASE = 5.7f;
    static constexpr float ACCELERATION_PACE = 1.6f;
    static constexpr float ACCELERATION_PHYSICALITY = 0.6f;
    static constexpr float BRAKING_BASE = 6.0f;
    static constexpr float BRAKING_PHYSICALITY = 2.0f;
    /** Lateral acceleration a runner sustains at a standstill / top speed. */
    static constexpr float LATERAL_ACCELERATION_SLOW = 9.0f;
    static constexpr float LATERAL_ACCELERATION_FAST = 5.5f;
    /** A turn through a waypoint of more than this (degrees) is a sharp
     * reversal: no corner, he stops on the waypoint (braking as late as he
     * can) and turns there, as athletes plant and turn rather than curve
     * round (the angle-velocity trade-off of change-of-direction work, e.g.
     * Dos'Santos et al. 2018; the threshold a design choice). */
    static constexpr float SHARP_REVERSAL_DEGREES = 135.0f;
    /** The plant (sharp reversal): his last PLANT_METRES into the stop are
     * a planted leg's braking, harder than his run's by a share set by
     * Agility (low at 1, high at 99), and he brakes later for it; as he
     * plants, his body twists toward the way out, this far (degrees, by
     * Agility) off his run. Pushing off out of it, his acceleration is up by
     * the same kind of share (by Agility) for PUSH_OFF_SECONDS. All design
     * choices: change-of-direction work has the plant's braking spike and
     * push-off, not these sizes for a body model like this. */
    static constexpr float PLANT_METRES = 1.5f;
    static constexpr float PLANT_BRAKING_GAIN_LOW = 0.25f;
    static constexpr float PLANT_BRAKING_GAIN_HIGH = 0.75f;
    static constexpr float PLANT_TWIST_DEGREES_LOW = 45.0f;
    static constexpr float PLANT_TWIST_DEGREES_HIGH = 90.0f;
    static constexpr float PUSH_OFF_SECONDS = 0.3f;
    static constexpr float PUSH_OFF_GAIN_LOW = 0.25f;
    static constexpr float PUSH_OFF_GAIN_HIGH = 0.75f;
    /** Desired-speed approach to a target (comfortable deceleration). */
    static constexpr float ARRIVAL_DECELERATION = 2.0f;
    static constexpr float ARRIVAL_DEAD_ZONE_METRES = 0.35f;
    static constexpr float FACING_TURN_RATE_RADIANS = 9.0f;
    static constexpr float MOVEMENT_FACING_THRESHOLD = 0.3f;
    /** Orientation plan, stage 1 (Binturong body). How fast his body turns
     * (rad/s), set by Agility: a 180 deg spin in 0.35 s to 0.17 s, from the
     * part-timer to the elite anchor ratings (a design choice: no research
     * figure for turning on the move; tried at 6.3-12.6 first, which looked
     * slow). */
    static constexpr float BODY_TURN_RATE_LOW = 9.0f;
    static constexpr float BODY_TURN_RATE_HIGH = 18.0f;
    /** Binturong body: Agility's boost to his acceleration, as a share of
     * the Acceleration stat's range (1 to 99), none at Agility 1 up to this
     * at 99 (a design choice: quick feet help him get going, a little). */
    static constexpr float AGILITY_ACCELERATION_BOOST = 0.05f;
    /** Binturong body: his hardest braking (m/s^2), set by Agility, at
     * Agility 1 and 99 (the handshake's Physicality range, 6-8, kept:
     * braking peaks of 6-8 m/s^2, Harper et al. 2020). */
    static constexpr float BRAKING_AT_AGILITY_1 = 6.0f;
    static constexpr float BRAKING_AT_AGILITY_99 = 8.0f;
    /** Binturong body: Acceleration's boost to his braking, as a share of
     * the braking range above, none at Acceleration 1 up to this at 99. */
    static constexpr float ACCELERATION_BRAKING_BOOST = 0.05f;
    /**
     * Moving at an angle to his body (orientation plan): the eight
     * directions, as the angle between body and run, from 0 (forward) by
     * 45 deg steps to 180 (backward), the same to his left and right.
     * Angles between two of them blend the two. For each direction:
     * - the top band he has (MovementMode: 3 sprint, 2 run, 1 jog);
     * - his speed as a share of the same forward band;
     * - his drive (acceleration) as a share;
     * - his energy per metre against forward running.
     * Sources:
     * - backward and sideways at 0.7, walk and jog only (the user's call):
     *   backward 1.3-1.5x the time over 5-20 m (Sammoud et al. 2023), the
     *   100 m backward record ~70% of the forward record's speed;
     *   sideways provisional, no measured figure;
     * - energy x1.3: 20-40% more for high-intensity backward and lateral
     *   movement (the review cited by Bloomfield et al. 2007);
     * - the diagonals harder than their neighbours (the user): 0.6 and x1.4,
     *   the forward diagonal up to a run (design choices, to judge on
     *   screen);
     * - the drive shares (design choices).
     */
    static constexpr std::array<int, 5> DIRECTION_TOP_BAND{3, 2, 1, 1, 1};
    static constexpr std::array<float, 5> DIRECTION_SPEED_SHARE{1.0f, 0.6f, 0.7f,
                                                                0.6f, 0.7f};
    static constexpr std::array<float, 5> DIRECTION_DRIVE_SHARE{1.0f, 0.6f, 0.7f,
                                                                0.6f, 0.7f};
    static constexpr std::array<float, 5> DIRECTION_ENERGY{1.0f, 1.4f, 1.3f,
                                                           1.4f, 1.3f};
    /** Turning his body to a facing asked of him by more than this, he
     * cannot drive: he glides on his momentum (the user's model of a
     * transition). Within it he starts to push, more as the turn
     * completes, as a player drives off the back foot through the last of a
     * turn (a design choice; tried at 20 deg first, a standstill through
     * nearly all of a half-turn). */
    static constexpr float BODY_TURN_DRIVE_RADIANS = 1.5708f;
    static constexpr float MINIMUM_STAMINA = 0.35f;
    // Fatigue lowers top speed far more than the first-step push.
    static constexpr float FATIGUE_TOP_SPEED_LOSS = 0.14f;
    static constexpr float FATIGUE_ACCELERATION_LOSS = 0.06f;
    /** Tired players also choose to run less hard (work-rate loss). */
    static constexpr float FATIGUE_WORK_RATE_LOSS = 0.12f;
    /** Top speed kept with an empty repeat-sprint reserve. */
    static constexpr float EMPTY_RESERVE_TOP_SPEED = 0.80f;
    // Intent speed caps as a share of the fresh top speed.
    static constexpr float HOLD_SHAPE_SPEED_SCALE = 0.36f;
    static constexpr float CARRY_BALL_SPEED_SCALE = 0.60f;
    static constexpr float SUPPORT_SPEED_SCALE = 0.46f;
    static constexpr float ATTACKING_RUN_SPEED_SCALE = 0.95f;
    static constexpr float PRESS_SPEED_SCALE = 0.88f;
    static constexpr float COVER_SPEED_SCALE = 0.55f;
    static constexpr float MARKING_SPEED_SCALE = 0.52f;
    static constexpr float RECOVERY_SPEED_SCALE = 0.58f;
    static constexpr float GOALKEEPER_MOVEMENT_SPEED_SCALE = 0.7f;
    static constexpr float RESTART_WALK_SPEED_SCALE = 0.22f;
    /** Binturong body, going to a restart position: he walks (the top of
     * his walk band), or jogs (the top of his jog band) while walking the
     * rest would take longer than this (the user's call). */
    static constexpr float RESTART_JOG_AFTER_SECONDS = 3.0f;
    /** A restart taker who does not line the ball up (a throw-in, a keeper)
     * is at it within this: his arrival dead zone, and a little. */
    static constexpr float RESTART_ARRIVED_METRES = 0.5f;
    /** Distance from the target at which a player runs at the urgency cap. */
    static constexpr float URGENCY_DISTANCE_METRES = 25.0f;
    static constexpr float MAX_URGENCY_SPEED_SCALE = 0.56f;
    /** Perception/intent lag: how quickly a moving target is followed. */
    static constexpr float TACTICAL_TARGET_RESPONSE_PER_SECOND = 1.8f;
    static constexpr float URGENT_TARGET_RESPONSE_PER_SECOND = 4.5f;
    /** Response lost by a side with no familiarity with its tactics. */
    static constexpr float FAMILIARITY_RESPONSE_LOSS = 0.3f;
    /** Two bodies of ~0.4 m radius cannot overlap. */
    static constexpr float MINIMUM_BODY_SEPARATION_METRES = 0.8f;
    static constexpr float BODY_SEPARATION_SHARE = 0.5f;
    static constexpr float BODY_COLLISION_VELOCITY_RETAINED = 0.9f;
    /** Collision plan step 1: his weight (no weight stat yet) is his height
     * squared times a body-mass index from Strength, 17 at Strength 1 to 31
     * at 99 (a straight line). Crouch 2.01 m / ~75 kg is BMI 18.6, a typical
     * pro ~23, Adama Traore ~27. A heavy but weak player comes out light. */
    static constexpr float BODY_MASS_INDEX_AT_STRENGTH_1 = 17.0f;
    static constexpr float BODY_MASS_INDEX_AT_STRENGTH_99 = 31.0f;
    static constexpr float SUBSTITUTION_SETTLE_SECONDS = 1.5f;
    static constexpr float MAX_LOOSE_BALL_LOOKAHEAD_SECONDS = 1.2f;
    static constexpr float COVER_LOOSE_BALL_OFFSET_METRES = 5.775f;
    static constexpr float PASS_RECEIVER_LOOKAHEAD_SECONDS = 0.8f;
    static constexpr float MINIMUM_PURSUIT_SPEED = 1.4f;
    static constexpr float CURRENT_VELOCITY_PURSUIT_WEIGHT = 0.55f;
    /** Teleports (restart repositioning) are not counted as running. */
    static constexpr float MAX_COUNTED_STEP_SPEED = 13.0f;
  };

  /**
   * Collisions (Binturong body, both players; Notes and
   * plans/match_engine_collision_plan.md): two players running into each
   * other. Contact changes velocity, never position.
   */
  struct Collision final
  {
    /** Personal space (the Binturong brain's overtaking and way round): the
     * closest he plans to pass another player, centre to centre. A
     * team-mate close (the bodies and a gap between them; collision plan
     * step 7, user); an opponent at walkers' ~1 m (Olivier et al. 2012). */
    static constexpr float OVERTAKE_OPPONENT_CLEARANCE_METRES = 1.0f;
    static constexpr float OVERTAKE_TEAM_MATE_GAP_METRES = 0.2f;
    static constexpr float OVERTAKE_TEAM_MATE_CLEARANCE_METRES =
        Body::DIAMETER_METRES + OVERTAKE_TEAM_MATE_GAP_METRES;
    /** Kept his feet and balance: no drive, steering or braking for this
     * long per m/s of jolt (a pushed person's legs act after ~0.2-0.3 s). */
    static constexpr float PAUSE_SECONDS_PER_JOLT = 0.1f;
    /** Through the pause he slows as a runner who has stopped running (the
     * body's arrival deceleration). */
    static constexpr float PAUSE_DECELERATION = 2.0f;
    /** Bodies left overlapping (a pile-up) are eased apart at this speed
     * (m/s) per metre of overlap: 1 m/s per 0.1 m. */
    static constexpr float OVERLAP_EASING_PER_SECOND = 10.0f;
    /** How sudden a jolt is: against his hardest braking over this long
     * (react, ~0.2-0.3 s, and get a recovery step down). */
    static constexpr float SUDDEN_WINDOW_SECONDS = 0.5f;
    /** Contacts resolved within one step at most (each a pair's first
     * touch in what is left of the step). */
    static constexpr int MAX_CONTACTS_PER_STEP = 32;
    /** Judging a knock: the worse of where he ends up and how sudden it
     * was, against two lines Balance moves (straight lines from 1 to 99):
     * below the first he keeps his feet and balance, from it he stumbles,
     * from the second he falls. */
    static constexpr float STUMBLE_LINE_AT_BALANCE_1 = 0.65f;
    static constexpr float STUMBLE_LINE_AT_BALANCE_99 = 0.85f;
    static constexpr float FALL_LINE_AT_BALANCE_1 = 0.90f;
    static constexpr float FALL_LINE_AT_BALANCE_99 = 1.10f;
    /** Already stumbling, a knock is judged against lines this much lower. */
    static constexpr float STUMBLING_LINE_DROP = 0.25f;
    /** The wobble on the score (a bell curve, its typical size), from
     * Balance 1 to 99 (to try). */
    static constexpr float WOBBLE_AT_BALANCE_1 = 0.15f;
    static constexpr float WOBBLE_AT_BALANCE_99 = 0.05f;
    /** The chance a stumble slips into a fall, at Balance 1, 50 and 99
     * (straight lines between; to try). */
    static constexpr float SLIP_AT_BALANCE_1 = 0.20f;
    static constexpr float SLIP_AT_BALANCE_50 = 0.10f;
    static constexpr float SLIP_AT_BALANCE_99 = 0.03f;
    /** A stumble lasts this long just over the stumble line, up to the
     * longest just short of the fall line (one to three recovery steps). */
    static constexpr float STUMBLE_SHORTEST_SECONDS = 0.5f;
    static constexpr float STUMBLE_LONGEST_SECONDS = 1.0f;
    /** His recovery steps brake him at this share of his hardest braking. */
    static constexpr float STUMBLE_BRAKING_SHARE = 0.5f;

    // The fall (plan step 4): down, he is the cylinder on its side.
    /** Knocked mostly forward or back (his slide within this many degrees
     * of his facing or its back) he lies along his slide; otherwise
     * across it, rolling along it. */
    static constexpr float LIE_ALONG_WITHIN_DEGREES = 45.0f;
    /** Sliding or tumbling, grass slows him at this (m/sÂ²; friction ~0.5-0.6). */
    static constexpr float FALL_DECELERATION = 5.5f;
    /** One turn of his body rolls him this far (m): its circumference. */
    static constexpr float ROLL_METRES_PER_TURN =
        3.14159265f * Body::DIAMETER_METRES;
    /** The chance of a big hit, by the jolt: none at the first, certain at
     * the second (m/s). Only if he fell. */
    static constexpr float BIG_HIT_JOLT_FROM = 4.0f;
    static constexpr float BIG_HIT_JOLT_CERTAIN = 7.0f;
    /** A big hit keeps him down at least this long (s), from the first
     * jolt to the second. */
    static constexpr float BIG_HIT_DOWN_AT_FROM = 5.0f;
    static constexpr float BIG_HIT_DOWN_AT_CERTAIN = 10.0f;
    /** Rolling about in pain (big hits, through the least time down): a
     * roll of a quarter to a full turn, pauses of 1-2 s between, each
     * roll's chance rising with the jolt over the big-hit range; most go
     * back the way the last came. */
    static constexpr float ROLL_SHORTEST_TURNS = 0.25f;
    static constexpr float ROLL_LONGEST_TURNS = 1.0f;
    static constexpr float ROLL_PAUSE_SHORTEST_SECONDS = 1.0f;
    static constexpr float ROLL_PAUSE_LONGEST_SECONDS = 2.0f;
    static constexpr float ROLL_REVERSE_SHARE = 0.75f;
    /** How fast he rolls (m/s): about a turn a second (to try). */
    static constexpr float ROLL_SPEED = 1.25f;
    /** Getting up once he has stopped (s), from Agility 1 to 99. */
    static constexpr float GET_UP_SECONDS_AT_AGILITY_1 = 2.0f;
    static constexpr float GET_UP_SECONDS_AT_AGILITY_99 = 1.0f;

    // Getting up (plan step 5): the brain's call, by urgency.
    /** He jumps up with the ball this near (m) and a chance to win it back. */
    static constexpr float GET_UP_BALL_METRES = 15.0f;
    /** An opponent's ball is contested with a team-mate of his this near it
     * (m; to try). */
    static constexpr float CONTESTED_METRES = 3.0f;
    /** Play runs on this long (s) with him down: he gets up gingerly. */
    static constexpr float GINGERLY_AFTER_SECONDS = 15.0f;
    /** Getting up gingerly (and in a stoppage), from Agility 1 to 99 (s). */
    static constexpr float GINGERLY_SECONDS_AT_AGILITY_1 = 4.0f;
    static constexpr float GINGERLY_SECONDS_AT_AGILITY_99 = 3.0f;

    // The ball against a fallen body (plan step 6), worked out in the flat.
    /** The ball reaches him with its centre below this (m): his body lies a
     * diameter high, and about a ball's radius above it (0.11 m). */
    static constexpr float FALLEN_BALL_BELOW_METRES = Body::DIAMETER_METRES + 0.1f;
    /** Off his body: this share of the speed straight into it comes back
     * (an uncushioned chest; the wall rebound), this share along it is kept. */
    static constexpr float FALLEN_BOUNCE_BACK_SHARE = 0.35f;
    static constexpr float FALLEN_ALONG_KEPT_SHARE = 0.8f;
  };

  /**
   * First touch (Binturong body, feet): the touch plays the ball on under real
   * physics and the player runs onto it. He owns it from the touch (a
   * claimed ball) until an opponent plays it or play stops (see Notes and
   * plans/match_engine_first_touch_plan.md and
   * match_engine_dribble_touches_plan.md).
   */
  struct FirstTouch final
  {
    /** A set touch leaves the ball this far ahead of him. */
    static constexpr float SET_OFFSET_METRES = 0.5f;
    /** Collected: within kick reach and moving with him. */
    static constexpr float COLLECT_RELATIVE_SPEED = 1.5f;
    /** A team-mate may play a claimed ball this far from its owner. */
    static constexpr float TEAMMATE_TAKES_METRES = 3.5f;
    /** Thinking time once the ball is collected (the AI). */
    static constexpr float MIN_DECISION_DELAY = 0.2f;
    static constexpr float MAX_DECISION_DELAY = 0.4f;
    /** How far ahead the chase looks along the ball's roll, and in what
     * steps. */
    static constexpr float CHASE_HORIZON_SECONDS = 2.0f;
    static constexpr float CHASE_STEP_SECONDS = 0.1f;

    // When to touch (Binturong brain): at the closest comfortable point the
    // ball will pass, not the moment it comes into reach.
    /** About a foot from his centre: any closer is as good as it gets. */
    static constexpr float COMFORT_GAP_METRES = 0.3f;
    /** Beyond the comfort gap he cannot touch a ball more than this far round
     * from his facing (degrees, either side): behind him is a dead zone
     * (Binturong bodies, outfielders). */
    static constexpr float DEAD_ZONE_DEGREES = 120.0f;
    /** He waits only for a point at least this much closer... */
    static constexpr float WAIT_GAIN_METRES = 0.05f;
    /** ...that is inside his reach by this margin. */
    static constexpr float WAIT_MARGIN_METRES = 0.15f;
    /** How far ahead he reads the ball and his own run. */
    static constexpr float TIMING_LOOKAHEAD_SECONDS = 0.3f;

    // What touch (Binturong brain).
    /** A set touch goes where he will be this long after it. */
    static constexpr float SET_LEAD_SECONDS = 0.3f;
    /** Into stride: the ball is played this far ahead along his run (his
     * speed times the lead, plus the extra), arriving at a share of his
     * speed so he gains on it; only into space clear of opponents. */
    static constexpr float STRIDE_LEAD_SECONDS = 0.8f;
    static constexpr float STRIDE_EXTRA_METRES = 1.0f;
    static constexpr float STRIDE_SPEED_SHARE = 0.9f;
    static constexpr float STRIDE_CLEARANCE_METRES = 4.0f;

    // How well he carries it out (Binturong body; Notes and plans/
    // match_engine_first_touch_jobs_plan.md). The touch is two jobs: the
    // weight (the pace change) and the angle (the turn off the line the
    // ball came on). Each has its own difficulty, chance of going wrong and
    // way of going wrong: a weight failure leaves it heavy, an angle failure
    // under-turns it.
    struct FactorWeights
    {
      float angle;
      float weight;
    };
    /** The jobs' sizes: the pace change (1 at the reference speed) counts
     * for the weight, the turn (1 at 90Â°) for the angle, times the inward
     * or outward multiplier (turnsOutward: the instep or the outside of his
     * foot). */
    static constexpr float PACE_CHANGE_WEIGHT = 0.6f;
    static constexpr float TURN_ANGLE = 0.6f;
    static constexpr float TURN_OUTWARD = 1.5f;
    static constexpr float TURN_INWARD = 0.75f;
    /** The turn in steepness (1 at 90 degrees), up or down alike: as the
     * turn seen from above, to start (vertical contact plan). */
    static constexpr float VERTICAL_TURN_ANGLE = TURN_ANGLE;
    // Conditions, each 0-1, adding to both difficulties with their weights.
    /** Incoming ball speed (1 at the reference speed). */
    static constexpr FactorWeights INCOMING_SPEED{0.15f, 0.10f};
    /** Where it comes from against where he faces (1 from behind). */
    static constexpr FactorWeights APPROACH{0.20f, 0.10f};
    /** How the approach angle grows with the angle off his facing (degrees, the
     * touch and every strike): nothing in front of him, a little towards
     * square, then steeply round to a full block behind him. */
    static constexpr float BEHIND_CLEAR_DEGREES = 45.0f;
    static constexpr float BEHIND_SQUARE_DEGREES = 90.0f;
    static constexpr float BEHIND_SQUARE_VALUE = 0.15f;
    static constexpr float BEHIND_FULL_DEGREES = 165.0f;
    /** His own speed against his top speed. */
    static constexpr FactorWeights OWN_MOVEMENT{0.10f, 0.10f};
    /** How far he reaches for it (the stretch map below). */
    static constexpr FactorWeights STRETCH{0.15f, 0.15f};
    /** The stretch map (StretchMap, the touch and every strike): stretch
     * (0-1) at each angle off his facing (degrees, rows) and distance from
     * his centre as a share of his reach (columns), blended in between.
     * Edited live in the contact view. As set: easiest in front of him,
     * rising faster out to his sides, and a ball behind him awkward even
     * close in (beyond 120 degrees and 0.3 of his reach is the dead zone
     * for a touch). */
    static constexpr std::array<float, 6> STRETCH_ANGLES{0.0f,   45.0f,
                                                         90.0f,  120.0f,
                                                         150.0f, 180.0f};
    static constexpr std::array<float, 6> STRETCH_DISTANCES{
        0.000f, 0.150f, 0.300f, 0.500f, 0.700f, 1.000f};
    static constexpr std::array<std::array<float, 6>, 6> STRETCH_VALUES{{
        {0.000f, 0.000f, 0.000f, 0.000f, 0.250f, 0.500f},
        {0.000f, 0.000f, 0.000f, 0.150f, 0.350f, 0.750f},
        {0.000f, 0.200f, 0.050f, 0.150f, 0.400f, 1.000f},
        {0.000f, 0.200f, 0.150f, 0.286f, 0.750f, 1.000f},
        {0.300f, 0.300f, 0.300f, 0.286f, 1.000f, 1.000f},
        {0.300f, 0.300f, 0.300f, 0.286f, 1.000f, 1.000f},
    }};
    /** Played with his weaker foot (the ball on that side of his facing
     * line): multiplies each job's difficulty by 1 + its weight. */
    static constexpr FactorWeights WEAK_FOOT{0.10f, 0.10f};
    /** Off the ground (1 at the top of the feet band). */
    static constexpr FactorWeights OFF_GROUND{0.15f, 0.15f};
    /** Not expecting it: the brain's read, on the touch intent (1 for a
     * pass meant for someone else). */
    static constexpr FactorWeights SURPRISE{0.15f, 0.15f};
    /** An opponent close by (1 on top of him, 0 at the radius): the
     * brain's read, on the touch intent. Within 1.5 m their 1 m reaches
     * overlap well: a body in the way, not just a man nearby. */
    static constexpr FactorWeights PRESSURE{0.10f, 0.10f};
    static constexpr float PRESSURE_RADIUS_METRES = 1.5f;
    static constexpr float REFERENCE_SPEED = 25.0f;

    /** Chance a job goes wrong: (base + difficulty x range), cut by his
     * touch skill (a perfect one by SKILL_REDUCTION) and widened by
     * tiredness and his weaker foot. */
    static constexpr float ANGLE_FAIL_BASE = 0.03f;
    static constexpr float ANGLE_FAIL_RANGE = 0.5f;
    static constexpr float WEIGHT_FAIL_BASE = 0.05f;
    static constexpr float WEIGHT_FAIL_RANGE = 0.5f;
    static constexpr float SKILL_REDUCTION = 0.9f;
    /** How badly a failed job misses: a share of the job, uniform up to
     * (base + difficulty x range). The skill decides how often; the ball
     * how badly. */
    static constexpr float ANGLE_MISS_BASE = 0.2f;
    static constexpr float ANGLE_MISS_RANGE = 0.6f;
    static constexpr float WEIGHT_MISS_BASE = 0.2f;
    static constexpr float WEIGHT_MISS_RANGE = 0.6f;
    /** Failures that go the usual way (heavy; under-turned); the rest
     * overshoot (over-cushioned or under-hit; over-turned). */
    static constexpr float HEAVY_SHARE = 0.85f;
    static constexpr float UNDER_TURN_SHARE = 0.85f;
    /** A clean touch's jitter (one sigma, cut by skill as the chance is):
     * radians for the angle, a share of the pace change for the weight. */
    static constexpr float ANGLE_JITTER = 0.04f;
    static constexpr float WEIGHT_JITTER = 0.04f;
    /** A ball sent back the way it came leaves at most this share of its
     * incoming pace (as off a wall). */
    static constexpr float REBOUND_SHARE = 0.6f;
    /** A first touch is not a pass: the most pace he means to give it. */
    static constexpr float MAX_TOUCH_SPEED = 8.0f;
  };

  /**
   * Dribbling: a carrier on the move pushes the ball 0.6-3 m ahead and
   * catches it up at the next touch, so the ball is exposed between touches
   * and a heavy touch can lose it. Defenders jockey goal-side and time their
   * challenge for the moment the ball is away from the attacker's foot.
   */
  struct Dribble final
  {
    /** Below this speed the ball is kept at the feet (close control). */
    static constexpr float CLOSE_CONTROL_SPEED = 2.2f;
    static constexpr float FEET_METRES = 0.35f;
    /** The carrier can strike the ball only when it is this close. */
    static constexpr float KICK_REACH_METRES = 0.9f;
    static constexpr float TOUCH_BASE_METRES = 0.5f;
    static constexpr float TOUCH_METRES_PER_SPEED = 0.28f;
    static constexpr float MIN_TOUCH_METRES = 0.6f;
    static constexpr float MAX_TOUCH_METRES = 3.0f;
    static constexpr float TOUCH_INTERVAL_BASE = 1.25f;
    static constexpr float TOUCH_INTERVAL_PER_SPEED = 0.07f;
    static constexpr float TOUCH_INTERVAL_DRIBBLING = 0.2f;
    static constexpr float MIN_TOUCH_INTERVAL = 0.4f;
    static constexpr float MAX_TOUCH_INTERVAL = 1.2f;
    static constexpr float HEAVY_TOUCH_BASE = 0.045f;
    static constexpr float HEAVY_TOUCH_PRESSURE = 1.5f;
    static constexpr float HEAVY_TOUCH_PRESSURE_METRES = 3.0f;
    static constexpr float MIN_HEAVY_PUSH_SPEED = 2.5f;
    static constexpr float MAX_HEAVY_PUSH_SPEED = 5.0f;
    // Take-ons against a jockeying defender in front.
    static constexpr float TAKE_ON_RANGE_METRES = 2.8f;
    static constexpr float TAKE_ON_BASE = 0.42f;
    static constexpr float TAKE_ON_SKILL = 0.6f;
    static constexpr float TAKE_ON_PACE = 0.25f;
    static constexpr float MIN_TAKE_ON = 0.1f;
    static constexpr float MAX_TAKE_ON = 0.8f;
    static constexpr float TAKE_ON_ANGLE_RADIANS = 0.5f;
    static constexpr float TAKE_ON_TOUCH_METRES = 2.4f;
    /** A beaten defender is wrong-footed: momentum lost, no challenge. */
    static constexpr float BEATEN_SECONDS = 1.1f;
    static constexpr float BEATEN_VELOCITY_RETAINED = 0.25f;

    // The loose dribble (Binturong body): the ball is never tethered; each
    // touch plays it on under physics and he runs onto it (dribble touches
    // plan).
    /** A touch into stride puts the ball where he will be this long later
     * at the pace he wants (less for a better dribbler)... */
    static constexpr float LOOSE_LEAD_SECONDS = 0.6f;
    /** ...plus this far, rolling on average at this share of that pace so
     * he gains on it (a set touch arrives at the arrival share). */
    static constexpr float LOOSE_LEAD_EXTRA_METRES = 0.4f;
    static constexpr float LOOSE_MEAN_PACE_SHARE = 0.95f;
    /** ...on a push this long or more (Binturong body); a shorter one is
     * weighted nearer his own pace, all of it at no length. */
    static constexpr float LOOSE_MEAN_PACE_FULL_METRES = 2.0f;
    static constexpr float LOOSE_ARRIVAL_SHARE = 0.85f;
    /** At least this long between two of his own touches. */
    static constexpr float LOOSE_TOUCH_INTERVAL = 0.25f;
    /** On a Binturong body the quickest he can play it again is by his
     * Dribbling (dribble meet plan 3b): this long with none, down to this
     * with the best, in whole 0.1 s steps (the ball's ticks). */
    static constexpr float TOUCH_INTERVAL_POOR_SECONDS = 0.5f;
    static constexpr float TOUCH_INTERVAL_BEST_SECONDS = 0.2f;
    /** In a contest (tackling plan step 2, agreed placeholder): within this
     * long of his planned touch the carrier's swing can't be called off. */
    static constexpr float COMMIT_SECONDS = 0.2f;
    /** Within this long of his planned touch a contest is a block: both
     * feet arrive in the same moment (tackling plan step 4, row 3, agreed
     * 2026-10-27); earlier in the commit moment he swings at nothing. */
    static constexpr float BLOCK_SECONDS = 0.1f;
    /** The commit rule (agreed 2026-10-26, a hard-wired standard): a feet
     * first touch (receiving, a loose ball, a free-ball tackle; not an
     * interception, a contest or a dribble touch) needs the ball on its
     * current path at least this long: he can't flinch at a ball that has
     * just changed. Committed to its old path, he misses it cleanly. */
    static constexpr float FIRST_TOUCH_COMMIT_SECONDS = 0.2f;
    /** He touches a ball heading this far off the way he wants to go... */
    static constexpr float LOOSE_REDIRECT_RADIANS = 0.5f;
    /** ...but at pace turns it at most this far off his run per touch, and
     * takes pace off for a sharper turn still to come (to this share at
     * most). */
    static constexpr float LOOSE_MAX_TURN_RADIANS = 0.6f;
    static constexpr float LOOSE_TURN_MIN_PACE = 0.4f;
    /** Below this pace he wants (m/s), a ball at rest by him stays there. */
    static constexpr float LOOSE_START_SPEED = 0.5f;
    /** A dribble touch is no pass: at most this much faster than his pace. */
    static constexpr float LOOSE_MAX_SPEED_OVER_RUN = 4.0f;
    /** His run to the next meet is a corner there (dribble meet plan): his
     * line may pass this far inside the meet point (the ball within
     * reach)... */
    static constexpr float MEET_APEX_METRES = 0.3f;
    /** ...and swing out this far to widen a sharp turn. */
    static constexpr float MEET_OUTWARD_ROOM_METRES = 1.5f;
    /** Deciding for his next meet (dribble meet plan 2a): where the others
     * will be is read this share of the way from their current velocity to
     * their planned target, times his Vision (design choice). */
    static constexpr float MEET_READ_TARGET_SHARE = 0.6f;
    /** He decides for a meet no further off than this (s)... */
    static constexpr float MEET_DECISION_HORIZON_SECONDS = 1.0f;
    /** ...and a strike so decided is dropped (he decides again) when still
     * unplayed this long past that horizon (the meet's own time reads
     * optimistic: he may still be turning or speeding up), or when the meet
     * has moved this far. */
    static constexpr float MEET_DECISION_LATE_SECONDS = 0.5f;
    static constexpr float MEET_DECISION_DRIFT_METRES = 1.5f;
    /** Shaping his run in to a pass or shot at the meet (2b): with this much
     * time to spare (s) before an opponent could get to the meet, he opens
     * his body fully into it; with none, straight at the meet. An opponent
     * reads it this long first. */
    static constexpr float MEET_SHAPE_MARGIN_SECONDS = 0.8f;
    static constexpr float MEET_SHAPE_READ_SECONDS = 0.2f;
    /** After a strike at the meet he runs on this far along its line (the
     * corner's way out). */
    static constexpr float MEET_STRIKE_OUT_METRES = 2.0f;
    /** Turning to pass (passing to-do, technique 2), Binturong body: a held
     * pass turns his body toward its line, and one decided with the ball at
     * his feet waits for the turn. It is struck once he is within
     * TURN_TO_PASS_READY_DEGREES of the line (or as far round as the ball
     * lets him), or when his time is up: before an opponent could get to
     * the ball (read as for the meet's shaping), at most
     * TURN_TO_PASS_MAX_SECONDS. He turns no further than leaves the ball
     * this far inside his dead zone's edge (design choices). */
    static constexpr float TURN_TO_PASS_READY_DEGREES = 10.0f;
    static constexpr float TURN_TO_PASS_MAX_SECONDS = 1.0f;
    /** A restart's taker walks to a spot behind the ball this share of his
     * comfort gap from it, and kicks once the ball is inside the gap
     * (restart lining-up). */
    static constexpr float RESTART_SPOT_COMFORT_SHARE = 0.75f;
    /** Everyone else keeps this far from the ball while he lines it up:
     * room for his body (the minimum body separation, 0.8 m) beside a ball
     * inside his comfort gap (0.3 m), and a margin. */
    static constexpr float RESTART_ROOM_METRES = 1.2f;
    static constexpr float TURN_TO_PASS_DEAD_ZONE_MARGIN_DEGREES = 15.0f;
    /** The carry's pace (2c, design choices): an urgency 0-1 (0 a walk, 1/3
     * a jog, 2/3 a run, 1 a sprint, the tops of his bands) from a base, the
     * space along his way (the nearest opponent within the cone ahead,
     * open from SPACE_CLOSE to SPACE_OPEN metres), an attacking transition,
     * and the side's risk-taking (from its middle). */
    static constexpr float CARRY_URGENCY_BASE = 0.15f;
    static constexpr float CARRY_URGENCY_SPACE = 0.55f;
    static constexpr float CARRY_URGENCY_TRANSITION = 0.35f;
    static constexpr float CARRY_URGENCY_RISK = 0.3f;
    static constexpr float CARRY_SPACE_CLOSE_METRES = 3.0f;
    static constexpr float CARRY_SPACE_OPEN_METRES = 18.0f;
    static constexpr float CARRY_SPACE_CONE_COSINE = 0.64f;
    /** The push by intent (3c, design choices). How long until he wants it
     * again, in open space: his touch interval at a walk (urgency 0),
     * PUSH_JOG seconds at a jog (1/3), PUSH_SPRINT at a sprint (1), in
     * straight lines between. Closed down: with the nearest opponent within
     * PUSH_CLOSE_NEAR metres of him, as soon as he can (his touch interval);
     * from there out to PUSH_CLOSE_FAR, blended back to the open-space time. */
    static constexpr float PUSH_JOG_SECONDS = 0.7f;
    static constexpr float PUSH_SPRINT_SECONDS = 1.5f;
    static constexpr float PUSH_CLOSE_NEAR_METRES = 2.0f;
    static constexpr float PUSH_CLOSE_FAR_METRES = 4.0f;
    /** The push goes at most this far, as far as he still gets to it this
     * long before any opponent could (who reads it this long first, then
     * speeds up from his own run as his legs allow), and short of where he
     * is going by what he runs in this long (it rolls on past the spot). */
    static constexpr float PUSH_MAX_METRES = 12.0f;
    static constexpr float PUSH_MARGIN_SECONDS = 0.1f;
    static constexpr float PUSH_READ_SECONDS = 0.3f;
    static constexpr float PUSH_ROLL_ON_SECONDS = 0.6f;
    /** A push this long is a knock into space: he runs after it at his full
     * pace, not the dribble's, and means it as hard as it needs. */
    static constexpr float PUSH_CHASE_METRES = 6.0f;
    /** On a route, a push is aimed where his line will be when it gets there,
     * but no further along it than a straight roll stays within this much
     * of the line (m): round a tight turn, it would cut across the inside.
     * A better dribbler cuts more (his touch true enough to collect it
     * there), so turns it further per touch: this much with no Dribbling,
     * up to this with the best. */
    static constexpr float PUSH_LINE_CUT_POOR_METRES = 0.1f;
    static constexpr float PUSH_LINE_CUT_BEST_METRES = 0.5f;
    /** Before he means to play it again (3d, design choices), he plays it
     * early with an opponent this close to it (m), or once it is no more
     * than this far ahead of him along its roll (caught). */
    static constexpr float EARLY_TOUCH_THREAT_METRES = 2.5f;
    static constexpr float EARLY_TOUCH_CAUGHT_METRES = 0.2f;
    /** A push rolls straight: on a route it goes no further into a turn
     * than where the line has curved this far off it. */
    static constexpr float PUSH_LINE_DEVIATION_METRES = 0.5f;
    /** Into a stop (a sharp reversal) he does not wait for the ball to roll
     * dead: it gets there with this much pace (m/s) for his touch to stop,
     * by his Dribbling (poor to best), and a touch within STOP_TOUCH_METRES
     * of the stop takes all its pace off. Out of the stop he plays the
     * still ball on once facing within this much (degrees) of the way he
     * sends it, by Dribbling (a better one before he has turned to it),
     * or after REVERSAL_HOLD_SECONDS whatever. Design choices. */
    static constexpr float STOP_MEET_PACE_POOR = 0.5f;
    static constexpr float STOP_MEET_PACE_BEST = 3.0f;
    static constexpr float STOP_TOUCH_METRES = 0.5f;
    static constexpr float REVERSAL_TOUCH_DEGREES_POOR = 30.0f;
    static constexpr float REVERSAL_TOUCH_DEGREES_BEST = 90.0f;
    static constexpr float REVERSAL_HOLD_SECONDS = 1.0f;
  };

  /** Physical load statistics thresholds (m/s). */
  struct Load final
  {
    // Bradley et al. 2009's bands: walking below 7.2 km/h, jogging to 14.4,
    // running to 19.8, high-speed running to 25.2, sprinting above.
    static constexpr float JOG_SPEED = 2.0f;
    static constexpr float RUN_SPEED = 4.0f;
    static constexpr float HIGH_INTENSITY_SPEED = 5.5f;
    static constexpr float SPRINT_SPEED = 7.0f;
    static constexpr float SPRINT_EXIT_SPEED = 6.5f;
  };

  struct Shape final
  {
    // Units (training ground plan section 17): a value named _METRES is a
    // distance from the ball, a player, a line or a goal. The rest of the
    // distances here are shares of the pitch on purpose (along: of the
    // length; across: of the width), so the team's shape and its moves from
    // the formation scale with the pitch.
    static constexpr float MIN_WIDTH_SCALE = 0.72f;
    static constexpr float WIDTH_SLIDER_SCALE = 0.54f;
    static constexpr float BASE_LONGITUDINAL_SHIFT = 0.12f;
    static constexpr float COMPACTNESS_LONGITUDINAL_SHIFT = 0.12f;
    static constexpr float BASE_LATERAL_SHIFT = 0.10f;
    static constexpr float COMPACTNESS_LATERAL_SHIFT = 0.10f;
    static constexpr float CARRIER_BASE_ADVANCE_METRES = 10.5f;
    static constexpr float CARRIER_DRIBBLING_ADVANCE_METRES = 8.4f;
    static constexpr float CARRIER_RISK_ADVANCE_METRES = 5.25f;
    static constexpr float CARRIER_EVASION_RANGE_METRES = 8.0f;
    static constexpr float CARRIER_BASE_EVASION_METRES = 2.38f;
    static constexpr float CARRIER_DRIBBLING_EVASION_METRES = 3.06f;
    static constexpr float CARRIER_CENTRALITY = 0.16f;
    // Wide players carrying down the flank keep their lane to cross.
    static constexpr float WIDE_LANE_DEVIATION = 0.24f;
    /** In the final third a wide forward sometimes cuts inside instead,
     * more often when he shoots better than he crosses. */
    static constexpr float CUT_INSIDE_BASE = 0.50f;
    static constexpr float CUT_INSIDE_PREFERENCE = 1.0f;
    static constexpr float MIN_CUT_INSIDE = 0.10f;
    static constexpr float MAX_CUT_INSIDE = 0.85f;
    static constexpr float CUT_INSIDE_PULL = 0.8f;
    static constexpr float SUPPORT_BASE_ADVANCE = 0.035f;
    static constexpr float SUPPORT_OFFENSIVE_ADVANCE = 0.10f;
    static constexpr std::size_t MAX_ACTIVE_SUPPORTERS = 3;
    static constexpr std::size_t NEAR_SUPPORT_SLOT = 0;
    static constexpr std::size_t SQUARE_SUPPORT_SLOT = 1;
    static constexpr std::size_t TRAILING_SUPPORT_SLOT = 2;
    static constexpr float SUPPORT_SELECTION_CONTINUITY_METRES = 7.0f;
    static constexpr float NEAR_SUPPORT_DEPTH_METRES = 6.825f;
    static constexpr float NEAR_SUPPORT_WIDTH_METRES = 7.14f;
    static constexpr float SQUARE_SUPPORT_DEPTH_METRES = 2.625f;
    static constexpr float SQUARE_SUPPORT_WIDTH_METRES = 11.22f;
    static constexpr float TRAILING_SUPPORT_DEPTH_METRES = 13.125f;
    static constexpr float TRAILING_SUPPORT_WIDTH_METRES = 3.06f;
    static constexpr float TRANSITION_SUPPORT_FORWARD_BONUS = 0.055f;
    static constexpr float RUN_BASE_ADVANCE = 0.045f;
    static constexpr float RUN_RISK_ADVANCE = 0.09f;
    static constexpr float ONSIDE_RECOVERY_METRES = 5.775f;
    static constexpr float MAX_SUPPORT_DISTANCE_METRES = 29.0f;
    static constexpr float SUPPORT_LONGITUDINAL_PULL = 0.20f;
    static constexpr float SUPPORT_LATERAL_PULL = 0.14f;
    static constexpr float POSSESSION_PROGRESS_START = 0.25f;
    static constexpr float POSSESSION_BLOCK_PROGRESS = 0.24f;
    static constexpr std::size_t MIN_COMMITTED_RUNNERS = 1;
    static constexpr std::size_t MAX_COMMITTED_RUNNERS = 3;
    static constexpr std::size_t MAX_STRIKER_RUNNERS = 1;
    static constexpr float SECOND_RUNNER_PROGRESS_THRESHOLD = 0.38f;
    static constexpr float SECOND_RUNNER_ATTACK_THRESHOLD = 0.80f;
    static constexpr float STRIKER_RUN_PRIORITY = 0.32f;
    static constexpr float WINGER_RUN_PRIORITY = 0.30f;
    static constexpr float ATTACKING_MIDFIELDER_RUN_PRIORITY = 0.30f;
    static constexpr float MIDFIELDER_RUN_PRIORITY = 0.26f;
    static constexpr float HOLDING_MIDFIELDER_RUN_PRIORITY = 0.08f;
    static constexpr float RUN_PACE_PRIORITY = 0.22f;
    static constexpr float RUN_DEPTH_PRIORITY = 0.12f;
    static constexpr float RUN_SEPARATION_PRIORITY = 0.10f;
    static constexpr float RUN_CONTINUITY_PRIORITY = 0.24f;
    /** Spread of the per-attack variation in who makes the runs. */
    static constexpr float RUN_VARIETY_PRIORITY = 0.15f;
    static constexpr float RUN_ONSIDE_BUFFER_METRES = 1.26f;
    static constexpr float RUN_DEPTH_TARGET_PULL = 0.82f;
    static constexpr float SECONDARY_RUN_DEPTH_STAGGER_METRES = 5.775f;
    static constexpr float RUN_CHANNEL_BLEND = 0.55f;
    static constexpr float LEFT_WIDE_ATTACK_CHANNEL = 0.18f;
    static constexpr float RIGHT_WIDE_ATTACK_CHANNEL = 0.82f;
    static constexpr float LEFT_INSIDE_FORWARD_CHANNEL = 0.34f;
    static constexpr float RIGHT_INSIDE_FORWARD_CHANNEL = 0.66f;
    static constexpr float LEFT_STRIKER_ATTACK_CHANNEL = 0.42f;
    static constexpr float RIGHT_STRIKER_ATTACK_CHANNEL = 0.58f;
    static constexpr float CENTRAL_ATTACK_CHANNEL = 0.50f;
    static constexpr float MINIMUM_RUN_CHANNEL_SEPARATION = 0.16f;
    static constexpr float FORWARD_SHORT_OPTION_DEPTH_METRES = 9.45f;
    static constexpr float FORWARD_SHORT_OPTION_LATERAL_SEPARATION_METRES = 8.84f;
    /** Late runs into the box in the final third (metres out from the
     * goal line, and from the centre away from the ball's side). */
    static constexpr float MIDFIELD_ARRIVAL_DEPTH_METRES = 12.6f;
    static constexpr float MIDFIELD_ARRIVAL_WIDTH_METRES = 3.4f;
    static constexpr float FAR_POST_ARRIVAL_DEPTH_METRES = 7.35f;
    static constexpr float FAR_POST_ARRIVAL_WIDTH_METRES = 6.8f;
    /** The far-post run needs the ball out wide. */
    static constexpr float FAR_POST_MIN_BALL_WIDTH_METRES = 9.52f;
    static constexpr float FINAL_THIRD_FULLBACK_OVERLAP = 0.17f;
    static constexpr float FINAL_THIRD_SELECTION_CONTINUITY = 0.16f;
    // A presser jockeys goal-side 1.5-2.5 m off the carrier.
    static constexpr float PRESSING_STANDOFF_BASE_METRES = 1.47f;
    static constexpr float PRESSING_STANDOFF_CAUTIOUS_BONUS_METRES = 1.05f;
    static constexpr float PRESSER_CONTINUITY_SECONDS = 0.75f;
    static constexpr float COVER_PRESS_MINIMUM = 0.35f;
    static constexpr float COVER_LANE_INTERCEPTION_POINT = 0.52f;
    static constexpr float COVER_LANE_GOAL_SIDE_OFFSET_METRES = 1.89f;
    static constexpr float COVER_OUTLET_MAX_DISTANCE_METRES = 29.0f;
    static constexpr float COVER_FORWARD_OPTION_METRES = 7.0f;
    static constexpr float COVER_FALLBACK_DEPTH_METRES = 3.675f;
    static constexpr float COVER_FALLBACK_LATERAL_OFFSET_METRES = 3.74f;
    static constexpr float CHANNEL_WEIGHT = 1.4f;
    static constexpr float DANGER_DEPTH_WEIGHT = 0.12f;
    static constexpr float BASE_MARK_WEIGHT = 0.12f;
    static constexpr float COMPACTNESS_MARK_WEIGHT = 0.20f;
    static constexpr std::uint64_t MARK_REFRESH_STEPS = 20;
    static constexpr float TIGHT_MARK_WITHIN_METRES = 39.9f;
    static constexpr float TIGHT_MARK_WEIGHT = 0.7f;
    static constexpr float GOAL_SIDE_MARK_METRES = 1.5f;
    static constexpr float SECOND_LOOSE_BALL_PRESS_THRESHOLD = 0.9f;
    static constexpr float ATTACKING_TRANSITION_SUPPORT_ADVANCE = 0.065f;
    static constexpr float ATTACKING_TRANSITION_RUN_ADVANCE = 0.13f;
    static constexpr float ATTACKING_TRANSITION_BALL_PULL = 0.12f;
    static constexpr float ATTACKING_TRANSITION_CARRIER_ADVANCE_METRES = 5.775f;
    static constexpr float IN_FLIGHT_SUPPORT_ADVANCE_PER_SECOND = 0.03f;
    static constexpr float IN_FLIGHT_SUPPORT_BALL_PULL_PER_SECOND = 0.25f;
    static constexpr float DEFENSIVE_TRANSITION_RECOVERY = 0.055f;
    static constexpr float DEFENSIVE_TRANSITION_BALL_COMPACTNESS = 0.16f;
    // Out-of-possession block (depths measured from the own goal line).
    static constexpr float BLOCK_LINE_BASE = -0.03f;
    static constexpr float BLOCK_LINE_BALL_FACTOR = 0.5f;
    static constexpr float BLOCK_LINE_PRESSING = 0.08f;
    static constexpr float BLOCK_LINE_MIN = 0.06f;
    static constexpr float BLOCK_LINE_MAX = 0.42f;
    static constexpr float BLOCK_SHORT_BALL_DEPTH = 0.2f;
    static constexpr float BLOCK_LONG_BALL_DEPTH = 0.7f;
    static constexpr float BLOCK_SHORT_LENGTH = 0.16f;
    static constexpr float BLOCK_LONG_LENGTH = 0.36f;
    static constexpr float BLOCK_COMPACTNESS_SQUEEZE = 0.2f;
    static constexpr float BLOCK_FORMATION_BACK = 0.18f;
    static constexpr float BLOCK_FORMATION_FRONT = 0.80f;
    static constexpr float BLOCK_WIDTH_SCALE = 0.72f;
    static constexpr float BLOCK_COMPACTNESS_NARROWING = 0.12f;
    static constexpr float BLOCK_BALL_SIDE_SHIFT = 0.35f;
  };

  struct Decision final
  {
    static constexpr float PRESSURE_RADIUS_METRES = 8.5f;
    static constexpr float BASE_SHOT_THRESHOLD = 0.056f;
    static constexpr float MIN_SHOT_XG = 0.005f;
    /** Time on the ball before a carrier re-decides while dribbling. */
    static constexpr float MIN_DRIBBLE_TIME = 0.5f;
    static constexpr float MAX_DRIBBLE_TIME = 1.1f;

    // Scored action selection: every candidate (best pass, shot, carry,
    // shield) is measured in a shared utility currency and the closest
    // choices are resolved by a vision-scaled random perturbation.
    static constexpr float SHOT_SCORE_SCALE = 26.0f;
    static constexpr float SHOT_BASE_INCLINATION = 1.5f;
    static constexpr float FINAL_THIRD_SHOT_BONUS = 0.3f;
    static constexpr float SHOT_ELIGIBILITY_FLOOR = 0.012f;
    static constexpr float SHOT_ELIGIBILITY_RANGE = 0.04f;
    static constexpr float SHOT_SKILL_BONUS = 0.28f;
    static constexpr float SHOT_PRESSURE_PENALTY = 0.02f;
    static constexpr float CARRY_OPENNESS_WEIGHT = 1.50f;
    static constexpr float CARRY_DRIBBLING_BONUS = 0.45f;
    static constexpr float CARRY_PRESSURE_PENALTY = 0.30f;
    static constexpr float CARRY_RISK_BIAS = 0.20f;
    static constexpr float CARRY_HOLD_PENALTY_PER_SECOND = 0.45f;
    static constexpr float SHIELD_PRESSURE_THRESHOLD = 0.60f;
    /** Carry appetite of a wide forward cutting inside (see Shape). */
    static constexpr float CUT_INSIDE_CARRY_BONUS = 0.8f;
    static constexpr float CUT_INSIDE_SHOT_BONUS = 0.6f;
    static constexpr float SHIELD_BONUS = 0.40f;
    static constexpr float SHIELD_DRIBBLING = 0.12f;
    static constexpr float VISION_NOISE_SCALE = 0.45f;
    static constexpr float VISION_DECISION_WEIGHT = 0.6f;
    /** Extra decision noise for a side with no tactical familiarity. */
    static constexpr float FAMILIARITY_NOISE_GAIN = 0.8f;
    static constexpr float LATE_GAME_MINUTE = 82.0f;
    static constexpr float LATE_LEAD_SPECULATIVE_PENALTY = 0.55f;
    static constexpr int COMFORTABLE_LEAD = 2;
    static constexpr float COMFORTABLE_LEAD_SHOT_PENALTY = 3.0f;
    static constexpr float CLEAR_CHANCE_XG = 0.30f;
    static constexpr float COMFORTABLE_LEAD_CARRY_PENALTY = 0.5f;
    static constexpr float WIDE_SHOT_WIDTH_METRES = 21.76f;
    static constexpr float WIDE_SHOT_DISCOUNT = 0.12f;
    static constexpr float WIDE_RECYCLE_BONUS = 0.85f;
  };

  struct Passing final
  {
    static constexpr float MIN_DISTANCE_METRES = 2.5f;
    static constexpr float MAX_DISTANCE_METRES = 50.0f;
    static constexpr float IDEAL_DISTANCE_METRES = 16.0f;
    static constexpr float OPENNESS_RADIUS_METRES = 10.0f;
    static constexpr float OPENNESS_WEIGHT = 1.15f;
    static constexpr float LANE_RISK_WEIGHT = 1.45f;
    static constexpr float BASE_PROGRESS_WEIGHT = 1.4f;
    static constexpr float OFFENSIVE_PROGRESS_WEIGHT = 1.15f;
    /** Utility lost per metre away from the ideal pass length. */
    static constexpr float DISTANCE_PENALTY_PER_METRE = 0.0135f;
    static constexpr float SAFE_OUTLET_WEIGHT = 0.8f;
    static constexpr float FORWARD_ROLE_BONUS = 0.0f;
    static constexpr float MIN_ACCEPTABLE_OPTION_SCORE = -0.15f;
    static constexpr float PRESSURE_RELEASE_THRESHOLD = 0.55f;
    // Progression is a share of the length gained towards goal (on purpose:
    // the same on any pitch); so are the bands across the width below.
    static constexpr float PRESSURE_RELEASE_MAX_PROGRESSION = 0.03f;
    static constexpr float PROGRESSIVE_PASS_MINIMUM = 0.035f;
    /** Space behind the opposing line (metres to their goal line) that makes
     * a ball over or through it attractive, and the range to full effect. */
    static constexpr float SPACE_BEHIND_MIN_METRES = 30.0f;
    static constexpr float SPACE_BEHIND_RANGE_METRES = 25.0f;
    static constexpr float SPACE_BEHIND_UTILITY = 1.2f;
    static constexpr float SPACE_BEHIND_COMPLETION_BONUS = 0.15f;
    static constexpr float THROUGH_BALL_MINIMUM_PROGRESSION = 0.09f;
    static constexpr float SWITCH_PLAY_MINIMUM_WIDTH = 0.34f;
    static constexpr float WIDE_ATTACK_MINIMUM_Y = 0.30f;
    static constexpr float WIDE_ATTACK_MAXIMUM_Y = 0.70f;
    static constexpr float CENTRAL_TARGET_MINIMUM_Y = 0.30f;
    static constexpr float CENTRAL_TARGET_MAXIMUM_Y = 0.70f;
    static constexpr float CROSS_PASSER_WITHIN_METRES = 39.9f;
    static constexpr float CROSS_RECEIVER_WITHIN_METRES = 31.5f;
    static constexpr float CUTBACK_PASSER_WITHIN_METRES = 14.7f;
    static constexpr float CUTBACK_MAXIMUM_PROGRESSION = 0.035f;
    static constexpr float CUTBACK_MINIMUM_PROGRESSION = -0.24f;
    static constexpr float CROSS_UTILITY_BONUS = 0.7f;
    static constexpr float CUTBACK_UTILITY_BONUS = 0.62f;
    static constexpr float CROSS_COMPLETION_PENALTY = 0.10f;
    static constexpr float CUTBACK_COMPLETION_BONUS = 0.07f;
    static constexpr float CROSS_TARGET_BLEND = 0.35f;
    static constexpr float BASE_COMPLETION_PROBABILITY = 0.58f;
    static constexpr float PASSING_COMPLETION_BONUS = 0.27f;
    static constexpr float OPENNESS_COMPLETION_BONUS = 0.15f;
    static constexpr float LANE_COMPLETION_PENALTY = 0.50f;
    static constexpr float DISTANCE_COMPLETION_PENALTY = 0.42f;
    static constexpr float LONG_PASS_REFERENCE_METRES = 45.0f;
    static constexpr float PRESSURE_COMPLETION_PENALTY = 0.14f;
    static constexpr float MIN_COMPLETION_PROBABILITY = 0.05f;
    static constexpr float MAX_COMPLETION_PROBABILITY = 0.98f;
    static constexpr float COMPLETION_UTILITY_WEIGHT = 1.5f;
    /** Per unit of risk-taking above neutral: weight gained by progress and
     * lost by the completion chance (below neutral the other way round). */
    static constexpr float RISK_PROGRESS_GAIN = 0.0f;
    static constexpr float RISK_SAFETY_GAIN = 0.0f;
    static constexpr float ACTIVE_RUNNER_UTILITY_BONUS = 0.16f;
    /** Utility per unit of the receiver's shooting chance (final third). */
    static constexpr float SHOT_CREATION_WEIGHT = 1.0f;
    static constexpr float THROUGH_BALL_FORWARD_LEAD_METRES = 5.775f;
    static constexpr float THROUGH_BALL_TARGET_BLEND = 0.60f;
    static constexpr float LOFTED_DISTANCE_METRES = 30.0f;
    static constexpr float PASS_PRESSURE_RADIUS_METRES = 7.5f;
    /** Average ball speed used to lead a moving receiver. */
    static constexpr float ESTIMATED_BALL_SPEED = 13.0f;
    static constexpr float RECEIVER_LEAD_SCALE = 0.45f;
    // Lateral execution error in metres (distance error per metre passed).
    static constexpr float TECHNICAL_ERROR_METRES = 3.6f;
    static constexpr float PRESSURE_ERROR_METRES = 1.2f;
    static constexpr float DISTANCE_ERROR = 0.012f;
    static constexpr float BACK_PASS_ERROR_SCALE = 0.35f;
    static constexpr float MAX_BACK_PASS_METRES = 28.0f;
    // Ground passes are struck so they arrive at a controllable pace.
    static constexpr float ARRIVAL_SPEED_BASE = 6.5f;
    static constexpr float ARRIVAL_SPEED_PASSING = 3.0f;
    static constexpr float ARRIVAL_SPEED_PER_METRE = 0.05f;
    static constexpr float MIN_GROUND_SPEED = 7.0f;
    /** Aardvark body: every ground pass; Binturong body: throws only (his
     * kicks are held to his kick power). */
    static constexpr float MAX_GROUND_SPEED = 24.0f;
    // Lofted balls: horizontal launch speed grows with the distance.
    static constexpr float LOFTED_BASE_SPEED = 11.0f;
    static constexpr float LOFTED_SPEED_PER_METRE = 0.22f;
    /** Aardvark body only (Binturong: his kick power). */
    static constexpr float MAX_LOFTED_SPEED = 24.0f;
    /** Binturong body: kick power, the fastest he can strike a pass (m/s,
     * the ball's launch speed along the ground or through the air): from
     * MIN_KICK_SPEED at Passing 1 (a Sunday-league player) to
     * MAX_KICK_SPEED at Passing 99 (the world's best). A pass that needs
     * more is struck at his limit and falls short. */
    static constexpr float MIN_KICK_SPEED = 20.0f;
    static constexpr float MAX_KICK_SPEED = 35.0f;
    /** Binturong body: the power budget with an incoming ball (agreed in
     * the passing to-do, built in shooting technique plan step 6; first
     * guesses). Kick power is what his foot gives the ball. Of the ball's
     * pace across the line he strikes, this share stays on it and his foot
     * aims against it; of its pace along the line, this share adds to the
     * strike (coming at him) or takes from it (running away). The fastest
     * he can send it: sqrt(P^2 - (across share x across pace)^2) - along
     * share x along pace, and never under the floor share of P. Passes and
     * shots alike (throws: their own limit). */
    static constexpr float INCOMING_ACROSS_SHARE = 0.7f;
    static constexpr float INCOMING_ALONG_SHARE = 0.1f;
    static constexpr float INCOMING_BUDGET_FLOOR = 0.25f;
    /** Binturong body: the most spin he can put on a pass (rev/s, sidespin
     * and topspin together), from Passing 1 (a Sunday-league player) to
     * Passing 99 (the world's best); a pass asking more gets this much.
     * Skilled players' curled kicks: about 7-10 rev/s (Asai et al. 2007;
     * Bray and Kerwin 2003). */
    static constexpr float MIN_SPIN_REVS = 4.0f;
    static constexpr float MAX_SPIN_REVS = 10.0f;
    /** Binturong body: the spin a kick gets when the brain asks for none
     * (rev/s; + topspin, - backspin): a lofted instep ball's backspin, a
     * cross's a little less (first guesses; held to his capacity). */
    static constexpr float DEFAULT_LOFTED_TOPSPIN_REVS = -5.0f;
    static constexpr float DEFAULT_CROSS_TOPSPIN_REVS = -4.0f;
    /** Pass types (Binturong body, 2026-10-23; first guesses). The share of
     * his kick power each can use, by the part of the foot: the side of
     * the foot (a ground pass) can't strike at full power; the instep
     * (lofted, driven, crosses) can; a chip is a stab. */
    static constexpr float SIDE_FOOT_POWER_SHARE = 0.75f;
    static constexpr float CHIP_POWER_SHARE = 0.6f;
    /** A driven pass is struck at this share of his kick power, low enough
     * to come down at the target (a long one in a flat arc), with a little
     * topspin (rev/s). */
    static constexpr float DRIVEN_SPEED_SHARE = 0.85f;
    /** Steeper than this (deg) at that share, he hits it at full power. */
    static constexpr float DRIVEN_MAX_ANGLE_DEGREES = 12.0f;
    static constexpr float DEFAULT_DRIVEN_TOPSPIN_REVS = 2.0f;
    /** A chip: the flattest launch, between these angles (deg), that comes
     * down at the target and clears this height this far out (or halfway,
     * for a short one), with heavy backspin (rev/s, held to his
     * capacity). */
    static constexpr float CHIP_CLEAR_HEIGHT_METRES = 2.5f;
    static constexpr float CHIP_CLEAR_DISTANCE_METRES = 4.0f;
    static constexpr float CHIP_MIN_ANGLE_DEGREES = 30.0f;
    static constexpr float CHIP_MAX_ANGLE_DEGREES = 70.0f;
    static constexpr float DEFAULT_CHIP_TOPSPIN_REVS = -9.0f;
    /** A first-time pass (Binturong body; drills only until the brain
     * judges it): he means to play it on as it reaches him once it is this
     * close (s), re-read every step until he meets it... */
    static constexpr float FIRST_TIME_DECISION_HORIZON_SECONDS = 0.5f;
    /** ...a ground ball only: no higher than this when he meets it (m);
     * otherwise he takes a first touch. */
    static constexpr float FIRST_TIME_MAX_BALL_HEIGHT_METRES = 0.1f;
    static constexpr float LOFTED_ARRIVAL_HEIGHT_METRES = 1.1f;
    static constexpr float DELIVERY_HEIGHT_SPREAD = 0.6f;
    static constexpr float OFFSIDE_PERCEPTION_ERROR_METRES = 6.3f;
    // Runners time their runs imperfectly: they sometimes drift beyond the
    // line, and a passer who misreads it releases an offside pass.
    static constexpr float RUN_TIMING_GAMBLE_METRES = 2.1f;
    static constexpr float OFFSIDE_TIMING_WINDOW_METRES = 3.15f;
    static constexpr float OFFSIDE_TIMING_CHANCE = 0.7f;
    static constexpr float RUN_TIMING_EPOCH_SECONDS = 5.0f;
    /** Heading rotation (rad/s) of a curled ground pass. */
    static constexpr float MAX_CURVE = 0.12f;
    static constexpr float CURVE_SKILL_BASE = 0.4f;
    /** Binturong body: a ground pass has no curve unless he means one; with
     * this chance at Passing 0 (falling to none at 100) it picks one up by
     * accident, up to MAX_CURVE the way his foot bends it (left foot: to
     * his right; right foot: to his left). */
    static constexpr float ACCIDENTAL_CURVE_CHANCE = 0.3f;
    /** Binturong body: a lofted pass the same way, curling more than one
     * along the ground (struck under and across the ball, with nothing but
     * the air to work on its spin). */
    static constexpr float LOFTED_ACCIDENTAL_CURVE_CHANCE = 0.3f;
    static constexpr float LOFTED_MAX_CURVE = 0.15f;
    static constexpr float MIN_ACTION_COOLDOWN = 0.8f;
    static constexpr float MAX_ACTION_COOLDOWN = 1.4f;
    /** The kicker cannot touch his own kick again for this long. */
    static constexpr float KICKER_LOCKOUT_SECONDS = 0.35f;
    static constexpr float LANE_START_MARGIN = 0.06f;
    static constexpr float LANE_END_MARGIN = 0.97f;
    static constexpr float BASE_INTERCEPTION_RADIUS_METRES = 2.2f;
    static constexpr float DEFENDING_INTERCEPTION_METRES = 1.7f;
    static constexpr float PACE_INTERCEPTION_METRES = 0.7f;
    static constexpr float BASE_INTERCEPTION_RISK = 0.55f;
    static constexpr float LATE_LANE_RISK = 0.35f;
    /** Binturong brain: a man already standing in the lane close to the
     * passer blocks the pass without having to react to it. There the lane
     * is only as wide as his reach (a leg and the ball), and a ball into it
     * is almost always stopped. Further on the ball takes longer to reach
     * him: over the ramp (about a reaction plus a stride or two at pass
     * speed) the lane widens to the full interception radius and the risk
     * eases to the base and late weights above. */
    static constexpr float NEAR_LANE_REACH_METRES = 1.1f;
    static constexpr float NEAR_LANE_RISK = 0.9f;
    static constexpr float LANE_REACH_RAMP_METRES = 8.0f;
    static constexpr float OFFSIDE_MARGIN_METRES = 0.42f;
  };

  struct Shooting final
  {
    static constexpr float PRESSURE_RADIUS_METRES = 4.0f;
    static constexpr float PRESSURE_PENALTY = 0.45f;
    // Logistic xG location model (Soccermatics fit on Wyscout PL 2017/18):
    // a central shot from 11 m is worth ~0.18, from 20 m ~0.06.
    static constexpr float XG_INTERCEPT = 0.5103f;
    static constexpr float XG_ANGLE = 0.6338f;
    static constexpr float XG_DISTANCE = -0.2798f;
    static constexpr float XG_LINE = 0.1243f;
    static constexpr float XG_LATERAL = -0.0300f;
    static constexpr float XG_LINE_SQUARED = 0.0014f;
    static constexpr float XG_LATERAL_SQUARED = 0.0041f;
    static constexpr float XG_ANGLE_LINE = -0.1251f;
    static constexpr float XG_MAX_DISTANCE_METRES = 38.0f;
    static constexpr float BASE_SKILL_FACTOR = 0.62f;
    static constexpr float SHOOTING_SKILL_FACTOR = 0.45f;
    static constexpr float MIN_OPEN_PLAY_XG = 0.005f;
    static constexpr float MAX_OPEN_PLAY_XG = 0.64f;
    static constexpr float PENALTY_XG = 0.78f;
    static constexpr float HEADER_XG_FACTOR = 0.45f;
    static constexpr float GOAL_HALF_WIDTH_METRES = 3.66f;
    static constexpr float KEEPER_CENTRED_METRES = 0.3f;
    static constexpr float NEAR_SIDE_AIM_CHANCE = 0.25f;
    // Share of shots simply struck hard at the frame rather than placed.
    static constexpr float POWER_SHOT_BASE_CHANCE = 0.75f;
    static constexpr float POWER_SHOT_SKILL_REDUCTION = 0.10f;
    static constexpr float POWER_SHOT_WIDTH_METRES = 1.3f;
    static constexpr float AIM_POST_INSET_BASE = 0.45f;
    static constexpr float AIM_POST_INSET_SKILL = 0.5f;
    static constexpr float AIM_INSET_SPREAD = 1.2f;
    static constexpr float PENALTY_AIM_INSET_SPREAD = 0.5f;
    static constexpr float AIM_MIN_HEIGHT_METRES = 0.2f;
    static constexpr float AIM_MAX_HEIGHT_METRES = 1.9f;
    static constexpr float HEADER_AIM_MAX_HEIGHT_METRES = 1.4f;
    static constexpr float ERROR_BASE_METRES = 3.45f;
    static constexpr float ERROR_SKILL_METRES = 2.4f;
    static constexpr float ERROR_PRESSURE_METRES = 1.8f;
    static constexpr float ERROR_REFERENCE_METRES = 16.0f;
    static constexpr float ERROR_MIN_DISTANCE_SCALE = 0.55f;
    static constexpr float PENALTY_ERROR_SCALE = 0.16f;
    static constexpr float VERTICAL_ERROR_SHARE = 0.6f;
    static constexpr float MIN_CROSSING_HEIGHT_METRES = 0.12f;
    static constexpr float MAX_CROSSING_HEIGHT_METRES = 6.0f;
    // Struck shots leave the foot at 21-33 m/s (lab instep kicks ~28 m/s).
    // Aardvark body, and Binturong headers; a Binturong foot shot is struck
    // at a share of his shot power instead.
    static constexpr float BASE_BALL_SPEED = 21.0f;
    static constexpr float SHOOTING_SPEED_BONUS = 9.0f;
    static constexpr float POWER_SPEED_BONUS = 3.0f;
    static constexpr float MIN_SPEED_VARIATION = 0.88f;
    /** Binturong body (shooting technique plan step 1): shot power, the
     * fastest he can strike a shot, is his kick power with Shooting in
     * place of Passing (Passing::MIN_KICK_SPEED at 1 to MAX_KICK_SPEED at
     * 99). A foot shot is struck at this share of it: about the old shot's
     * pace for a typical shooter (26.5 m/s at Shooting 60, Physicality 60). */
    static constexpr float DEFAULT_POWER_SHARE = 0.9f;
    /** Binturong body: a foot shot's launch, the share of his power as the
     * ball's whole launch speed, is solved to cross the line at the height
     * he aimed for; a strike that would need more than this (deg) up, or
     * more than its pace, is too weak for a shot's line and is flown as a
     * pass beyond his power (the flattest angle that gets there, or the one
     * that gets furthest). */
    static constexpr float MAX_LAUNCH_DEGREES = 30.0f;
    /** Shot types (Binturong body, shooting technique plan step 5; first
     * guesses). The share of his shot power each part of the foot can use
     * (the driven shot, the instep: all of it), and the share of his shot
     * power each is struck at unless a drill says otherwise (the driven
     * one: DEFAULT_POWER_SHARE; a chip: what gets it there). */
    static constexpr float PLACED_FOOT_SHARE = 0.75f;
    static constexpr float CURLED_FOOT_SHARE = 0.85f;
    static constexpr float CHIP_FOOT_SHARE = 0.6f;
    static constexpr float TOE_FOOT_SHARE = 0.7f;
    static constexpr float PLACED_POWER_SHARE = 0.6f;
    static constexpr float CURLED_POWER_SHARE = 0.75f;
    static constexpr float TOE_POWER_SHARE = 0.65f;
    /** The spin each is struck with (rev/s; held to his spin capacity, by
     * Shooting as a pass's is by Passing): a little topspin on a driven
     * shot to dip it, sidespin on a curled one (bent in towards the middle
     * of the goal), backspin on a chip. */
    static constexpr float DRIVEN_TOPSPIN_REVS = 2.0f;
    static constexpr float CURLED_SIDESPIN_REVS = 7.0f;
    static constexpr float CHIP_BACKSPIN_REVS = 9.0f;
    /** A chip clears the keeper's standing reach by this much where he is;
     * with no keeper off his line, a chip pass's clearance. It comes down
     * through the height he aimed for on the goal line. */
    static constexpr float CHIP_KEEPER_MARGIN_METRES = 0.3f;
    /** Aardvark body only: a shot leaves at this height (Binturong: the
     * height the ball is at). */
    static constexpr float RELEASE_HEIGHT_METRES = 0.2f;
    static constexpr float WOODWORK_BAND_METRES = 0.10f;
    static constexpr float WOODWORK_REBOUND = 0.35f;
    static constexpr float MAX_SET_PIECE_XG = 0.82f;
    static constexpr float MIN_GOAL_PROBABILITY = 0.01f;
    static constexpr float MIN_ACTION_COOLDOWN = 1.5f;
    static constexpr float MAX_ACTION_COOLDOWN = 2.5f;
  };

  /**
   * Strikes carried out by the contact model (Binturong body; Notes and
   * plans/match_engine_contact_plan.md, step 3): each kind's profile, the
   * defaults of the strike knobs (match_strike_knobs.h). The condition
   * levels are on the touch's scale; each condition adds its level to both
   * jobs. Not tuned: ground and lofted passes and crosses are first
   * guesses; shots and headers start from the old spread's size (Â§13).
   */
  struct Strike final
  {
    static constexpr float LOW = 0.05f;
    static constexpr float MEDIUM = 0.10f;
    static constexpr float HIGH = 0.20f;
    static constexpr float VERY_HIGH = 0.30f;

    /** What every kind shares unless it says otherwise. */
    struct Profile
    {
      /** The jobs' sizes: power demand (the pace change against the
       * reference speed) and the turn off the ball's line. */
      static constexpr float PACE_CHANGE = 0.2f;
      static constexpr float REFERENCE_SPEED = 25.0f;
      static constexpr float TURN_ANGLE = 0.3f;
      /** The turn counts in full when the ball comes at this pace (m/s),
       * in proportion below it: a ball barely rolling has no line worth
       * the name. */
      static constexpr float TURN_PACE_REFERENCE = 10.0f;
      /** Power asked (shooting technique plan step 3): the pace he asks
       * for against his power for the kind (shot power for shots, kick
       * power for kicks). Nothing below the comfort share; above it, the
       * square of how far into the rest he goes (1 at all of it). What it
       * adds to the angle and weight jobs: none unless a kind says so. */
      static constexpr float POWER_ASKED_ANGLE = 0.0f;
      static constexpr float POWER_ASKED_WEIGHT = 0.0f;
      static constexpr float POWER_COMFORT_SHARE = 0.7f;
      /** Power by angle (shooting technique plan step 4; first guesses):
       * the share of his power left for a strike square to his body, across
       * it (inward: the instep swings through) and away from it (outward:
       * the outside of the foot), and straight behind him. Only read
       * through power asked. */
      static constexpr float POWER_INWARD_SQUARE = 0.8f;
      static constexpr float POWER_OUTWARD_SQUARE = 0.6f;
      static constexpr float POWER_BEHIND = 0.3f;
      // The conditions' levels (both jobs).
      static constexpr float INCOMING_PACE = MEDIUM;
      static constexpr float OWN_SPEED = MEDIUM;
      static constexpr float STRETCH = HIGH;
      static constexpr float WEAKER_FOOT = HIGH;
      static constexpr float OFF_GROUND = HIGH;
      static constexpr float PRESSURE = MEDIUM;
      /** Where the ball comes from against his facing (1 from behind), as
       * the touch weighs it. Only a ball coming to him has a direction:
       * his own ball at his feet reads where it lies, in front of him. */
      static constexpr float APPROACH_ANGLE = FirstTouch::APPROACH.angle;
      static constexpr float APPROACH_WEIGHT = FirstTouch::APPROACH.weight;
      /** As the touch's: a body in the way (1.5 m). */
      static constexpr float PRESSURE_RADIUS_METRES = 1.5f;
      // Technique: how often each job goes wrong, and how badly.
      static constexpr float SKILL_REDUCTION = 0.9f;
      static constexpr float ANGLE_FAIL_BASE = 0.05f;
      static constexpr float ANGLE_FAIL_RANGE = 0.5f;
      static constexpr float WEIGHT_FAIL_BASE = 0.05f;
      static constexpr float WEIGHT_FAIL_RANGE = 0.5f;
      /** A failed angle: a fixed miss (rad), not a share of the turn (the
       * turn only makes it likelier). */
      static constexpr float ANGLE_MISS_BASE = 0.0f;
      static constexpr float ANGLE_MISS_RANGE = 0.0f;
      static constexpr float ANGLE_MISS_ABSOLUTE_BASE = 0.04f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.2f;
      /** A failed weight: a share of the pace he puts on it. */
      static constexpr float WEIGHT_MISS_BASE = 0.15f;
      static constexpr float WEIGHT_MISS_RANGE = 0.4f;
      static constexpr float UNDER_TURN_SHARE = 0.85f;
      static constexpr float HEAVY_SHARE = 0.4f;
      /** A clean strike's jitter: radians, and a share of the pace. */
      static constexpr float ANGLE_JITTER = 0.05f;
      static constexpr float WEIGHT_JITTER = 0.04f;
      /** The share of the angle jitter that grows with the angle job's
       * difficulty (shooting technique plan step 3): the jitter is
       * ANGLE_JITTER x (1 - share + share x difficulty). 0: all of it, at
       * any difficulty. */
      static constexpr float JITTER_DIFFICULTY_SHARE = 0.0f;
      /** The share of an angle miss and jitter that goes into the
       * elevation (0: along the ground), and of those the share up. */
      static constexpr float VERTICAL_SHARE = 0.0f;
      static constexpr float UP_SHARE = 0.5f;
      /** Above 0: the elevation has its own jitter (rad), and a failed
       * angle its own miss, independent of the line's, which then keeps
       * all of its own (the vertical share is not used). */
      static constexpr float ELEVATION_JITTER = 0.0f;
    };

    /** Ground passes (step 3a). Skill: Passing. Failed passes are more
     * often under-hit than over-hit. */
    struct GroundPass final : Profile
    {
      static constexpr float OWN_SPEED = LOW;
      static constexpr float STRETCH = MEDIUM;
      static constexpr float WEAKER_FOOT = MEDIUM;
    };

    /** Lofted passes (step 3b). Skill: Passing. Struck at 15-25 m/s: the
     * reference for their power demand, so the usual long ball is not the
     * hardest there is. A failed weight misses by a smaller share (the
     * pace is two or three times a ground pass's). A long ball as often
     * high as low. Its line and its steepness each have their own error:
     * the line as wide as a ground pass's, the steepness on top. */
    struct Lofted : Profile
    {
      static constexpr float PACE_CHANGE = 0.6f;
      static constexpr float REFERENCE_SPEED = 35.0f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.15f;
      static constexpr float WEIGHT_MISS_BASE = 0.05f;
      static constexpr float WEIGHT_MISS_RANGE = 0.15f;
      static constexpr float HEAVY_SHARE = 0.5f;
      static constexpr float ANGLE_JITTER = 0.05f;
      static constexpr float WEIGHT_JITTER = 0.03f;
      static constexpr float VERTICAL_SHARE = 0.4f;
      static constexpr float UP_SHARE = 0.5f;
      static constexpr float ELEVATION_JITTER = 0.02f;
    };

    /** Driven passes (pass types, 2026-10-23; first guesses): struck hard
     * with the instep, so their power demand is read as a lofted ball's,
     * and a failed weight misses by a smaller share. A wider line than a
     * ground pass, and some of its error in its height (more often too
     * high than too low). */
    struct DrivenPass final : Profile
    {
      static constexpr float PACE_CHANGE = 0.6f;
      static constexpr float REFERENCE_SPEED = 35.0f;
      static constexpr float WEIGHT_MISS_BASE = 0.05f;
      static constexpr float WEIGHT_MISS_RANGE = 0.15f;
      static constexpr float ANGLE_JITTER = 0.06f;
      static constexpr float WEIGHT_JITTER = 0.03f;
      static constexpr float UP_SHARE = 0.6f;
      static constexpr float ELEVATION_JITTER = 0.015f;
    };

    /** Chips (pass types, 2026-10-23; first guesses): a lofted ball
     * stabbed at, hardest on its weight and its steepness. */
    struct Chip final : Lofted
    {
      static constexpr float WEIGHT_FAIL_BASE = 0.1f;
      static constexpr float WEIGHT_MISS_BASE = 0.1f;
      static constexpr float WEIGHT_MISS_RANGE = 0.3f;
      static constexpr float WEIGHT_JITTER = 0.06f;
      static constexpr float ELEVATION_JITTER = 0.04f;
    };

    /** Crosses (step 3b): a lofted ball, more often over everyone. Still
     * one error shared between the line and the steepness. */
    struct Cross final : Lofted
    {
      static constexpr float ANGLE_JITTER = 0.04f;
      static constexpr float VERTICAL_SHARE = 0.5f;
      static constexpr float UP_SHARE = 0.65f;
      static constexpr float ELEVATION_JITTER = 0.0f;
    };

    /** Shots (step 3c; shooting technique plan step 3, 2026-10-24: first
     * guesses). Skill: Shooting, cutting the errors as a pass's by Passing
     * (0.9). Power costs precision: the power asked (pace against his shot
     * power, nothing up to 70% of it, steeply beyond) adds to the angle job
     * and the weight job, in place of the pace change against a fixed
     * 40 m/s. The jitter grows with the angle job's difficulty from a
     * small floor (15%), so an easy strike is tight and the spread comes
     * from power, pressure and the rest; 0.375 of it vertical. On top,
     * failures grow with the conditions: usually skied, a failed weight
     * scuffed. */
    struct Shot : Profile
    {
      static constexpr float PACE_CHANGE = 0.0f;
      static constexpr float POWER_ASKED_ANGLE = 0.6f;
      static constexpr float POWER_ASKED_WEIGHT = 0.6f;
      static constexpr float POWER_COMFORT_SHARE = 0.7f;
      static constexpr float REFERENCE_SPEED = 40.0f;
      static constexpr float TURN_ANGLE = 0.6f;
      static constexpr float INCOMING_PACE = HIGH;
      static constexpr float OFF_GROUND = VERY_HIGH;
      static constexpr float PRESSURE = HIGH;
      static constexpr float PRESSURE_RADIUS_METRES = 1.5f;
      static constexpr float SKILL_REDUCTION = 0.9f;
      static constexpr float ANGLE_FAIL_BASE = 0.0f;
      static constexpr float ANGLE_FAIL_RANGE = 0.4f;
      static constexpr float WEIGHT_FAIL_BASE = 0.0f;
      static constexpr float WEIGHT_FAIL_RANGE = 0.4f;
      static constexpr float ANGLE_MISS_ABSOLUTE_BASE = 0.05f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.3f;
      static constexpr float WEIGHT_MISS_BASE = 0.1f;
      static constexpr float WEIGHT_MISS_RANGE = 0.3f;
      static constexpr float HEAVY_SHARE = 0.3f;
      static constexpr float ANGLE_JITTER = 0.5f;
      static constexpr float JITTER_DIFFICULTY_SHARE = 0.85f;
      static constexpr float WEIGHT_JITTER = 0.05f;
      static constexpr float VERTICAL_SHARE = 0.375f;
      static constexpr float UP_SHARE = 0.7f;
      /** The pace he means: the old shot's mean of its 0.88-1 variation
       * (the weight jitter is the variation now). */
      static constexpr float MEAN_SPEED_SHARE = 0.94f;
    };

    /** Headers (step 3c): shots, clearances and knock-downs. Skill:
     * Shooting 70%, Physicality 30%. A header is off the ground and off
     * no foot by nature; the ball comes in fast. The old header shot had
     * the shot's spread, and keeps it: the shot's power cost and
     * difficulty-driven jitter (2026-10-24) are the foot's. */
    struct Header final : Shot
    {
      static constexpr float PACE_CHANGE = 0.4f;
      static constexpr float POWER_ASKED_ANGLE = 0.0f;
      static constexpr float POWER_ASKED_WEIGHT = 0.0f;
      // Skill: Heading (heading technique plan step 5). The easiest header's
      // clean error (a gentle dropping ball, square on, 10 m: angle
      // difficulty about 0.09), its mean miss along the ground: 20 deg at
      // Heading 1 to 0.5 deg at 99 (decision 6), in a straight line. As for
      // shots, the jitter grows with the angle job's difficulty: ANGLE_JITTER
      // x (1 - share + share x difficulty) x (1 - Heading x SKILL_REDUCTION),
      // a standard deviation, set so the drill's mean miss comes out at
      // those figures; the hardest header is about 2.4 times the easiest.
      // All of it sideways; the elevation has its own (a placeholder: 5 deg
      // at 1 to 0.1 at 99).
      static constexpr float SKILL_REDUCTION = 0.985f;
      static constexpr float ANGLE_JITTER = 1.016f;
      static constexpr float JITTER_DIFFICULTY_SHARE = 0.65f;
      static constexpr float VERTICAL_SHARE = 0.0f;
      static constexpr float ELEVATION_JITTER = 0.104f;
      static constexpr float REFERENCE_SPEED = 25.0f;
      // No foot, no spin, always in the air.
      static constexpr float WEAKER_FOOT = 0.0f;
      static constexpr float OFF_GROUND = 0.0f;
      // A ball dropping over his shoulder is hard to head: he can't see it.
      // Weighted half as much again as for a foot.
      static constexpr float APPROACH_ANGLE = 1.5f * FirstTouch::APPROACH.angle;
      static constexpr float APPROACH_WEIGHT = 1.5f * FirstTouch::APPROACH.weight;
      // A head's power by angle (heading technique plan step 3): all of it
      // the way he faces, a flick of the neck square to his body, a glance
      // behind; the same either side (placeholders).
      static constexpr float POWER_INWARD_SQUARE = 0.35f;
      static constexpr float POWER_OUTWARD_SQUARE = 0.35f;
      static constexpr float POWER_BEHIND = 0.1f;
    };

    /** Shot types (shooting technique plan step 5, 2026-10-24; first
     * guesses, untuned), each read as the driven shot (Shot) but for
     * these. Skill: Shooting. Power asked reads each against its part of
     * the foot's share of his power (Shooting::*_FOOT_SHARE). */
    /** Side-footed: a tighter line (the inside of the foot). */
    struct PlacedShot final : Shot
    {
      static constexpr float ANGLE_JITTER = 0.35f;
      static constexpr float UP_SHARE = 0.6f;
    };
    /** Bent with the instep's wrap: a little wider than driven. */
    struct CurledShot final : Shot
    {
      static constexpr float ANGLE_JITTER = 0.55f;
    };
    /** Stabbed under the ball: its weight and steepness the hardest to
     * judge (as a chip pass's against a lofted one), with its own
     * elevation error. */
    struct ChipShot final : Shot
    {
      static constexpr float WEIGHT_MISS_BASE = 0.15f;
      static constexpr float WEIGHT_MISS_RANGE = 0.4f;
      static constexpr float WEIGHT_JITTER = 0.08f;
      static constexpr float UP_SHARE = 0.5f;
      static constexpr float ELEVATION_JITTER = 0.04f;
    };
    /** The toe: little control of the line or the pace. */
    struct ToePoke final : Shot
    {
      static constexpr float ANGLE_JITTER = 0.8f;
      static constexpr float WEIGHT_JITTER = 0.1f;
    };

    /** Clearances (step 3c): precision barely matters, height and distance
     * do. Skill: Defending. */
    struct Clearance final : Profile
    {
      static constexpr float PACE_CHANGE = 0.3f;
      static constexpr float REFERENCE_SPEED = 35.0f;
      static constexpr float WEAKER_FOOT = MEDIUM;
      static constexpr float OFF_GROUND = MEDIUM;
      // Passes and shots only, for now.
      static constexpr float APPROACH_ANGLE = 0.0f;
      static constexpr float APPROACH_WEIGHT = 0.0f;
      static constexpr float SKILL_REDUCTION = 0.9f;
      static constexpr float ANGLE_MISS_ABSOLUTE_BASE = 0.1f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.3f;
      static constexpr float WEIGHT_MISS_BASE = 0.1f;
      static constexpr float WEIGHT_MISS_RANGE = 0.3f;
      static constexpr float HEAVY_SHARE = 0.3f;
      static constexpr float ANGLE_JITTER = 0.15f;
      static constexpr float WEIGHT_JITTER = 0.08f;
      static constexpr float VERTICAL_SHARE = 0.4f;
      static constexpr float UP_SHARE = 0.6f;
    };

    /** Throws (step 3c): throw-ins, and a keeper's ball rolled or thrown
     * from his hands. Skill: Passing. Two hands and a still ball: no
     * stretch, weaker foot or ball off the ground. */
    struct Throw final : Profile
    {
      static constexpr float INCOMING_PACE = 0.0f;
      static constexpr float STRETCH = 0.0f;
      static constexpr float WEAKER_FOOT = 0.0f;
      static constexpr float OFF_GROUND = 0.0f;
      static constexpr float APPROACH_ANGLE = 0.0f;
      static constexpr float APPROACH_WEIGHT = 0.0f;
      static constexpr float ANGLE_FAIL_BASE = 0.03f;
      static constexpr float ANGLE_FAIL_RANGE = 0.4f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.15f;
      static constexpr float WEIGHT_MISS_BASE = 0.1f;
      static constexpr float WEIGHT_MISS_RANGE = 0.3f;
      static constexpr float HEAVY_SHARE = 0.5f;
      static constexpr float ANGLE_JITTER = 0.04f;
      static constexpr float VERTICAL_SHARE = 0.3f;
    };
  };

  struct Defending final
  {
    /** Reach of a standing challenge and of a sliding one (to the ball). */
    static constexpr float TACKLE_DISTANCE_METRES = 1.3f;
    static constexpr float TACKLE_DEFENDING_REACH_METRES = 0.3f;
    static constexpr float SLIDE_TACKLE_DISTANCE_METRES = 2.2f;
    /** Engagement waits for the ball to be away from the attacker's foot. */
    static constexpr float CLOSE_CONTROL_ENGAGE_SHARE = 0.25f;
    static constexpr float EXPOSED_ENGAGE_SHARE = 1.75f;
    static constexpr float EXPOSURE_WIN_BONUS = 0.35f;
    static constexpr float SHIELD_PHYSICALITY_PENALTY = 0.15f;
    static constexpr float SLIDE_WIN_PENALTY = 0.05f;
    static constexpr float FROM_BEHIND_WIN_PENALTY = 0.10f;
    static constexpr float FROM_BEHIND_FOUL_FACTOR = 2.2f;
    static constexpr float SLIDE_FOUL_FACTOR = 1.6f;
    static constexpr float EXPOSED_FOUL_RELIEF = 0.6f;
    static constexpr float ENGAGE_RATE_PER_SECOND = 0.35f;
    static constexpr float PRESSING_ENGAGE_BONUS = 0.3f;
    static constexpr float FINAL_THIRD_ENGAGE_FACTOR = 2.5f;
    static constexpr float MIN_TACKLE_COOLDOWN = 1.4f;
    static constexpr float MAX_TACKLE_COOLDOWN = 3.2f;
    static constexpr float PRESSING_COOLDOWN_REDUCTION = 0.25f;
    static constexpr float TACKLE_COOLDOWN_BASE_MULTIPLIER = 1.1f;
    static constexpr float BASE_WIN_CHANCE = 0.12f;
    static constexpr float DEFENDING_WIN_BONUS = 0.42f;
    static constexpr float DRIBBLING_WIN_PENALTY = 0.27f;
    // Pressing wins more challenges by making more of them, not better ones:
    // rushed challenges win slightly less often and foul more.
    static constexpr float PRESSING_WIN_EFFECT = -0.07f;
    static constexpr float PRESSING_FOUL_BONUS = 0.06f;
    static constexpr float MIN_WIN_CHANCE = 0.08f;
    static constexpr float MAX_WIN_CHANCE = 0.58f;
    static constexpr float PHYSICALITY_DUEL_WEIGHT = 0.12f;
    static constexpr float BASE_FOUL_CHANCE = 0.24f;
    static constexpr float RISK_FOUL_BONUS = 0.10f;
    static constexpr float TECHNIQUE_FOUL_BONUS = 0.10f;
    static constexpr float WINNING_TACKLE_FOUL_SHARE = 0.25f;
    static constexpr float PENALTY_AREA_FOUL_SCALE = 0.13f;
    /** A free-ball tackle's "win it" nudge (tackling plan step 3, agreed
     * 2026-10-26): it stops this far from where he met it, by Tackling
     * (Defending stands in): far for the worst, close for the best. */
    static constexpr float NUDGE_METRES_POOR = 3.0f;
    static constexpr float NUDGE_METRES_BEST = 1.5f;
    /** Goal-side and face on, forward (away from his goal) and this far
     * round to the side. */
    static constexpr float FACE_ON_NUDGE_DEGREES = 45.0f;
    /** Within this of the attacker's line he is dead centre: the side is
     * his weaker foot's (placeholder). */
    static constexpr float NUDGE_CENTRE_METRES = 0.2f;
    /** The block (tackling plan step 4; placeholders tried 2026-10-27). A
     * push: the pace his foot would give it unopposed, by Tackling. */
    static constexpr float BLOCK_PUSH_POOR = 4.0f;
    static constexpr float BLOCK_PUSH_BEST = 8.0f;
    /** A foot stuck out in reaction (a wall). */
    static constexpr float BLOCK_REACTING_SHARE = 0.6f;
    /** Lost at full stretch (the stretch map's 1), and flat out (his speed
     * at his top speed). */
    static constexpr float BLOCK_STRETCH_LOSS = 0.5f;
    static constexpr float BLOCK_SPEED_LOSS = 0.3f;
    /** The squeeze: the share of the opposed pushes' difference lost. */
    static constexpr float BLOCK_SQUEEZE_LOSS = 0.5f;
    /** Popped up: both opposed pushes at least this firm (m/s) and their
     * difference no more than this share of the stronger; then this share
     * of the squeezed pace goes up. */
    static constexpr float BLOCK_POP_MIN = 3.0f;
    static constexpr float BLOCK_POP_EQUAL = 0.25f;
    static constexpr float BLOCK_POP_SHARE = 0.4f;
    /** After a block (tackling plan step 6; my placeholders 2026-10-28):
     * extra foot recovery (s) at full stretch or flat out, in proportion
     * to the larger. */
    static constexpr float BLOCK_RECOVERY_SECONDS = 0.2f;
    /** After a win-it nudge he meets the ball this far (m) to the
     * attacker's side of it: his body between them. */
    static constexpr float STEP_ACROSS_METRES = 0.5f;
    /** Going in (tackling plan step 5; my placeholders 2026-10-28). The
     * cue as he reads the carrier's foot: still recovering from a touch
     * (row 1), his touch due 0.1-0.2 s away (row 2, he swings at nothing),
     * within 0.1 s (row 3, an even block); free with none due is no cue. */
    static constexpr float CUE_RECOVERING = 1.0f;
    static constexpr float CUE_SWING = 0.9f;
    static constexpr float CUE_BLOCK = 0.5f;
    /** Shown too much: the carrier's stretch for it less his own, times
     * this. */
    static constexpr float CUE_SHOWN_SHARE = 1.0f;
    /** He goes in when the cue clears the bar: this, lower pressing (full
     * pressing instruction), against a "press him" order, with cover
     * behind; higher as the last man or in his own box. */
    static constexpr float GO_IN_BAR = 0.6f;
    static constexpr float GO_IN_PRESSING = 0.15f;
    static constexpr float GO_IN_PRESS_ORDER = 0.1f;
    static constexpr float GO_IN_COVER = 0.1f;
    static constexpr float GO_IN_LAST_MAN = 0.15f;
    static constexpr float GO_IN_OWN_BOX = 0.15f;
    static constexpr float GO_IN_BAR_MIN = 0.3f;
    /** A team-mate goal-side of him within this is cover. */
    static constexpr float COVER_METRES = 15.0f;
    /** How far out he reads the carrier's timing (s, either way), by
     * Defending: his judgement. */
    static constexpr float READ_ERROR_POOR = 0.25f;
    static constexpr float READ_ERROR_BEST = 0.05f;
    /** His intent: knock it away within this many metres of his own goal line, at this
     * stretch or more, or as the last man; otherwise win it. A free ball
     * this far (m) from the carrier he simply controls. */
    static constexpr float KNOCK_AWAY_WITHIN_METRES = 34.65f;
    static constexpr float KNOCK_AWAY_STRETCH = 0.6f;
    static constexpr float CONTROL_FREE_BALL_METRES = 2.0f;
    // A won challenge often only pokes the ball loose.
    static constexpr float POKE_LOOSE_CHANCE = 0.45f;
    static constexpr float MIN_POKE_SPEED = 3.5f;
    static constexpr float MAX_POKE_SPEED = 8.0f;
    // Defenders pressed in their own third with no safe pass clear it.
    static constexpr float CLEARANCE_WITHIN_METRES = 34.65f;
    static constexpr float CLEARANCE_MIN_DISTANCE_METRES = 30.0f;
    static constexpr float CLEARANCE_MAX_DISTANCE_METRES = 50.0f;
    static constexpr float CLEARANCE_SPEED = 22.0f;
    static constexpr float CLEARANCE_TOUCHLINE_BIAS = 0.35f;
    static constexpr float MIN_RECOVERY_COOLDOWN = 0.6f;
    static constexpr float MAX_RECOVERY_COOLDOWN = 1.2f;
    /** Reach of an outfield player throwing his body at a shot. */
    static constexpr float BLOCK_DISTANCE_METRES = 1.6f;
    static constexpr float BASE_BLOCK_CHANCE = 0.55f;
    static constexpr float DEFENDING_BLOCK_BONUS = 0.25f;
    static constexpr float DEFLECTION_SPEED_FACTOR = -0.22f;
    static constexpr float MAX_DEFLECTION_Y_SPEED = 5.0f;
  };

  /**
   * Ball physics in SI units: quadratic air drag with a drag crisis, gravity,
   * a restitution bounce, rolling resistance on the grass and spin (sidespin
   * and topspin/backspin) with its Magnus force.
   */
  struct Ball final
  {
    static constexpr float GRAVITY = 9.81f;
    static constexpr float AIR_DENSITY = 1.225f;
    static constexpr float MASS_KG = 0.43f;
    static constexpr float CROSS_SECTION_M2 = 0.038f;
    static constexpr float DRAG_SUBCRITICAL = 0.45f;
    static constexpr float DRAG_SUPERCRITICAL = 0.24f;
    static constexpr float DRAG_CRISIS_LOW_SPEED = 8.0f;
    static constexpr float DRAG_CRISIS_HIGH_SPEED = 14.0f;
    static constexpr float BOUNCE_RESTITUTION = 0.62f;
    static constexpr float BOUNCE_TANGENTIAL_RETAINED = 0.84f;
    static constexpr float MIN_BOUNCE_SPEED = 1.0f;
    static constexpr float ROLLING_DECELERATION = 0.75f;
    static constexpr float ROLLING_VISCOUS_PER_SECOND = 0.05f;
    static constexpr float RADIUS_METRES = 0.11f;
    /** Spin (Binturong physics): the Magnus force is
     * 0.5 rho A C_L v^2 across the ball's path, its lift coefficient C_L
     * this many times the spin ratio (r w / v), at most MAX_LIFT
     * (footballs: about 0.25-0.3 at spin ratios of 0.2-0.3, Goff and Carre
     * 2010; Asai et al. 2007). Below the cap a sidespin turns the heading
     * at a steady TURN_PER_SIDESPIN rad/s per rad/s of spin, whatever the
     * speed (the old curve's heading rotation). */
    static constexpr float LIFT_PER_SPIN_RATIO = 1.0f;
    static constexpr float MAX_LIFT_COEFFICIENT = 0.35f;
    static constexpr float TURN_PER_SIDESPIN = 0.5f * AIR_DENSITY *
                                               CROSS_SECTION_M2 / MASS_KG *
                                               LIFT_PER_SPIN_RATIO *
                                               RADIUS_METRES;
    /** Spin fades slowly in flight (only the air works on it; a first
     * guess, kept from the curve it replaced). */
    static constexpr float SPIN_DECAY_PER_SECOND = 0.35f;
    /** Binturong physics: on the ground the grass bleeds the sidespin off
     * and holds the ball to its line, so a rolling ball's sidespin fades
     * this much faster; a bounce keeps only this share of it. A rolling
     * ball's topspin is its roll (no slipping). First guesses. */
    static constexpr float SIDESPIN_DECAY_ROLLING_PER_SECOND = 2.0f;
    static constexpr float SIDESPIN_RETAINED_ON_BOUNCE = 0.5f;
    /** A bounce's grip (Binturong physics): the share of the way the
     * contact's slip (the ball's speed against its spin's) is taken toward
     * rolling, as for a hollow sphere (I = 2/3 m r^2). Set so a ball with
     * no spin keeps BOUNCE_TANGENTIAL_RETAINED of its speed, as before;
     * backspin then checks it up, topspin sends it on. */
    static constexpr float BALL_INERTIA_SHARE = 2.0f / 3.0f;
    static constexpr float BOUNCE_GRIP =
        (1.0f - BOUNCE_TANGENTIAL_RETAINED) * (1.0f + BALL_INERTIA_SHARE);
    static constexpr float STOP_SPEED = 0.2f;
    /** A spent shot slower than this is a loose ball again. */
    static constexpr float DEAD_SHOT_SPEED = 6.0f;
    static constexpr float MAX_FLIGHT_SECONDS = 6.0f;
    // Reach for first touches: feet for a low ball, body up to the chest.
    static constexpr float GOALKEEPER_CONTROL_RADIUS_METRES = 1.8f;
    static constexpr float OUTFIELD_CONTROL_RADIUS_METRES = 1.0f;
    static constexpr float CHEST_CONTROL_RADIUS_METRES = 0.7f;
    static constexpr float LOW_BALL_METRES = 0.5f;
    // A Binturong body's lines between feet, thigh, chest and head, as
    // shares of his height (chest and thigh control plan step 1): his knee,
    // the top of a raised thigh, the top of his shoulders. 0.50 m, 0.99 m
    // and 1.53 m for a 1.80 m player. The Aardvark body keeps the fixed
    // LOW_BALL_METRES and Aerial::CONTROL_CEILING_METRES.
    static constexpr float FEET_LINE_HEIGHT_SHARE = 0.28f;
    static constexpr float CHEST_LINE_HEIGHT_SHARE = 0.55f;
    static constexpr float HEAD_LINE_HEIGHT_SHARE = 0.85f;
    // How far from his centre a Binturong body's thigh and chest reach a
    // ball, in front of him only (chest and thigh control plan step 2): it
    // has to be right at him. The Aardvark body keeps
    // CHEST_CONTROL_RADIUS_METRES all round.
    static constexpr float THIGH_REACH_METRES = 0.2f;
    static constexpr float CHEST_REACH_METRES = 0.3f;
    // The chest touch (chest and thigh control plan step 3; placeholders).
    // It only takes pace off or turns it, never adds it. He sends it up to
    // CHEST_MAX_SIDE_DEGREES either side of the way he faces, from flat to
    // CHEST_MAX_UP_DEGREES up, never down (he can't lean over the ball);
    // unasked, back the way it came (within that) at
    // CHEST_CUSHION_UP_DEGREES, as softly as he can. At least a share of the pace into his chest comes back off it:
    // CHEST_KEEP_LOW at First Touch 1 to CHEST_KEEP_HIGH at 99.
    static constexpr float CHEST_MAX_SIDE_DEGREES = 45.0f;
    static constexpr float CHEST_MAX_UP_DEGREES = 45.0f;
    static constexpr float CHEST_CUSHION_UP_DEGREES = 30.0f;
    static constexpr float CHEST_KEEP_LOW = 0.35f;
    static constexpr float CHEST_KEEP_HIGH = 0.10f;
    // The thigh touch (chest and thigh control plan step 4; placeholders):
    // the same, but only upward, from THIGH_MIN_UP_DEGREES to
    // THIGH_MAX_UP_DEGREES (it pops up and drops at his feet), up to
    // THIGH_MAX_SIDE_DEGREES either side of the way he faces; unasked at
    // THIGH_CUSHION_UP_DEGREES. Only when he is nearly stopped (no faster
    // than THIGH_MAX_SPEED, m/s); then he stays planted (no acceleration)
    // for THIGH_PLANTED_SECONDS. The share that comes back is the chest's
    // for now (a flatter ball being harder is plan Â§5 question 1).
    static constexpr float THIGH_MAX_SIDE_DEGREES = 20.0f;
    static constexpr float THIGH_MIN_UP_DEGREES = 30.0f;
    static constexpr float THIGH_MAX_UP_DEGREES = 80.0f;
    static constexpr float THIGH_CUSHION_UP_DEGREES = 65.0f;
    static constexpr float THIGH_MAX_SPEED = 1.0f;
    static constexpr float THIGH_PLANTED_SECONDS = 0.3f;
    static constexpr float THIGH_KEEP_LOW = CHEST_KEEP_LOW;
    static constexpr float THIGH_KEEP_HIGH = CHEST_KEEP_HIGH;
    static constexpr float BASE_CONTROL_CHANCE = 0.74f;
    static constexpr float TOUCH_SKILL_BONUS = 0.45f;
    static constexpr float SPEED_CONTROL_PENALTY = 0.0065f;
    static constexpr float MIN_CONTROL_CHANCE = 0.28f;
    static constexpr float MAX_CONTROL_CHANCE = 0.97f;
    // An opponent in the lane only gets a foot to it some of the time: it
    // depends on how far he has to stretch and how fast the ball is.
    static constexpr float INTERCEPT_BASE_CHANCE = 0.62f;
    static constexpr float INTERCEPT_DEFENDING_BONUS = 0.35f;
    static constexpr float INTERCEPT_SPEED_PENALTY = 0.018f;
    static constexpr float INTERCEPT_MIN_CHANCE = 0.08f;
    static constexpr float FAILED_TRAP_TIME = 0.55f;
    static constexpr float FAILED_TOUCH_LOCKOUT = 0.3f;
    static constexpr float HEAVY_TOUCH_RETAINED = 0.25f;
    static constexpr float MIN_HEAVY_TOUCH_SPEED = 1.5f;
    static constexpr float MAX_HEAVY_TOUCH_SPEED = 4.5f;
    static constexpr float BASE_TRAP_TIME = 0.25f;
    static constexpr float TRAP_SKILL_PENALTY = 0.35f;
    static constexpr float MIN_POST_TOUCH_DELAY = 0.5f;
    static constexpr float MAX_POST_TOUCH_DELAY = 0.9f;
    static constexpr float PASSING_TOUCH_WEIGHT = 0.85f;
    static constexpr float KEEPER_FEET_WEIGHT = 0.7f;
    static constexpr float SAVE_DISTANCE_METRES = 1.8f;
    static constexpr float SAVE_DIVE_TIME = 1.2f;
    /** A keeper holding the ball waits for his team to push up. */
    static constexpr float MIN_KEEPER_HOLD_SECONDS = 2.5f;
    static constexpr float MAX_KEEPER_HOLD_SECONDS = 5.0f;
  };

  struct Rules final
  {
    static constexpr int MAX_SUBSTITUTIONS_PER_TEAM = 5;
    /** One more change (and window) once a tie goes to extra time. */
    static constexpr int EXTRA_TIME_SUBSTITUTIONS = 1;
    static constexpr long MINIMUM_PLAYERS = 7;
    static constexpr float PARKED_PLAYER_OFFSET_METRES = 6.3f;
    static constexpr float PARKED_PLAYER_SPACING_METRES = 2.625f;
    /** A share of the length (formation frame, on purpose). */
    static constexpr float DEFENSIVE_SLOT_DEPTH = 0.30f;
    static constexpr float SHORT_HANDED_DROP = 0.02f;
    // Crowd-driven home advantage on execution; referee bias is modelled in
    // Discipline (card biases).
    static constexpr float HOME_EXECUTION_BONUS = 0.15f;
    /** Home crowd lift on technical and mental attributes (stretched
     * units), worth a few rating points. */
    static constexpr float HOME_ATTRIBUTE_BONUS = 0.19f;
    /** Execution, decision and duel edge per player of numerical advantage
     * (a side reduced to ten is stretched and loses confidence). */
    static constexpr float NUMERICAL_EDGE_PER_PLAYER = 0.20f;
    static constexpr int MAX_NUMERICAL_EDGE_PLAYERS = 3;
    /** Weight of the team edge on the attributes used in duels. */
    static constexpr float DUEL_EDGE_WEIGHT = 0.8f;
    /** The final third: shares of the length, on purpose. */
    static constexpr float HOME_FINAL_THIRD_START = 0.67f;
    static constexpr float AWAY_FINAL_THIRD_START = 0.33f;
  };

  /** Goalkeeper model in real seconds, metres and m/s. */
  struct Goalkeeper final
  {
    /** A ball (not a shot) going into his own goal that nobody else reaches
     * first (user 2026-11-11): read this far ahead along its roll, in these
     * steps (design choices, not researched). He reacts after his
     * KeeperHands reaction time, from when it first reads as going in. */
    static constexpr float OWN_GOAL_LOOK_SECONDS = 4.0f;
    static constexpr float OWN_GOAL_LOOK_STEP_SECONDS = 0.04f;
    // A set keeper reads the striker's body shape, so his effective
    // reaction to a struck ball is shorter than a raw 0.21 s reaction time.
    static constexpr float REACTION_BASE_SECONDS = 0.12f;
    static constexpr float REACTION_SKILL_SECONDS = 0.08f;
    static constexpr float SCREENED_REACTION_SECONDS = 0.12f;
    static constexpr float SCREEN_WIDTH_METRES = 0.9f;
    static constexpr float READ_ERROR_METRES = 0.8f;
    static constexpr float DIVE_POST_MARGIN_METRES = 1.36f;
    static constexpr float DIVE_ACCELERATION_METRES = 14.0f;
    static constexpr float DIVE_BASE_SPEED_METRES = 4.2f;
    static constexpr float DIVE_SKILL_SPEED_METRES = 1.6f;
    static constexpr float BODY_REACH_METRES = 2.0f;
    static constexpr float HAND_REACH_HEIGHT_SHARE = 0.45F;
    static constexpr float DIVE_REACH_HEIGHT_SHARE = 0.85F;
    static constexpr float SHOULDER_HEIGHT_SHARE = 0.8F;
    static constexpr float MIN_VERTICAL_REACH_MARGIN = 0.01F;
    static constexpr float HEIGHT_REACH_GAIN = 0.8f;
    static constexpr float HIGH_BALL_METRES = 1.9f;
    static constexpr float HIGH_BALL_REACH_SCALE = 0.85f;
    static constexpr float COMFORT_SPEED_METRES = 24.0f;
    static constexpr float SAVE_BASE = 0.97f;
    static constexpr float SAVE_STRETCH_PENALTY = 0.35f;
    static constexpr float SAVE_SPEED_PENALTY = 0.015f;
    static constexpr float SAVE_SKILL_BONUS = 0.22f;
    static constexpr float MIN_SAVE_CHANCE = 0.05f;
    static constexpr float MAX_SAVE_CHANCE = 0.98f;
    static constexpr float HOLD_BASE = 0.35f;
    static constexpr float HOLD_SKILL = 0.40f;
    static constexpr float HOLD_STRETCH_PENALTY = 0.45f;
    static constexpr float HOLD_SPEED_PENALTY = 0.02f;
    static constexpr float MIN_HOLD_CHANCE = 0.05f;
    static constexpr float MAX_HOLD_CHANCE = 0.90f;
    static constexpr float TIP_OVER_METRES = 2.05f;
    static constexpr float TIP_AROUND_STRETCH = 0.5f;
    static constexpr float PARRY_SPEED_SHARE = 0.35f;
    static constexpr float PARRY_MAX_VERTICAL_SPEED = 4.0f;
    static constexpr float RECOVER_TIME_SECONDS = 1.1f;
    static constexpr float DISTRIBUTE_TIME_SECONDS = 0.6f;
    static constexpr float PENALTY_READ_BASE = 0.0f;
    static constexpr float PENALTY_READ_SKILL = 0.12f;
    static constexpr float PENALTY_STAY_CHANCE = 0.08f;
    static constexpr float PENALTY_DIVE_METRES = 1.2f;
    /** Even a keeper who guesses right rarely reaches a placed penalty. */
    static constexpr float PENALTY_SAVE_FACTOR = 0.30f;
    static constexpr float CLAIM_LOOKAHEAD_SECONDS = 0.95f;
    static constexpr float CLAIM_BOX_DEPTH_METRES = 6.0f;
    static constexpr float SWEEP_ADVANTAGE = 1.25f;
    static constexpr float RUSH_COVER_DISTANCE_METRES = 5.0f;
    static constexpr float RUSH_CLOSING_SHARE = 0.55f;
    static constexpr float NEAR_DEPTH_METRES = 0.8f;
    static constexpr float DEPTH_PER_METRE = 0.11f;
    static constexpr float SWEEPER_DISTANCE_METRES = 35.0f;
    static constexpr float SWEEPER_DEPTH_PER_METRE = 0.18f;
    static constexpr float MAX_DEPTH_METRES = 16.0f;
  };

  struct SetPiece final
  {
    static constexpr float CROSSING_FREE_KICK_METRES = 45.0f;
    static constexpr float DIRECT_FREE_KICK_METRES = 30.0f;
    static constexpr float DIRECT_FREE_KICK_WIDTH_METRES = 16.0f;
    static constexpr float CROSS_MIN_WIDTH_METRES = 16.0f;
    static constexpr float DIRECT_SHOT_BASE = 0.55f;
    static constexpr float DIRECT_SHOT_SKILL = 0.45f;
    static constexpr float DIRECT_SHOT_DISTANCE_PENALTY = 0.012f;
    static constexpr float DIRECT_FREE_KICK_XG = 0.065f;
    static constexpr float WALL_BLOCK_BASE = 0.34f;
    static constexpr float WALL_BLOCK_SKILL = 0.20f;
    static constexpr float WALL_REBOUND_SPEED_SHARE = 0.35f;
    /** The wall hides the strike: the keeper reacts this much later. */
    static constexpr float WALL_SCREEN_SECONDS = 0.55f;
    /** A dead ball is struck with more precision and aimed higher and
     * tighter to the post than a shot on the move. */
    static constexpr float FREE_KICK_ERROR_SCALE = 0.5f;
    static constexpr float FREE_KICK_AIM_INSET_SPREAD = 0.6f;
    static constexpr float FREE_KICK_AIM_MIN_HEIGHT_METRES = 1.2f;
    static constexpr float SHORT_CORNER_CHANCE = 0.12f;
    static constexpr float SHORT_CORNER_MAX_DISTANCE_METRES = 14.0f;
    static constexpr float NEAR_POST_CHANCE = 0.42f;
    static constexpr float TARGET_RANDOMNESS = 0.6f;
    /** Weight of a box attacker's heading reach when choosing the target. */
    static constexpr float TARGET_THREAT_WEIGHT = 0.0f;
    static constexpr std::size_t REST_DEFENDERS = 3;
    static constexpr float MARKING_GOAL_SIDE_OFFSET_METRES = 0.84f;
    static constexpr float PENALTY_WAIT_OFFSET_METRES = 1.05f;
    static constexpr float SET_PIECE_PHASE_SECONDS = 20.0f;
    // Defensive clearances and blocks near the own goal line often go out
    // for a corner.
    static constexpr float CLEARANCE_BEHIND_CHANCE = 0.6f;
    /** A defender heading a delivery away near his goal line. */
    static constexpr float HEADER_BEHIND_CHANCE = 0.33f;
    /** Nearer his own goal line than this (m), a defender's header clears
     * the danger rather than finding a team-mate. */
    static constexpr float DEFENSIVE_HEADER_WITHIN_METRES = 47.25f;
    /** Chance that a defender's header or touch of a delivery in front of
     * his goal skews toward it (own goals), and its speed and lift. */
    static constexpr float OWN_GOAL_TOUCH_CHANCE = 0.04f;
    static constexpr float OWN_GOAL_TOUCH_DEPTH_METRES = 9.0f;
    static constexpr float OWN_GOAL_TOUCH_WIDTH_METRES = 9.0f;
    static constexpr float OWN_GOAL_MIN_SPEED = 7.0f;
    static constexpr float OWN_GOAL_MAX_SPEED = 13.0f;
    static constexpr float OWN_GOAL_MAX_LIFT = 2.0f;
    static constexpr float BLOCK_BEHIND_CHANCE = 0.6f;
    static constexpr float BLOCK_BEHIND_WITHIN_METRES = 21.0f;
    static constexpr float CROSS_CLEARANCE_WITHIN_METRES = 26.25f;
  };

  /**
   * Unit conventions. Positions and the ball height `MatchBall::z` are
   * metres (MatchPitch); velocities are in m/s.
   */
  struct Units final
  {
    static constexpr float CROSSBAR_HEIGHT_METRES = 2.44f;
    static constexpr float BALL_RADIUS_METRES = 0.11f;
    static constexpr float DEFAULT_PLAYER_HEIGHT_METRES = 1.80f;
  };

  struct Rating final
  {
    static constexpr float BASELINE = 6.0f;
    static constexpr float MINIMUM = 3.0f;
    static constexpr float MAXIMUM = 10.0f;
    static constexpr float GOAL = 1.05f;
    static constexpr float OWN_GOAL = -0.9f;
    static constexpr float ASSIST = 0.65f;
    static constexpr float SHOT_ON_TARGET = 0.12f;
    static constexpr float SHOT_OFF_TARGET = -0.04f;
    static constexpr float KEY_PASS = 0.18f;
    static constexpr float COMPLETED_PASS = 0.012f;
    static constexpr float FAILED_PASS = -0.035f;
    static constexpr float TACKLE_WON = 0.13f;
    static constexpr float INTERCEPTION = 0.10f;
    static constexpr float CLEARANCE = 0.05f;
    /** A blocked shot (shot block plan step 4): what it earned as a
     * clearance until then (my placeholder). */
    static constexpr float BLOCKED_SHOT = 0.05f;
    /** A blocked pass (pass block plan): as a blocked shot (my
     * placeholder). */
    static constexpr float BLOCKED_PASS = 0.05f;
    static constexpr float AERIAL_WON = 0.04f;
    static constexpr float SAVE = 0.32f;
    static constexpr float GOALKEEPER_GOAL_CONCEDED = -0.38f;
    static constexpr float DEFENDER_GOAL_CONCEDED = -0.14f;
    static constexpr float CLEAN_SHEET_GOALKEEPER = 0.6f;
    static constexpr float CLEAN_SHEET_DEFENDER = 0.35f;
    static constexpr float CLEAN_SHEET_MINIMUM_MINUTES = 60.0f;
    static constexpr float FOUL = -0.06f;
    static constexpr float YELLOW_CARD = -0.35f;
    static constexpr float RED_CARD = -1.6f;
    static constexpr float RESULT_BONUS = 0.25f;
    static constexpr float FULL_MATCH_MINUTES = 90.0f;
  };

  struct Discipline final
  {
    static constexpr float STRICTNESS_SD = 0.13f;
    static constexpr float MIN_STRICTNESS = 0.65f;
    static constexpr float MAX_STRICTNESS = 1.4f;
    /** Bounds of a referee drawn around a league's own mean strictness. */
    static constexpr float MIN_CONTEXT_STRICTNESS = 0.3f;
    static constexpr float MAX_CONTEXT_STRICTNESS = 2.8f;
    static constexpr float RECKLESS_SHARE = 0.17f;
    static constexpr float CARELESS_YELLOW_CHANCE = 0.024f;
    static constexpr float RECKLESS_YELLOW_CHANCE = 0.235f;
    static constexpr float TACTICAL_YELLOW_CHANCE = 0.24f;
    static constexpr float SERIOUS_FOUL_PLAY_SHARE = 0.0015f;
    static constexpr float DOGSO_RED_CHANCE = 0.38f;
    static constexpr float PENALTY_AREA_DOGSO_RED_CHANCE = 0.18f;
    static constexpr float AWAY_CARD_BIAS = 1.07f;
    static constexpr float HOME_CARD_BIAS = 0.94f;
    static constexpr float BOOKED_PLAYER_CAUTION = 0.25f;
    static constexpr float ADVANTAGE_WINDOW_SECONDS = 3.0f;
    static constexpr float ADVANTAGE_MIN_OPENNESS = 0.45f;
    static constexpr float ADVANTAGE_PLAY_CHANCE = 0.45f;
    static constexpr float DOGSO_MAX_DISTANCE_METRES = 28.0f;
    static constexpr float DOGSO_MAX_WIDTH_METRES = 14.96f;
  };

  struct Injury final
  {
    // Non-contact hazard per match minute for a fresh player at moderate
    // intensity; low condition and sprinting raise it.
    static constexpr float BASE_HAZARD_PER_MINUTE = 0.00009f;
    static constexpr float FATIGUE_HAZARD_MULTIPLIER = 3.0f;
    static constexpr float INTENSITY_HAZARD_MULTIPLIER = 1.5f;
    static constexpr float CONTACT_INJURY_CHANCE = 0.006f;
    static constexpr float RECKLESS_CONTACT_INJURY_CHANCE = 0.03f;
    static constexpr float INJURED_SPEED_SCALE = 0.55f;
    static constexpr float CHECK_INTERVAL_MINUTES = 1.0f;
  };

  struct Stoppage final
  {
    static constexpr float FIRST_HALF_BASE_MINUTES = 1.5f;
    static constexpr float SECOND_HALF_BASE_MINUTES = 2.5f;
    static constexpr float EXTRA_TIME_BASE_MINUTES = 0.5f;
    static constexpr float MINUTES_PER_GOAL = 0.7f;
    static constexpr float MINUTES_PER_SUBSTITUTION = 0.3f;
    static constexpr float MINUTES_PER_CARD = 0.3f;
    static constexpr float MINUTES_PER_INJURY = 1.2f;
    static constexpr float MINUTES_PER_PENALTY = 0.5f;
    static constexpr int MIN_ADDED_MINUTES = 1;
    static constexpr int MAX_ADDED_MINUTES = 12;
    // A dangerous attack (corner, penalty, shot in flight) is allowed to
    // finish; this bounds how long the referee waits past the added time.
    static constexpr float MAX_OVERRUN_MINUTES = 1.5f;
  };

  struct Substitution final
  {
    static constexpr int MAX_WINDOWS = 3;
    static constexpr float EARLIEST_TACTICAL_MINUTE = 68.0f;
    static constexpr float FATIGUE_THRESHOLD = 0.60f;
    static constexpr float FATIGUE_THRESHOLD_LATE_GAIN = 0.12f;
    static constexpr float LATE_GAME_MINUTE = 80.0f;
    static constexpr float CARD_RISK_MINUTE = 64.0f;
    static constexpr float CARD_RISK_NEED = 0.22f;
    static constexpr float TRAILING_CHASE_MINUTE = 66.0f;
    static constexpr float LEADING_PROTECT_MINUTE = 75.0f;
    static constexpr float TACTICAL_NEED = 0.12f;
    static constexpr float MINIMUM_NEED = 0.22f;
    /** Share of the minimum need that suffices in the last minutes. */
    static constexpr float LATE_NEED_SHARE = 0.4f;
    /** Share of the minimum need that joins a change already being made. */
    static constexpr float WINDOW_NEED_SHARE = 0.95f;
    static constexpr float MINUTE_NEED_GAIN = 0.012f;
    static constexpr float FATIGUE_NEED_SCALE = 2.2f;
    /** Forwards lose their sharpness first and are replaced most often. */
    static constexpr float ATTACKER_FATIGUE_NEED_FACTOR = 1.6f;
    /** From this minute a tiring forward is also freshened up tactically
     * (need per minute, scaled by the condition he has lost). */
    static constexpr float ATTACKER_ROTATION_MINUTE = 64.0f;
    static constexpr float ATTACKER_ROTATION_NEED_PER_MINUTE = 0.045f;
    static constexpr float ROLE_FIT_BONUS = 0.5f;
  };

  /**
   * Two-pool energy model per real second. The slow pool (`stamina`, the
   * match condition) drains with running intensity and caps top speed late
   * in the game; the fast pool (repeat-sprint reserve) empties during
   * high-intensity bursts and refills within a couple of minutes.
   */
  struct Fatigue final
  {
    static constexpr float IDLE_DRAIN = 0.000030f;
    static constexpr float RUNNING_DRAIN = 0.00080f;
    static constexpr float SPRINT_THRESHOLD = 0.78f;
    static constexpr float SPRINT_DRAIN = 0.00030f;
    static constexpr float PRESSING_DRAIN = 0.00015f;
    /** Extra drain per second for the whole side out of possession, per unit
     * of pressing above the neutral setting. */
    static constexpr float TEAM_PRESSING_NEUTRAL = 0.5f;
    static constexpr float TEAM_PRESSING_DRAIN = 0.00040f;
    static constexpr float ENDURANCE_BASE = 1.40f;
    static constexpr float ENDURANCE_RELIEF = 0.80f;
    static constexpr float HALF_TIME_RECOVERY = 0.06f;
    static constexpr float EXTRA_TIME_BREAK_RECOVERY = 0.03f;
    static constexpr float EXTRA_TIME_HALF_TIME_RECOVERY = 0.01f;
    static constexpr float TECHNIQUE_ERROR_GAIN = 0.35f;
    /** Binturong body: technique error added at an empty tank (the fatigue
     * tiers' shape scales up to it; fatigue plan 4.5, a stand-in). */
    static constexpr float SPENT_TECHNIQUE_ERROR = 0.60f;
    /** Reserve drained per second at full sprint (empties in ~25 s). */
    static constexpr float RESERVE_DRAIN_PER_SECOND = 0.04f;
    static constexpr float RESERVE_RECOVERY_SECONDS = 90.0f;
  };

  struct Aerial final
  {
    static constexpr float CONTROL_CEILING_METRES = 1.55f;
    static constexpr float BASE_JUMP_METRES = 0.32f;
    static constexpr float PHYSICALITY_JUMP_METRES = 0.38f;
    static constexpr float HEAD_OFFSET_METRES = 0.08f;
    /** Jumping reach's jump (heading technique plan step 1): how far the
     * top of his head rises, at rating 1 and 99 of its blend. */
    static constexpr float JUMP_LOW_METRES = 0.25f;
    static constexpr float JUMP_HIGH_METRES = 0.70f;
    static constexpr float GOALKEEPER_ARM_REACH_RATIO = 1.26f;
    static constexpr float GOALKEEPER_BASE_JUMP_METRES = 0.30f;
    static constexpr float GOALKEEPER_SKILL_JUMP_METRES = 0.30f;
    static constexpr float DUEL_RADIUS_METRES = 1.1f;
    static constexpr float GOALKEEPER_CLAIM_RADIUS_METRES = 2.5f;
    static constexpr float BASE_CLAIM_CHANCE = 0.50f;
    static constexpr float CROWDING_PENALTY = 0.08f;
    static constexpr float SKILL_CLAIM_BONUS = 0.24f;
    static constexpr float DUEL_FOUL_CHANCE = 0.08f;
    /** Defenders facing a delivery win first contact more often. */
    static constexpr float DEFENDER_DUEL_ADVANTAGE = 1.3f;
    static constexpr float HEADER_SHOT_WITHIN_METRES = 16.8f;
    /** Team-mates' lofted balls are cushioned down further than this from goal (m). */
    static constexpr float HEADER_KNOCK_DOWN_WITHIN_METRES = 29.4f;
    static constexpr float KNOCK_DOWN_MAX_BACKWARD_METRES = 4.0f;
    static constexpr float HEADER_SHOT_WIDTH_METRES = 12.24f;
    static constexpr float HEADER_SHOT_MIN_XG = 0.06f;
    static constexpr float HEADER_CLEARANCE_SPEED = 17.0f;
    /** Weakest defensive header as a share of the clearance speed. */
    static constexpr float MIN_CLEARANCE_SHARE = 0.45f;
    static constexpr float HEADER_PASS_SPEED = 10.0f;
    static constexpr float HEADER_LIFT = 5.0f;
    // Headed out of play (Binturong brain), always as hard as he can: high
    // over a line, or steeply up over the bar (radians above the ground),
    // which it must clear by a margin (untuned).
    static constexpr float HEADER_OUT_LIFT = 8.0f;
    static constexpr float HEADER_OVER_ELEVATION = 0.9f;
    static constexpr float HEADER_OVER_BAR_MARGIN_METRES = 1.0f;
    static constexpr float HEADER_ACCURACY_PENALTY = 0.96f;
    /** Share of physicality (against shooting) in heading precision. */
    static constexpr float HEADING_PHYSICAL_SHARE = 0.3f;
    static constexpr float HEADER_SPEED_SCALE = 0.7f;
    // Header power (Binturong body; heading technique plan step 3). The
    // pace he gives a ball that arrives with almost no pace of its own, at
    // Heading 1 and 99 (placeholders).
    static constexpr float HEADER_POWER_LOW = 8.0f;
    static constexpr float HEADER_POWER_HIGH = 17.0f;
    /** The share of the ball's pace into his head (along the line his head
     * pushes) that springs back off it. */
    static constexpr float HEADER_SPRING = 0.6f;
    /** A headed shot: the share of the fastest he can head it on its line. */
    static constexpr float HEADER_SHOT_SHARE = 0.9f;
    /** A header to a spot (rad up): weighted to land on it at this angle. */
    static constexpr float HEADER_PASS_ELEVATION = 0.35f;
    /** A clearing header (rad up), as hard as he can. */
    static constexpr float HEADER_CLEAR_ELEVATION = 0.5f;
    // The head's reach (heading technique plan step 4; placeholders). He
    // can't stretch for a header as he can with a leg: the ball has to come
    // within a lean of the neck and shoulders of his centre, easily within
    // the comfort gap. Beyond the comfort gap and more than the dead zone's
    // angle off his facing, his forehead can't get to it; only the top of
    // his head, for a ball at or above it. It meets a ball from the chest
    // ceiling (CONTROL_CEILING_METRES) up to his Jumping reach (the top of
    // his head at the top of his jump), the forehead up to HEAD_OFFSET_METRES
    // below that.
    static constexpr float HEAD_REACH_METRES = 0.4f;
    static constexpr float HEAD_COMFORT_GAP_METRES = 0.15f;
    static constexpr float HEAD_DEAD_ZONE_DEGREES = 60.0f;
    /** The head's stretch map: the shares of HEAD_REACH_METRES out, at
     * FirstTouch::STRETCH_ANGLES (rows), rising quickly beyond the comfort
     * gap (0.375 of the reach). Past 60 degrees only the top of the head
     * reaches. */
    static constexpr std::array<float, 6> HEAD_STRETCH_DISTANCES{
        0.000f, 0.375f, 0.500f, 0.700f, 0.850f, 1.000f};
    static constexpr std::array<std::array<float, 6>, 6> HEAD_STRETCH_VALUES{{
        {0.000f, 0.000f, 0.150f, 0.400f, 0.700f, 1.000f},
        {0.000f, 0.000f, 0.200f, 0.500f, 0.800f, 1.000f},
        {0.000f, 0.100f, 0.300f, 0.600f, 0.900f, 1.000f},
        {0.000f, 0.100f, 0.300f, 0.600f, 0.900f, 1.000f},
        {0.000f, 0.100f, 0.300f, 0.600f, 0.900f, 1.000f},
        {0.000f, 0.100f, 0.300f, 0.600f, 0.900f, 1.000f},
    }};
    // What his head can do with it (heading technique plan step 5;
    // placeholders). The top of his head only glances it on: within this
    // turn (rad) of the way it was going, flat or upward, with this share
    // of his header power added.
    static constexpr float TOP_MAX_TURN = 0.45f;
    static constexpr float TOP_POWER_SHARE = 0.3f;
    // The cushion (heading technique plan step 6; placeholders): the head
    // can only give a little. At least this share of the pace into his head
    // (along the line it pushes) comes back off it, at Heading 1 and 99.
    static constexpr float CUSHION_KEEP_LOW = 0.5f;
    static constexpr float CUSHION_KEEP_HIGH = 0.2f;
    // Jumping (aerial challenge plan step 1). His running jump (one-footed,
    // off a run-up) takes the top of his head to his Jumping reach; his
    // standing jump (two-footed, from the spot) rises this share of it
    // (a placeholder). A running jump needs at least this pace (m/s, about
    // a jog; a placeholder).
    static constexpr float STANDING_JUMP_SHARE = 0.85f;
    static constexpr float RUNNING_JUMP_MIN_SPEED = 3.0f;
    /** Landing takes half his time in the air (decision 5); meanwhile he
     * has this share of his acceleration, braking and turning (a
     * placeholder). */
    static constexpr float LANDING_MOVEMENT_SHARE = 0.5f;
    /** Standing, he can duck his head this share of his height below his
     * forehead to meet a ball (about 0.3 m for a 1.80 m man; decision 7). */
    static constexpr float DUCK_HEIGHT_SHARE = 0.167f;
    /** How far ahead (s) he reads the ball's flight to time a jump. */
    static constexpr float JUMP_LOOKAHEAD_SECONDS = 1.5f;
    /** He times his jump for his forehead to be this far above the ball's
     * centre (m), so it meets the forehead. */
    static constexpr float JUMP_FOREHEAD_ABOVE_METRES = 0.03f;
    // Going for a loose high ball (aerial challenge plan step 2;
    // placeholders). He holds the spot only if he gets there this long
    // before the ball; a running jump needs at least this run-up (m) unless
    // he is already moving at a jog. The other side contests if it can get
    // to the meet within this long after the ball. The ball's flight is read
    // this far ahead (s).
    static constexpr float CHALLENGE_SETTLE_SECONDS = 0.3f;
    static constexpr float CHALLENGE_RUN_UP_METRES = 2.0f;
    static constexpr float CHALLENGE_TOLERANCE_SECONDS = 0.2f;
    static constexpr float CHALLENGE_PATH_SECONDS = 4.0f;
    // The duel on jumps (aerial challenge plan step 3; placeholders). The
    // first head the ball reaches wins it; an opponent's head reaching it
    // within this long after makes it a close call, decided by strength
    // (Physicality), how far his head is above the ball (up to this margin,
    // m: up to twice as strong), and holding the spot against attacking it
    // (the holder this much stronger), with some chance.
    static constexpr float DUEL_CLOSE_CALL_SECONDS = 0.05f;
    static constexpr float DUEL_HEIGHT_MARGIN_METRES = 0.3f;
    static constexpr float DUEL_HOLD_ADVANTAGE = 1.2f;
    static constexpr float ARRIVAL_HEIGHT_METRES = 2.1f;
  };

  /**
   * Touchline instructions: a shout nudges the team sliders (and shot
   * appetite) and fades linearly; a team talk scales execution, decisions and
   * work rate by at most a few per cent for one half.
   */
  /** The people round the touchline (touchline plan, stage 2): each side's
   * subs and staff seated in its dugout, and its manager in his technical
   * area. Their bodies collide like players' (Binturong bodies only). */
  struct TouchlinePeople final
  {
    /** Stand-ins for people without a player's stats (agreed): an average
     * fit adult, every stat 50, 75 kg. */
    static constexpr float STAND_IN_ATTRIBUTE = 0.5f;
    static constexpr float STAND_IN_MASS_KILOGRAMS = 75.0f;
    /** They walk (staff walk only; agreed), at the restart walk's pace. */
    static constexpr float WALK_SPEED = 1.5f;
    /** Seats along the dugout's bench, staff nearest the halfway line: the
     * first this far in from the dugout's halfway end, then this apart. */
    static constexpr float FIRST_SEAT_METRES = 0.5f;
    static constexpr float SEAT_SPACING_METRES = 0.6f;
    /** Close enough to his seat or spot to stop there. */
    static constexpr float ARRIVED_METRES = 0.15f;
    /** The manager: his spot this far in front of his dugout, level with
     * the play along his technical area (it moves on once the play has
     * moved this far); up at the front edge while the ball is this near
     * his area, or for this long after his side's shout. */
    static constexpr float MANAGER_SPOT_OUT_METRES = 1.5f;
    static constexpr float MANAGER_FOLLOW_METRES = 1.0f;
    static constexpr float MANAGER_BALL_NEAR_METRES = 25.0f;
    static constexpr float MANAGER_SHOUT_SECONDS = 20.0f;
    /** A touchline body takes part in a step's run-ins only with a player
     * within this of it (nobody else can reach it in a step). */
    static constexpr float COLLISION_RANGE_METRES = 5.0f;
  };

  /** The match officials (touchline plan stage 3): bodies only; how the
   * game is refereed is unchanged. Stand-ins as the touchline's (every stat
   * 50, 75 kg), the referee and his assistants reaching 7 m/s (agreed). */
  struct Officials final
  {
    static constexpr float TOP_SPEED = 7.0f;
    /** Easing in to their spot: the speed they run at this far from it is
     * what stops them on it braking at this (m/sÂ²); within the dead band
     * they stay put (the spot moves with play all the time). */
    static constexpr float ARRIVE_BRAKING = 2.5f;
    static constexpr float DEAD_BAND_METRES = 0.5f;
    /** The referee in play: on his diagonal (from beside the left penalty
     * area near the bottom touchline to beside the right one near the top,
     * so play is between him and the assistant on that half), this far
     * from the ball. */
    static constexpr float DIAGONAL_END_X_METRES = 16.5f;
    static constexpr float DIAGONAL_END_Y_METRES = 12.0f;
    static constexpr float NEAREST_TO_BALL_METRES = 15.0f;
    static constexpr float FURTHEST_FROM_BALL_METRES = 25.0f;
    /** He stays this far inside the lines. */
    static constexpr float INSIDE_LINES_METRES = 1.0f;
    /** The referee out of the players' way (user): if anyone (going on as
     * he is) would come within this in the look-ahead, he takes, of
     * standing still and these speeds in 16 directions, the nearest to
     * what he wants, each second sooner that someone would come within it
     * costing as much as this much more speed off what he wants (m/s). */
    static constexpr float CLEARANCE_METRES = 2.0f;
    static constexpr float LOOK_AHEAD_SECONDS = 2.0f;
    static constexpr std::array<float, 3> DODGE_SPEEDS{2.0f, 4.0f, 6.5f};
    static constexpr float THREAT_COST = 20.0f;
    /** Out of the way (user): his spot is moved, up to this far, to where
     * the nearest player is furthest off (counting up to this far); each
     * metre moved costs this much of that, and each metre from where he
     * already is half as much (so he does not dither). */
    static constexpr float CLEAR_SPOT_SEARCH_METRES = 6.0f;
    static constexpr float CLEAR_SPOT_METRES = 6.0f;
    static constexpr float CLEAR_SPOT_MOVE_COST = 0.35f;
    /** At restarts: kick-off, this far into the half of the side not
     * kicking off and this far below the centre; goal kick, this far from
     * halfway into the kicking side's half; corner, this far out from the
     * goal line (by the edge of the box) and this far past the far post;
     * free kick, this far from the ball at this angle off the line to the
     * goal it is taken toward; penalty (user), outside the area: this far
     * beyond its edge and this far beyond the end of the arc. */
    static constexpr float KICK_OFF_INTO_HALF_METRES = 8.0f;
    static constexpr float KICK_OFF_BELOW_CENTRE_METRES = 12.0f;
    static constexpr float GOAL_KICK_FROM_HALFWAY_METRES = 5.0f;
    static constexpr float CORNER_OUT_METRES = 15.0f;
    static constexpr float CORNER_PAST_FAR_POST_METRES = 2.0f;
    static constexpr float FREE_KICK_METRES = 12.0f;
    static constexpr float FREE_KICK_ANGLE_RADIANS = 1.05f;
    static constexpr float PENALTY_BEYOND_AREA_METRES = 1.5f;
    static constexpr float PENALTY_BEYOND_ARC_METRES = 1.0f;
    /** After a goal: back toward the centre, this far below it. */
    static constexpr float GOAL_BELOW_CENTRE_METRES = 8.0f;
    /** The assistants run this far outside the touchline (clear of the
     * technical areas, 1 m out), each along half of it: the top one the
     * left half, the bottom one (the dugouts' side) the right. */
    static constexpr float ASSISTANT_OUT_METRES = 0.5f;
    /** A throw-in on his touchline: he stands this far further out than
     * the thrower (DeadBall::THROW_IN_STAND_BEHIND_METRES), behind him
     * (user; the distance is a design choice). An assistant never steps
     * onto the pitch to keep clear of a player. */
    static constexpr float ASSISTANT_BEHIND_THROWER_METRES = 1.0f;
  };

  /** Walk-outs and walk-offs (touchline plan stage 4, Binturong out of play
   * with both sides on the Binturong body): everyone comes out of the
   * tunnel at the start of each half and goes back in at half-time. */
  struct WalkOut final
  {
    /** Deeper than this into the tunnel (past its mouth) he is in: out of
     * sight and out of play. They wait this deep, in two files this far
     * either side of the tunnel's middle (home on the left). */
    static constexpr float IN_METRES = 2.0f;
    static constexpr float WAIT_METRES = 3.0f;
    static constexpr float FILE_METRES = 1.0f;
    /** Out of the tunnel one after another, this far apart: the officials,
     * then the teams side by side (captains first, then teamsheet order),
     * then the staff (user). */
    static constexpr float RELEASE_SECONDS = 1.0f;
    /** The line-up before the first half (agreed): on the halfway line,
     * this far in from the bottom touchline, facing that stand; the
     * officials in the middle this far apart, each team's line this far
     * from them and its players this far apart. */
    static constexpr float LINE_IN_METRES = 3.0f;
    static constexpr float OFFICIAL_SPACING_METRES = 0.8f;
    static constexpr float TEAM_FROM_OFFICIALS_METRES = 1.0f;
    static constexpr float PLAYER_SPACING_METRES = 1.0f;
    /** Lined up, a moment before the handshake. */
    static constexpr float LINED_UP_SECONDS = 3.0f;
    /** The handshake (agreed): the home team stands; the away team files
     * along in front of the officials and the home line, this far in front
     * of them, setting off this far apart and pausing this long at each
     * hand. */
    static constexpr float HANDSHAKE_LANE_METRES = 0.9f;
    static constexpr float HANDSHAKE_START_SECONDS = 2.0f;
    static constexpr float HANDSHAKE_PAUSE_SECONDS = 0.4f;
    /** On his line-up spot, or at a hand to shake. */
    static constexpr float ARRIVED_METRES = 0.3f;
    /** Safety net: a walking phase moves on after this long anyway. */
    static constexpr float PHASE_LIMIT_SECONDS = 120.0f;
    /** A substitution still holding up the restart after this long (user):
     * the player going off and the sub are put where they were going (the
     * one agreed exception to no teleporting). */
    static constexpr float SUBSTITUTION_LIMIT_SECONDS = 60.0f;
    /** Where a sub waits (agreed): this far beyond the touchline, this far
     * from the edge of his technical area toward halfway; more than one,
     * a queue along the touchline away from halfway, this far apart
     * (user). Each comes on when the player he replaces is off. Back from
     * the line clear of the assistant referee's run (Officials::
     * ASSISTANT_OUT_METRES; user). */
    static constexpr float SUB_OUT_METRES = 1.2f;
    static constexpr float SUB_FROM_AREA_METRES = 1.0f;
    static constexpr float SUB_QUEUE_SPACING_METRES = 0.8f;
  };

  struct Touchline final
  {
    static constexpr float SHOUT_DURATION_SECONDS = 600.0f;
    static constexpr float SHOUT_SLIDER_STEP = 0.2f;
    static constexpr float SHOUT_SHOT_BIAS = 0.6f;
    /** Each recent shout divides the next one's effect by (1 + recent
     * shouts); one recent shout is forgotten per this many seconds (less
     * than a shout lasts, so a shout made once the last has faded is at
     * full strength). */
    static constexpr float SHOUT_REPEAT_FADE_SECONDS = 480.0f;
    /** A new shape costs tactical familiarity per metre the slots move on
     * average (at most MAX), recovered linearly over RECOVERY seconds. */
    static constexpr float RESHAPE_FAMILIARITY_PER_METRE = 0.02f;
    static constexpr float MAX_RESHAPE_FAMILIARITY_COST = 0.2f;
    static constexpr float RESHAPE_RECOVERY_SECONDS = 300.0f;
    static constexpr float ENCOURAGE_WORK_RATE = 0.03f;
    static constexpr float MAX_TEAM_TALK_MODIFIER = 0.05f;
    // Score effects on the sliders per goal of lead (at most two), from
    // SCORE_EFFECT_BASE of full strength at kick-off to full at 90 minutes.
    static constexpr int SCORE_EFFECT_MAX_GOALS = 3;
    static constexpr float SCORE_EFFECT_BASE = 0.4f;
    static constexpr float SCORE_EFFECT_OFFENSIVE = 0.10f;
    static constexpr float SCORE_EFFECT_RISK = 0.10f;
    static constexpr float SCORE_EFFECT_PRESSING = 0.08f;
    static constexpr float SCORE_EFFECT_COMPACTNESS = 0.08f;
    /** A side two or more goals up also stops committing men forward (per
     * goal of lead from Decision::COMFORTABLE_LEAD, at full urgency). */
    static constexpr float GAME_MANAGEMENT_OFFENSIVE = 0.12f;
    // A clearly weaker side sits deeper and more compact and commits a few
    // fewer men forward, from UNDERDOG_GAP_START of mean (stretched) outfield
    // quality below the opponent to full effect UNDERDOG_GAP_RANGE later. It
    // still attacks with its forwards: taking them out of the game made big
    // quality gaps far more decisive than real results (NFR-019).
    static constexpr float UNDERDOG_GAP_START = 0.08f;
    static constexpr float UNDERDOG_GAP_RANGE = 0.30f;
    static constexpr float UNDERDOG_PRESSING = 0.35f;
    static constexpr float UNDERDOG_COMPACTNESS = 0.40f;
    static constexpr float UNDERDOG_OFFENSIVE = 0.12f;
    // AI managers react to the score late in the game.
    static constexpr float AI_CHASE_MINUTE = 65.0f;
    static constexpr float AI_SHOOT_ON_SIGHT_MINUTE = 82.0f;
    static constexpr float AI_PROTECT_MINUTE = 75.0f;
  };

  /**
   * External control of one player (play mode). The controlled player's body
   * is integrated at a finer fixed rate than the AI step so a stick feels
   * responsive; everything else keeps the common step.
   */
  struct Control final
  {
    /** Kinematic sub-steps per fixed step for the controlled player (50 Hz). */
    static constexpr int PHYSICS_SUBSTEPS = 5;
    /** Run speed without the sprint button, as a share of top speed. */
    static constexpr float JOG_SPEED_SHARE = 0.62f;
    /** How long a pressed action waits for a chance to be performed. */
    static constexpr float ACTION_BUFFER_SECONDS = 1.0f;
    /** A pass goes to the team-mate best aligned with the aim within this
     * cosine (60 degrees) ... */
    static constexpr float PASS_MIN_ALIGNMENT = 0.5f;
    /** ... preferring nearer team-mates by this much per metre. */
    static constexpr float PASS_DISTANCE_WEIGHT = 0.006f;

    // --- Play mode: pass assistance (index = MatchPlayerInput::passAssist) --
    /** Aim cone (cosine) of a pass without, with normal and with strong
     * assistance; index 1 is PASS_MIN_ALIGNMENT. */
    static constexpr std::array<float, 3> ASSIST_MIN_ALIGNMENT{0.82f, 0.5f,
                                                               0.25f};
    /** Weight of the pass's chance of arriving in picking the receiver. */
    static constexpr std::array<float, 3> ASSIST_COMPLETION_WEIGHT{0.0f, 0.25f,
                                                                   0.6f};
    /** A through ball prefers team-mates going forward (per metre gained)
     * and those already running in behind. */
    static constexpr float THROUGH_PROGRESS_WEIGHT = 0.012f;
    static constexpr float THROUGH_RUNNER_BONUS = 0.25f;
    /** Space ahead of the receiver a through ball is played into, from a
     * tap to a full bar (metres). */
    static constexpr float THROUGH_LEAD_MIN_METRES = 5.0f;
    static constexpr float THROUGH_LEAD_MAX_METRES = 16.0f;
    /** Length of a pass into space along the stick (ground and lofted). */
    static constexpr float SPACE_PASS_MIN_METRES = 10.0f;
    static constexpr float SPACE_PASS_MAX_METRES = 30.0f;
    static constexpr float SPACE_LOFT_MIN_METRES = 18.0f;
    static constexpr float SPACE_LOFT_MAX_METRES = 45.0f;
    /** Power of a tap (no hold), as a share of the bar. */
    static constexpr float DEFAULT_POWER = 0.55f;

    // --- Play mode: shots ----------------------------------------------------
    /** Shot pace scale: SHOT_SPEED_MIN_SCALE + SHOT_SPEED_POWER_GAIN * power.
     */
    static constexpr float SHOT_SPEED_MIN_SCALE = 0.72f;
    static constexpr float SHOT_SPEED_POWER_GAIN = 0.42f;
    /** Above this share of the bar the shot gets harder to keep down ... */
    static constexpr float SHOT_OVERPOWER_FROM = 0.75f;
    /** ... by up to this much extra spread and lift at a full bar. */
    static constexpr float SHOT_OVERPOWER_SPREAD = 0.55f;
    static constexpr float SHOT_OVERPOWER_LIFT_METRES = 0.9f;

    // --- Play mode: defending and receiving ---------------------------------
    /** Jockeying pace as a share of the fresh top speed. */
    static constexpr float JOCKEY_SPEED_SHARE = 0.42f;
    /** Goal-side gap kept to the carrier while jockeying (metres). */
    static constexpr float JOCKEY_CONTAIN_METRES = 2.5f;
    /** A patient, jockeying defender is harder to beat one against one. */
    static constexpr float JOCKEY_TAKE_ON_PENALTY = 0.08f;
    /** With the stick idle the receiver of a pass moves to meet it, aiming
     * this many seconds of ball travel ahead. */
    static constexpr float MEET_PASS_LOOKAHEAD_SECONDS = 0.5f;
    /** On hand-back the AI picks up the run where the momentum carries him
     * (seconds of travel). */
    static constexpr float RELEASE_MOMENTUM_SECONDS = 0.6f;

    // --- Play mode: who becomes the active footballer ------------------------
    /** Ball path looked at, and its sampling (seconds). */
    static constexpr float SWITCH_HORIZON_SECONDS = 2.5f;
    static constexpr float SWITCH_SAMPLE_SECONDS = 0.1f;
    /** Rough deceleration of a ball on the ground for the path (m/s^2). */
    static constexpr float SWITCH_BALL_DECELERATION = 2.0f;
    /** Reaction before a team-mate sets off (seconds). */
    static constexpr float SWITCH_REACTION_SECONDS = 0.25f;
    /** Head start of the current active footballer (hysteresis) and of a
     * defender already goal-side of the ball. */
    static constexpr float SWITCH_CURRENT_PREFERENCE_SECONDS = 0.35f;
    static constexpr float SWITCH_GOAL_SIDE_BONUS_SECONDS = 0.2f;
  };

  /** Accepted ranges of MatchContext (league character). */
  struct Context final
  {
    static constexpr float MIN_GOAL_RATE_SCALE = 0.5f;
    static constexpr float MAX_GOAL_RATE_SCALE = 1.5f;
    /** Shot precision grows as goalRateScale^FINISHING_EXPONENT. */
    static constexpr float FINISHING_EXPONENT = 1.7f;
    static constexpr float MIN_REFEREE_STRICTNESS = 0.5f;
    static constexpr float MAX_REFEREE_STRICTNESS = 2.0f;
    static constexpr float MAX_REFEREE_SD = 0.5f;
    static constexpr float MAX_HOME_ADVANTAGE_SCALE = 2.0f;
  };

  struct Statistics final
  {
    static constexpr float EVEN_POSSESSION_PERCENT = 50.0f;
    static constexpr float PERCENT_SCALE = 100.0f;
  };
};

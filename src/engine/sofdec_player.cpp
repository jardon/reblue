/**
 * @file    engine/sofdec_player.cpp
 * @copyright Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *            All rights reserved.
 * @license   BSD 3-Clause License
 */
#include "engine/sofdec_player.h"

#include <atomic>
#include <chrono>
#include <cstddef>

#include <rex/ppc.h>
#include <rex/types.h>

#include "core/memory_helpers.h"
#include "engine/frame_clock.h"
#include "engine/game.h"

namespace bd::engine {

namespace {

struct SofdecPlayer_t {
  /* 0x00 */ u8 _pad00[0x8C];
  /* 0x8C */ be_i32 status;
  /* 0x90 */ u8 paused;
};
static_assert(offsetof(SofdecPlayer_t, status) == 0x8C);
static_assert(offsetof(SofdecPlayer_t, paused) == 0x90);

struct PlayTask_t {
  /* 0x00 */ u8 _pad00[0x68];
  /* 0x68 */ be_u32 player;
};
static_assert(offsetof(PlayTask_t, player) == 0x68);

constexpr i32 kNoStatus = -1;

constexpr i32 kStatusPreparing = 1;
constexpr i32 kStatusAdvancing = 2;

constexpr i64 kHoldNs = 250'000'000;
std::atomic<i64> g_untilNs{0};
std::atomic<u32> g_player{0};

u32 PlayTaskPlayer(u32 address) {
  const auto *task = bd::mem::at<PlayTask_t>(address);
  return task ? static_cast<u32>(task->player) : 0;
}

i64 NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void OnPresent(u32 address) {
  const SofdecPlayer player(address);
  if (!player)
    return;
  const i32 status = player.Status();
  const bool playing =
      (status == kStatusPreparing || status == kStatusAdvancing) &&
      !player.Paused();
  g_player.store(address, std::memory_order_relaxed);
  g_untilNs.store(playing ? NowNs() + kHoldNs : 0, std::memory_order_relaxed);
}

} // namespace

bool SofdecPlayer::Playing() {
  return NowNs() < g_untilNs.load(std::memory_order_relaxed);
}

i32 SofdecPlayer::Status() const {
  const auto *self = Self<SofdecPlayer_t>();
  return self ? static_cast<i32>(self->status) : kNoStatus;
}

bool SofdecPlayer::Paused() const {
  const auto *self = Self<SofdecPlayer_t>();
  return self && self->paused != 0;
}

engine::SofdecPlayer Game::SofdecPlayer() const {
  return engine::SofdecPlayer(
      engine::SofdecPlayer::Playing() ? g_player.load(std::memory_order_relaxed)
                                      : 0);
}

} // namespace bd::engine

void bdMoviePlaybackHook(PPCRegister &r31) { bd::engine::OnPresent(r31.u32); }

bool bdMovieTaskDrawTickGateHook(PPCRegister &r31, PPCRegister &r11) {
  if (bd::engine::TickDue())
    return false;
  const u32 player = bd::engine::PlayTaskPlayer(r31.u32);
  if (!player)
    return false;
  r11.u32 = player;
  return true;
}

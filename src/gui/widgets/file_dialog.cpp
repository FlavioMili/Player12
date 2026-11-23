// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/widgets/file_dialog.h"

#include <array>
#include <mutex>

#include "database/world/world_io.h"

struct FileDialog::Slot
{
  std::mutex mutex;
  bool busy = false;
  std::optional<std::filesystem::path> result;
};

namespace
{
constexpr std::array<SDL_DialogFileFilter, 1> FILTERS = {{
    {"World database (*.fmdb)", "fmdb"},
}};

/** Owns one reference to the slot for the duration of the picker. */
void SDLCALL onPicked(void* userdata, const char* const* files, int filter)
{
  (void)filter;
  std::unique_ptr<std::shared_ptr<FileDialog::Slot>> owner(
      static_cast<std::shared_ptr<FileDialog::Slot>*>(userdata));
  FileDialog::Slot& slot = **owner;
  const std::scoped_lock lock(slot.mutex);
  slot.busy = false;
  if (files != nullptr && files[0] != nullptr)
  {
    slot.result = World::pathFromUtf8(files[0]);
  }
}
}  // namespace

FileDialog::FileDialog() : slot(std::make_shared<Slot>()) {}

FileDialog::~FileDialog() = default;

void FileDialog::open(SDL_Window* window)
{
  {
    const std::scoped_lock lock(slot->mutex);
    if (slot->busy)
    {
      return;
    }
    slot->busy = true;
    slot->result.reset();
  }
  SDL_ShowOpenFileDialog(&onPicked, new std::shared_ptr<Slot>(slot), window,
                         FILTERS.data(), static_cast<int>(FILTERS.size()),
                         nullptr, false);
}

void FileDialog::save(SDL_Window* window, const std::string& suggested_name)
{
  {
    const std::scoped_lock lock(slot->mutex);
    if (slot->busy)
    {
      return;
    }
    slot->busy = true;
    slot->result.reset();
  }
  SDL_ShowSaveFileDialog(
      &onPicked, new std::shared_ptr<Slot>(slot), window, FILTERS.data(),
      static_cast<int>(FILTERS.size()),
      suggested_name.empty() ? nullptr : suggested_name.c_str());
}

bool FileDialog::busy() const
{
  const std::scoped_lock lock(slot->mutex);
  return slot->busy;
}

std::optional<std::filesystem::path> FileDialog::take()
{
  const std::scoped_lock lock(slot->mutex);
  std::optional<std::filesystem::path> result = std::move(slot->result);
  slot->result.reset();
  if (result && result->extension().empty())
  {
    *result += std::string(World::FILE_EXTENSION);
  }
  return result;
}

std::optional<std::filesystem::path> FileDialog::dropped(const SDL_Event& event)
{
  if (event.type != SDL_EVENT_DROP_FILE || event.drop.data == nullptr)
  {
    return std::nullopt;
  }
  return World::pathFromUtf8(event.drop.data);
}

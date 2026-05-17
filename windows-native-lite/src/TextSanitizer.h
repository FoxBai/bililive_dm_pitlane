#pragma once

#include "AppSettings.h"
#include "ChatMessage.h"

#include <optional>
#include <string>

namespace pitlane::lite {

std::optional<ChatMessage> normalize_message(const ChatMessage& message, const AppSettings& settings);
std::wstring clean_text(std::wstring value, int max_length, int max_repeat_characters);

}  // 命名空间 pitlane::lite

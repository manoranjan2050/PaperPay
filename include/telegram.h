// ============================================================================
//  telegram.h — Telegram bot: payment notifications + auto-confirm
// ============================================================================
#pragma once
#include <Arduino.h>

void tgInit();                       // start the background polling task
void tgNotify(const String& text);   // queue an outgoing message (non-blocking)

// ============================================================================
//  display.h — e-paper rendering (runs on the main loop, not async tasks)
// ============================================================================
#pragma once
#include <Arduino.h>

void displayInit();

// Boot / status splash while connecting.
void displayBoot(const String& line1, const String& line2);

// Idle screen: shop name + how to use + IP/host.
void displayQueueIdle(const String& shopName, const String& host,
                      const String& ip);

// Payment screen: big amount + "Scan to Pay" + the UPI QR.
void displayQueuePayment(const String& shopName, double amount,
                         const String& upiUrl, const String& note);

// "Payment received" confirmation tick.
void displayQueuePaid(double amount);

// Call every loop(); renders whatever was queued (SPI must run on loop task).
void displayService();

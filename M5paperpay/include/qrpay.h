// ============================================================================
//  qrpay.h — build a UPI deep-link and a QR code from it
// ============================================================================
#pragma once
#include <Arduino.h>
#include "qrcode.h"   // ricmoo/QRCode

// Build the UPI "pay" URL:  upi://pay?pa=..&pn=..&am=..&cu=INR&tn=..
// amount is in rupees (2 decimals). note/txn ref are optional.
String upiBuildUrl(const String& vpa, const String& payee,
                   double amount, const String& note);

// Generate a QR for `data`. Allocates the module buffer on the heap and
// returns it through *outBuf — the CALLER MUST free(*outBuf) when done.
// Returns 0 on success, negative on failure (qr.size holds the module count).
int qrBuild(const String& data, QRCode* qr, uint8_t** outBuf);

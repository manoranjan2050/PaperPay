// ============================================================================
//  qrpay.cpp — UPI URL + QR generation
// ============================================================================
#include "qrpay.h"

// Percent-encode the bits of UPI fields that may contain spaces/&/etc.
static String urlEncode(const String& s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 2);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else if (c == ' ') {
      out += "%20";
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

String upiBuildUrl(const String& vpa, const String& payee,
                   double amount, const String& note) {
  char amt[16];
  dtostrf(amount, 0, 2, amt); // always 2 decimals, e.g. 149.50

  String url = "upi://pay?pa=" + urlEncode(vpa);
  url += "&pn=" + urlEncode(payee.length() ? payee : "Shop");
  url += "&am=" + String(amt);
  url += "&cu=INR";
  if (note.length()) url += "&tn=" + urlEncode(note);
  return url;
}

// Smallest QR version (byte mode, ECC_LOW) whose capacity holds `len` chars.
static uint8_t versionForLength(size_t len) {
  // capacities for byte mode @ ECC_LOW
  static const uint16_t cap[] = {
    0, 17, 32, 53, 78, 106, 134, 154, 192, 230, 271,
    321, 367, 425, 458, 520, 586, 644, 718, 792, 858
  };
  for (uint8_t v = 3; v <= 20; v++) {
    if (len <= cap[v]) return v;
  }
  return 20;
}

int qrBuild(const String& data, QRCode* qr, uint8_t** outBuf) {
  uint8_t version = versionForLength(data.length());
  uint8_t* buf = (uint8_t*) malloc(qrcode_getBufferSize(version));
  if (!buf) { *outBuf = nullptr; return -1; }

  qrcode_initText(qr, buf, version, ECC_LOW, data.c_str());
  *outBuf = buf;
  return 0;
}

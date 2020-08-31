#pragma once

#include <cstdlib>
#include <cstdint>
void mkrndstr_ipa(int length, uint8_t* randomString);

void mkrndstr_ipa(int length, uint8_t* randomString) {
  static uint8_t charset[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

  if (length) {
    if (randomString) {
      int l = static_cast<int>(sizeof(charset) - 1);
      for (int n = 0; n < length; n++) {
        int key = rand() % l;
        randomString[n] = charset[key];
      }

      randomString[length] = '\0';
    }
  }
}

/**The MIT License (MIT)

  Copyright (c) 2015 by Daniel Eichhorn

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  See more at https://blog.squix.org


*/

//#define min(a,b)     (((a) < (b)) ? (a) : (b))

#pragma once

// Call up the SPIFFS FLASH filing system this is part of the ESP Core
#define FS_NO_GLOBALS
#include <FS.h>

#include "AdsbExchangeClient.h"
#include "GeoMap.h"

#include <JPEGDecoder.h>          // https://github.com/Bodmer/JPEGDecoder
#include <TFT_eSPI.h>             // https://github.com/Bodmer/TFT_eSPI

enum TextAlignment {
  LEFT, CENTER, RIGHT
};

class PlaneSpotter {
  public:
    PlaneSpotter(TFT_eSPI* tft, GeoMap* geoMap);
    void drawPlane(Aircraft aircraft, bool isSpecial);
    void drawInfoBox(Aircraft closestAircraft);
    void drawAircraftHistory(Aircraft aircraft, AircraftHistory history);

  private:
    TFT_eSPI* tft_;
    GeoMap* geoMap_;
    // Shape of the plane
    // The points are defined as degree on a circle, the first array are the degrees,
    // the second the radius of the circle
    int planeDeg_[5] = {0, 130, 180, 230, 0};
    int planeRadius_[5] = {10, 10, 3, 10, 10};
    //const uint16_t heightPalette_[40] = {0x081F, 0x00FF, 0x027F, 0x029F, 0x037F, 0x041F, 0x047F, 0x0E5F, 0x0F5F, 0x07DF, 0x0FFC, 0x0FF8, 0x07D2, 0x07CD, 0x0FCA, 0x07C7, 0x07C3, 0x1FA2, 0x27C1, 0x57A1,
    //                      0x87A1, 0x9FA2, 0xBFA2, 0xEF82, 0xF763, 0xF6A3, 0xF5C3, 0xF4E3, 0xF3E3, 0xF324, 0xF243, 0xF123, 0xF803, 0xF807, 0xF80D, 0xF810, 0xF815, 0xF81A, 0xF81E, 0xF81F};
    const uint16_t heightPalette_[10] = {0x30FE, 0x00BF, 0x041F, 0x063F, 0x07EC, 0xD7C1, 0xF401, 0xF084, 0xF0B4, 0xF0DD};
    int planeDots_ = 5;
    TextAlignment alignment_ = LEFT;
    uint16_t textColor_;
    uint16_t backgroundColor_;

};


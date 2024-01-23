
#ifndef DIYBMS_TFT_H_
#define DIYBMS_TFT_H_

#include <M5GFX.h>

extern M5GFX display;

class TFT
{
public:
  static void TFTDrawWifiDetails();
  static void PrepareTFT_Error();
  static void PrepareTFT_Voltage();
  static void DrawTFT_Voltage();
  static void init();
private:
  static int last_soc_width;
  static int16_t fontHeight_2;
  static int16_t fontHeight_4;
};

#endif

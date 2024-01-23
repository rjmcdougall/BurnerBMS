//####################################################################################################
// Draw a JPEG on the TFT pulled from SD Card
//####################################################################################################

#include "tft_jpeg.h"
#include "tft.h"

#define USE_ESP_IDF_LOG 1
static constexpr const char *  TAG = "tft_jpeg";

void drawArrayJpeg(const uint8_t array[], uint32_t  array_size, int xpos, int ypos) {

  // Use one of the following methods to initialise the decoder:
  bool decoded = JpegDec.decodeArray(array, array_size);

  if (decoded) {
    // print information about the image to the serial port
    jpegInfo();
    // render the image onto the screen at given coordinates
    jpegRender(xpos, ypos);
  }
  else {
    ESP_LOGE(TAG, "Jpeg file format not supported!");
  }
}

//####################################################################################################
// Draw a JPEG on the TFT pulled from SD Card
//####################################################################################################
// xpos, ypos is top left corner of plotted image
void drawSdJpeg(const char *filename, int xpos, int ypos) {

  // Open the named file (the Jpeg decoder library will close it)
  File jpegFile = SD.open( filename, FILE_READ);  // or, file handle reference for SD library
 
  if ( !jpegFile ) {
    ESP_LOGE(TAG, "ERROR: File %s not found!", filename);
    return;
  }

  ESP_LOGI(TAG, "===========================");
  ESP_LOGI(TAG, "Drawing file: %s", filename);
  ESP_LOGI(TAG, "===========================");

  // Use one of the following methods to initialise the decoder:
  bool decoded = JpegDec.decodeSdFile(jpegFile);  // Pass the SD file handle to the decoder,
  //bool decoded = JpegDec.decodeSdFile(filename);  // or pass the filename (String or character array)

  if (decoded) {
    // print information about the image to the serial port
    jpegInfo();
    // render the image onto the screen at given coordinates
    jpegRender(xpos, ypos);
  }
  else {
    ESP_LOGE(TAG, "Jpeg file format not supported!");
  }
}

#ifdef LFS
// xpos, ypos is top left corner of plotted image
void drawLfsJpeg(const char *filename, int xpos, int ypos) {

  // Open the named file (the Jpeg decoder library will close it)
  //File jpegFile = SD.open( filename, FILE_READ);  // or, file handle reference for SD library
 
  if (!LITTLEFS.exists(filename))
 {
    ESP_LOGE(TAG, "ERROR: File %s not found!", filename);
    return;
  }

  ESP_LOGI(TAG, "===========================");
  ESP_LOGI(TAG, "Drawing file: %s", filename);
  ESP_LOGI(TAG, "===========================");

  // Use one of the following methods to initialise the decoder:
  bool decoded = JpegDec.decodeLfsFile(filename);  // Pass the SD file handle to the decoder,
  //bool decoded = JpegDec.decodeSdFile(filename);  // or pass the filename (String or character array)

  if (decoded) {
    // print information about the image to the serial port
    jpegInfo();
    // render the image onto the screen at given coordinates
    jpegRender(xpos, ypos);
  }
  else {
    ESP_LOGE(TAG, "Jpeg file format not supported!");
  }
}
#endif

//####################################################################################################
// Draw a JPEG on the TFT, images will be cropped on the right/bottom sides if they do not fit
//####################################################################################################
// This function assumes xpos,ypos is a valid screen coordinate. For convenience images that do not
// fit totally on the screen are cropped to the nearest MCU size and may leave right/bottom borders.
void jpegRender(int xpos, int ypos) {

  //jpegInfo(); // Print information from the JPEG file (could comment this line out)

  uint16_t *pImg;
  uint16_t mcu_w = JpegDec.MCUWidth;
  uint16_t mcu_h = JpegDec.MCUHeight;
  uint32_t max_x = JpegDec.width;
  uint32_t max_y = JpegDec.height;

  bool swapBytes = display.getSwapBytes();
  display.setSwapBytes(true);
  
  // Jpeg images are draw as a set of image block (tiles) called Minimum Coding Units (MCUs)
  // Typically these MCUs are 16x16 pixel blocks
  // Determine the width and height of the right and bottom edge image blocks
  uint32_t min_w = jpg_min(mcu_w, max_x % mcu_w);
  uint32_t min_h = jpg_min(mcu_h, max_y % mcu_h);

  // save the current image block size
  uint32_t win_w = mcu_w;
  uint32_t win_h = mcu_h;

  // record the current time so we can measure how long it takes to draw an image
  uint32_t drawTime = millis();

  // save the coordinate of the right and bottom edges to assist image cropping
  // to the screen size
  max_x += xpos;
  max_y += ypos;

  // Fetch data from the file, decode and display
  while (JpegDec.read()) {    // While there is more data in the file
    pImg = JpegDec.pImage ;   // Decode a MCU (Minimum Coding Unit, typically a 8x8 or 16x16 pixel block)

    // Calculate coordinates of top left corner of current MCU
    int mcu_x = JpegDec.MCUx * mcu_w + xpos;
    int mcu_y = JpegDec.MCUy * mcu_h + ypos;

    // check if the image block size needs to be changed for the right edge
    if (mcu_x + mcu_w <= max_x) win_w = mcu_w;
    else win_w = min_w;

    // check if the image block size needs to be changed for the bottom edge
    if (mcu_y + mcu_h <= max_y) win_h = mcu_h;
    else win_h = min_h;

    // copy pixels into a contiguous block
    if (win_w != mcu_w)
    {
      uint16_t *cImg;
      int p = 0;
      cImg = pImg + win_w;
      for (int h = 1; h < win_h; h++)
      {
        p += mcu_w;
        for (int w = 0; w < win_w; w++)
        {
          *cImg = *(pImg + w + p);
          cImg++;
        }
      }
    }

    // calculate how many pixels must be drawn
    uint32_t mcu_pixels = win_w * win_h;

    // draw image MCU block only if it will fit on the screen
    if (( mcu_x + win_w ) <= display.width() && ( mcu_y + win_h ) <= display.height())
      display.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
    else if ( (mcu_y + win_h) >= display.height())
      JpegDec.abort(); // Image has run off bottom of screen so abort decoding
  }

  display.setSwapBytes(swapBytes);

  //showTime(millis() - drawTime); // These lines are for sketch testing only
}

//####################################################################################################
// Print image information to the serial port (optional)
//####################################################################################################
// JpegDec.decodeFile(...) or JpegDec.decodeArray(...) must be called before this info is available!
void jpegInfo() {

  // Print information extracted from the JPEG file
  ESP_LOGI(TAG, "JPEG image info");
  ESP_LOGI(TAG, "===============");
  ESP_LOGI(TAG, "Width      : %d", JpegDec.width);
  ESP_LOGI(TAG, "Height     : %d", JpegDec.height);
  ESP_LOGI(TAG, "Components : %d", JpegDec.comps);
  ESP_LOGI(TAG, "MCU / row  : %d", JpegDec.MCUSPerRow);
  ESP_LOGI(TAG, "MCU / col  : %d", JpegDec.MCUSPerCol);
  ESP_LOGI(TAG, "Scan type  : %d", JpegDec.scanType);
  ESP_LOGI(TAG, "MCU width  : %d", JpegDec.MCUWidth);
  ESP_LOGI(TAG, "MCU height : %d", JpegDec.MCUHeight);
  ESP_LOGI(TAG, "===============");
}


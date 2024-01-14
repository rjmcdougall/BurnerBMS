// TODO:
// - automatically read the current multiplier and apply

#include "bq76952.h"
static const char *TAG = "bq76952";

// Library config
#define BQ_I2C_FREQ 100000

// BQ769x2 address is 0x10 including R/W bit or 0x8 as 7-bit address

#define BQ_I2C_ADDR_READ 0x8
#define BQ_I2C_ADDR_WRITE 0x8
bool BQ_DEBUG = false;

// BQ76952 - Address Map
#define CMD_DIR_SUBCMD_LOW 0x3E
#define CMD_DIR_SUBCMD_HI 0x3F
#define CMD_DIR_RESP_LEN 0x61
#define CMD_DIR_RESP_START 0x40
#define CMD_DIR_RESP_CHKSUM 0x60

// BQ76952 - Voltage measurement commands
#define CMD_READ_VOLTAGE_CELL_1 0x14
#define CMD_READ_VOLTAGE_CELL_2 0x16
#define CMD_READ_VOLTAGE_CELL_3 0x18
#define CMD_READ_VOLTAGE_CELL_4 0x1A
#define CMD_READ_VOLTAGE_CELL_5 0x1C
#define CMD_READ_VOLTAGE_CELL_6 0x1E
#define CMD_READ_VOLTAGE_CELL_7 0x20
#define CMD_READ_VOLTAGE_CELL_8 0x22
#define CMD_READ_VOLTAGE_CELL_9 0x24
#define CMD_READ_VOLTAGE_CELL_10 0x26
#define CMD_READ_VOLTAGE_CELL_11 0x28
#define CMD_READ_VOLTAGE_CELL_12 0x2A
#define CMD_READ_VOLTAGE_CELL_13 0x2C
#define CMD_READ_VOLTAGE_CELL_14 0x2E
#define CMD_READ_VOLTAGE_CELL_15 0x30
#define CMD_READ_VOLTAGE_CELL_16 0x32
#define CMD_READ_VOLTAGE_STACK 0x34
#define CMD_READ_VOLTAGE_PACK 0x36

// BQ76952 - Direct Commands
#define CMD_DIR_SPROTEC 0x02
#define CMD_DIR_FPROTEC 0x03
#define CMD_DIR_STEMP 0x04
#define CMD_DIR_FTEMP 0x05
#define CMD_DIR_SFET 0x06
#define CMD_DIR_FFET 0x07
#define CMD_DIR_VCELL_1 0x14
#define CMD_DIR_INT_TEMP 0x68
#define CMD_DIR_CC2_CUR 0x3A
#define CMD_DIR_FET_STAT 0x7F

/*
0-1 VREG18 16-bit ADC counts
2–3 VSS 16-bit ADC counts
4–5 Max Cell Voltage mV
6–7 Min Cell Voltage mV
8–9 Battery Voltage Sum cV
10–11 Avg Cell Temperature 0.1 K
12–13 FET Temperature 0.1 K
14–15 Max Cell Temperature 0.1 K
16–17 Min Cell Temperature 0.1 K
18–19 Avg Cell Temperature 0.1 K
20–21 CC3 Current userA
22–23 CC1 Current userA
24–27 Raw CC2 Counts 32-bit ADC counts
28–31 Raw CC3 Counts 32-bit ADC counts
*/

// 0x0075 DASTATUS5() Subcommand
#define CMD_DASTATUS5 0x75
#define DASTATUS_VREG18 0
#define DASTATUS_VSS 2
#define DASTATUS_MAXCELL 4
#define DASTATUS_MINCELL 6
#define DASTATUS_BATSUM 8
#define DASTATUS_TEMPCELLAVG 10
#define DASTATUS_TEMPFET 12
#define DASTATUS_TEMPCELLMAX 14
#define DASTATUS_TEMPCELLMIN 16
#define DASTATUS_TEMCELLLAVG 18
#define DASTATUS_CURRENTCC3 20
#define DASTATUS_CURRENTCC1 22
#define DASTATUS_RAWCC2_COUNTS 24
#define DASTATUS_RAWCC3_COUNTS 28

/* 0x68 Int Temperature Internal die temperature
0x6A CFETOFF Temperature CFETOFF pin thermistor
0x6C DFETOFF Temperature DFETOFF pin thermistor
0x6E ALERT Temperature ALERT pin thermistor
0x70 TS1 Temperature TS1 pin thermistor
0x72 TS2 Temperature TS2 pin thermistor
0x74 TS3 Temperature TS3 pin thermistor
0x76 HDQ Temperature HDQ pin thermistor
0x78 DCHG Temperature DCHG pin thermistor
0x7A DDSG Temperature DDSG pin thermistor
*/

// Alert Bits in BQ76952 registers
#define BIT_SA_SC_DCHG 7
#define BIT_SA_OC2_DCHG 6
#define BIT_SA_OC1_DCHG 5
#define BIT_SA_OC_CHG 4
#define BIT_SA_CELL_OV 3
#define BIT_SA_CELL_UV 2

#define BIT_SB_OTF 7
#define BIT_SB_OTINT 6
#define BIT_SB_OTD 5
#define BIT_SB_OTC 4
#define BIT_SB_UTINT 2
#define BIT_SB_UTD 1
#define BIT_SB_UTC 0

// Cell Balancing
#define CB_ACTIVE_CELLS 0x83
#define CB_STATUS1 0x85
#define CB_STATUS2 0x86
#define CB_STATUS3 0x87

// Inline functions
#define CELL_NO_TO_ADDR(cellNo) (0x14 + ((cellNo) * 2))
#define LOW_uint8_t(data) (uint8_t)(data & 0x00FF)
#define HIGH_uint8_t(data) (uint8_t)((data >> 8) & 0x00FF)

//// LOW LEVEL FUNCTIONS ////

bq76952::bq76952()
{
  // pinMode(alertPin, INPUT);
  //  TODO - Attach IRQ here
}

void bq76952::begin(void)
{
  BLog_d(TAG, "Releasing M5 external I2C...");
  // If using Wire library on M5, first need to release the external port from M5's library
  m5::I2C_Class *m5_i2c_class = &M5.Ex_I2C;
  m5_i2c_class->release();
  BLog_d(TAG, "Initializing Wire external port...");
  Wire1.end();
  Wire1.setPins(32, 33); // Core2 External
  Wire1.begin();
  Wire1.setClock(10000);
  // BLog_d(TAG, "Wire timeout %d", Wire1.getTimeout());
  // Wire1.setTimeout(500);
  // BLog_d(TAG, "Wire timeout %d", Wire1.getTimeout());
  BLog_d(TAG, "Initializing BQ76952...");
  //::cellMv = new uint32_t[20];
}

uint32_t bq76952::cellMv[16];
;
uint32_t bq76952::cellIsBalancing[16];
uint32_t bq76952::cellBalancingMs[16];
uint32_t bq76952::cellTempMin;
uint32_t bq76952::cellTempMax;
uint32_t bq76952::chipTemp;
uint32_t bq76952::bmsTemp;

uint32_t bq76952::voltageMultiplier = 10;
uint32_t bq76952::currentMultiplier = 10;

bool bq76952::update()
{
  BLog_d(TAG, "update");
  // Additional 3 to read top of stack voltages
  uint32_t cells[19];
  bool cellBalanceStatus[16];
  uint32_t cellBalanceTimes[16];
  BLog_d(TAG, "reading bq mode");
  uint32_t mode = getVcellMode();
  // BLog_d(TAG, "bq mode = %x", mode);
  getAllCellVoltages(&cells[0]);
  // getCellBalanceStatus(&cellBalanceStatus[0]);
  // getCellBalanceTimes(&cellBalanceTimes[0]);
  int configuredCell = 0;
  for (int i = 0; i < 16; i++)
  {
    // BLog_d(TAG, "bq cell = %d", i);
    if ((mode & (1 << i)) == false)
    {
      // i++;
      continue;
    }
    BLog_d(TAG, "cell %i (bq cell %i) %d mv", configuredCell, i, cells[i]); // - cellptr->BalanceCurrentCount);
    // BLog_d(TAG, "cell %i (bq cell %i) %d mv, %d balance (%d)", configuredCell, i, cells[i], cellBalanceTimes[i], cellBalanceStatus[i]); // - cellptr->BalanceCurrentCount);
    if (cells[i] > 0)
    {
      cellMv[configuredCell] = cells[i];
      // Estimate 25ma cell balance current * seconds of balancing
      // cellBalancingMs[configuredCell] = cellBalanceTimes[i] * 25 / 3600;
      // cellIsBalancing[configuredCell] = cellBalanceStatus[i];
    }
    configuredCell++;
  }
  BLog_d(TAG, "bq cells = %d", configuredCell);
  return true;
}

// Send Direct command
// readRegister16()
uint32_t bq76952::directCommand(uint8_t command)
{
  BLog_d(TAG, "Direct CMD  %d", command);
  BLog_d(TAG, "Direct BQ_I2C_ADDR_WRITE");
  Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
  Wire1.write(command);
  Wire1.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(2));
  BLog_d(TAG, "Direct CMD BQ_I2C_ADDR_READ");
  if (Wire1.requestFrom(BQ_I2C_ADDR_READ, 2) != 2)
  {
    BLog_d(TAG, "Direct CMD requestFrom failed...");
    return 0;
  }
  while (!Wire1.available())
  {
    BLog_d(TAG, "Direct CMD waiting...");
  };
  uint8_t lsb = Wire1.read();
  if (lsb == 0)
  {
    BLog_e(TAG, "Direct CMD  %d failed", command);
    return 0;
  }
  uint8_t msb = Wire1.read();
  if (msb == 0)
  {
    BLog_e(TAG, "Direct CMD  %d failed", command);
    return 0;
  }
  BLog_d(TAG, "Direct CMD %d +  resp %d", command, (uint16_t)(msb << 8 | lsb));
  return (uint32_t)(msb << 8 | lsb);
}

// Send Sub-command
void bq76952::subCommand(uint16_t data)
{
  BLog_d(TAG, "subCommand CMD %d", BQ_I2C_ADDR_WRITE);
  Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
  Wire1.write(CMD_DIR_SUBCMD_LOW);
  Wire1.write((uint8_t)data & 0x00FF);
  Wire1.write((uint8_t)(data >> 8) & 0x00FF);
  Wire1.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(2));
  BLog_d(TAG, "Sub Cmd SENT to 0x3E -> %d", data);
}

// Read subcommand response
uint16_t bq76952::subCommandResponseInt(void)
{
  BLog_d(TAG, "subCommandResponseInt");
  Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
  Wire1.write(CMD_DIR_RESP_START);
  Wire1.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(2));

  if (Wire1.requestFrom(BQ_I2C_ADDR_READ, 2) != 2)
  {
    BLog_d(TAG, "subCommandResponseInt requestFrom failed...");
    return 0;
  }

  while (!Wire1.available())
    ;
  uint8_t lsb = Wire1.read();
  uint8_t msb = Wire1.read();

  BLog_d(TAG, "[+] Sub Cmd uint16_t RESP at 0x40 -> %d ", (uint16_t)(msb << 8 | lsb));

  return (uint16_t)(msb << 8 | lsb);
}

bool bq76952::subCommandResponseBlock(uint8_t *data, uint16_t len)
{
  bool ret;
  uint8_t ret_len;

  BLog_d(TAG, "subCommandResponseInt");
  Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
  Wire1.write(CMD_DIR_RESP_LEN);
  Wire1.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(2));
  if (Wire1.requestFrom(BQ_I2C_ADDR_READ, 1) != 1)
  {
    BLog_d(TAG, "subCommandResponseBlock read len 1  failed...");
    return 0;
  }
  ret_len = Wire1.read();
  BLog_d(TAG, "BQsubCommandResponseBlock  len = %d", ret_len);

  int returned;
  BLog_d(TAG, "BQsubCommandResponseBlock Check len");
  if ((returned = Wire1.requestFrom(BQ_I2C_ADDR_READ, ret_len)) != ret_len)
  {
    BLog_d(TAG, "subCommandResponseBlock read len 1  failed %d returned", returned);
    return false;
  }

  BLog_d(TAG, "BQsubCommandResponseBlock Check len = %d, ret = %d", ret_len, ret);
  if (ret_len > 40)
  {
    ret_len = 40;
  }

  BLog_d(TAG, "BQsubCommandResponseBlock Send");

  // vTaskDelay(pdMS_TO_TICKS(10));

  for (int i = 0; i < ret_len; i++)
  {
    Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
    Wire1.write(CMD_DIR_RESP_START);
    Wire1.endTransmission();
    vTaskDelay(pdMS_TO_TICKS(2));
    if (Wire1.requestFrom(BQ_I2C_ADDR_READ, 1) != 1)
    {
      BLog_d(TAG, "subCommandResponseBlock CMD_DIR_RESP_START + %d  failed", i);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    BLog_d(TAG, "BQsubCommandResponseBlock data %x", data[i]);
  }
  vTaskDelay(pdMS_TO_TICKS(2));
  BLog_d(TAG, "BQsubCommandResponseBlock done");
  return true;
}

// Write uint8_t to Data memory of BQ76952
void bq76952::writeDataMemory(uint16_t addr, uint16_t data, uint16_t noOfBytes)
{
  uint8_t chksum = 0;
  chksum = computeChecksum(chksum, BQ_I2C_ADDR_WRITE);
  chksum = computeChecksum(chksum, CMD_DIR_SUBCMD_LOW);
  chksum = computeChecksum(chksum, LOW_uint8_t(addr));
  chksum = computeChecksum(chksum, HIGH_uint8_t(addr));
  chksum = computeChecksum(chksum, data);

  enterConfigUpdate();
  Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
  Wire1.write(CMD_DIR_SUBCMD_LOW);
  Wire1.write(LOW_uint8_t(addr));
  Wire1.write(HIGH_uint8_t(addr));
  Wire1.write(LOW_uint8_t(data));
  if (noOfBytes == 2)
    Wire1.write(HIGH_uint8_t(data));
  Wire1.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(2));

  Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
  Wire1.write(CMD_DIR_RESP_CHKSUM);
  Wire1.write(chksum);
  Wire1.write(0x05);
  Wire1.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(2));
  exitConfigUpdate();
}

// Read uint8_t from Data memory of BQ76952
bool bq76952::readDataMemory(uint16_t addr, uint8_t *data)
{
  BLog_d(TAG, "readDataMemory %x ...", addr);
  Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
  Wire1.write(CMD_DIR_SUBCMD_LOW);
  Wire1.write(LOW_uint8_t(addr));
  Wire1.write(HIGH_uint8_t(addr));
  Wire1.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(2));

  Wire1.beginTransmission(BQ_I2C_ADDR_WRITE);
  Wire1.write(CMD_DIR_RESP_START);
  Wire1.endTransmission();
  vTaskDelay(pdMS_TO_TICKS(2));

  if (Wire1.requestFrom(BQ_I2C_ADDR_READ, 1) != 1)
  {
    BLog_d(TAG, "readDataMemory requestFrom failed...");
    return false;
  }
  while (!Wire1.available())
    ;
  *data = Wire1.read();
  return true;
}

// Compute checksome = ~(sum of all uint8_ts)
uint8_t bq76952::computeChecksum(uint8_t oldChecksum, uint8_t data)
{
  if (!oldChecksum)
    oldChecksum = data;
  else
    oldChecksum = ~(oldChecksum) + data;
  return ~(oldChecksum);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

/////// API FUNCTIONS ///////

// Reset the BQ chip
void bq76952::reset(void)
{
  subCommand(0x0012);
  // BLog_d(TAG,"Resetting BQ76952...");
}

// Enter config update mode
void bq76952::enterConfigUpdate(void)
{
  subCommand(0x0090);
  vTaskDelay(pdMS_TO_TICKS(2));
}

// Exit config update mode
void bq76952::exitConfigUpdate(void)
{
  subCommand(0x0092);
  vTaskDelay(pdMS_TO_TICKS(1));
}

// Read single cell voltage
uint32_t bq76952::getCellVoltageMv(int cellNumber)
{
  BLog_d(TAG, "getCellVoltageMv %d", cellNumber);
  return cellMv[cellNumber];
}

// Read single cell voltage
uint32_t bq76952::getCellVoltageInternalMv(int cellNumber)
{
  uint16_t value;
  return directCommand(CELL_NO_TO_ADDR(cellNumber));
}

// Read All cell voltages in given array - Call like readAllCellVoltages(&myArray)
void bq76952::getAllCellVoltages(uint32_t *cellArray)
{
  // Additional 3 to read top of stack voltages
  for (int x = 0; x < 19; x++)
    //  for(uint8_t x=1;x<2;x++)
    cellArray[x] = getCellVoltageInternalMv(x);
}

// Get Cell Balancing Status per cell
void bq76952::getCellBalanceStatus(bool *cellArray)
{
  subCommand(CB_ACTIVE_CELLS);
  uint16_t status = subCommandResponseInt();
  // BLog_d(TAG," CB_ACTIVE_CELLS status %x", status);
  for (int i = 0; i < 16; i++)
  {
    cellArray[i] = ((status & (1 << i)) > 0);
  }
}

// Get balance times per cell
void bq76952::getCellBalanceTimes(uint32_t *cellArray)
{

  uint32_t buffer[32];

  subCommand(CB_STATUS2);
  subCommandResponseBlock((uint8_t *)&buffer[0], 32);
  // for (int i = 0; i < 8; i++) {
  //     BLog_d(TAG," balance: %x", buffer[i]);
  // }
  memcpy((uint8_t *)cellArray, &buffer[0], 32);
  subCommand(CB_STATUS3);
  subCommandResponseBlock((uint8_t *)&buffer[0], 32);
  memcpy(cellArray + 8, &buffer[0], 32);
  return;
}

#define CELL_STACK 16
#define CELL_PACK 17

uint32_t bq76952::getVoltageMv()
{
  return (bq76952::voltageMultiplier * bq76952::getCellVoltageInternalMv(CELL_STACK));
}

// Measure CC2 current
uint16_t bq76952::getCurrentMa(void)
{
  return (bq76952::currentMultiplier * directCommand(CMD_DIR_CC2_CUR));
}

// Measure chip temperature in °C
float bq76952::getInternalTemp(void)
{
  BLog_d(TAG,"getInternalTemp");
  int32_t raw = directCommand(CMD_DIR_INT_TEMP);
  BLog_d(TAG,"getInternalTemp raw %d", raw);
  float calc = raw / 10.0;
  if (raw == 0) {
    return 0;
  }
  // TODO: check why this isn't K
  // return (raw - 273.15);
  return (calc - 273.15);
}

// Get DASTATUS5
bool bq76952::getDASTATUS5()
{
  uint32_t value;
  subCommand(CMD_DASTATUS5);
  /*
   for (int i = 0; i < 10; i++) {
     read16(CMD_DASTATUS5, &value);
     BLog_d(TAG," CMD_DASTATUS5 status %x", value);
     vTaskDelay(pdMS_TO_TICKS(10));
   }
   */
  return subCommandResponseBlock(&subcmdCache[0], 32);
}

float bq76952::getMaxCellTemp()
{
  float raw = (subcmdCache[DASTATUS_TEMPCELLMAX] | (subcmdCache[DASTATUS_TEMPCELLMAX + 1] << 8)) / 10.0;
  return (raw - 273.15);
}

float bq76952::getMinCellTemp()
{
  float raw = (subcmdCache[DASTATUS_TEMPCELLMIN] | (subcmdCache[DASTATUS_TEMPCELLMIN + 1] << 8)) / 10.0;
  return (raw - 273.15);
}

uint32_t bq76952::getMaxCellVoltMv()
{
  float raw = (subcmdCache[DASTATUS_MAXCELL] | (subcmdCache[DASTATUS_MAXCELL + 1] << 8)) / 1000.0;
  return (raw);
}

uint32_t bq76952::getMinCellVoltMv()
{
  float raw = (subcmdCache[DASTATUS_MINCELL] | (subcmdCache[DASTATUS_MINCELL + 1] << 8)) / 1000.0;
  return (raw);
}

// Measure thermistor temperature in °C
float bq76952::getThermistorTemp(bq76952_thermistor thermistor)
{
  uint8_t cmd = 0;
  BLog_d(TAG, "getThermistorTemp...");

  switch (thermistor)
  {
  case TS1:
    cmd = 0x70;
    break;
  case TS2:
    cmd = 0x72;
    break;
  case TS3:
    cmd = 0x74;
    break;
  case HDQ:
    cmd = 0x76;
    break;
  case DCHG:
    cmd = 0x78;
    break;
  case DDSG:
    cmd = 0x7A;
    break;
  }
  if (cmd)
  {
    float raw = directCommand(cmd) / 10.0;
    if (raw == 0) {
      return 0.0;
    }
    return (raw - 273.15);
  }
  return 0.0;
}

// Check Primary Protection status
bq76952_protection_t bq76952::getProtectionStatus(void)
{
  bq76952_protection_t prot;
  uint8_t regData = (uint8_t)directCommand(CMD_DIR_FPROTEC);
  prot.bits.SC_DCHG = bitRead(regData, BIT_SA_SC_DCHG);
  prot.bits.OC2_DCHG = bitRead(regData, BIT_SA_OC2_DCHG);
  prot.bits.OC1_DCHG = bitRead(regData, BIT_SA_OC1_DCHG);
  prot.bits.OC_CHG = bitRead(regData, BIT_SA_OC_CHG);
  prot.bits.CELL_OV = bitRead(regData, BIT_SA_CELL_OV);
  prot.bits.CELL_UV = bitRead(regData, BIT_SA_CELL_UV);
  return prot;
}

// Check Temperature Protection status
bq76952_temperature_t bq76952::getTemperatureStatus(void)
{
  bq76952_temperature_t prot;
  uint8_t regData = (uint8_t)directCommand(CMD_DIR_FTEMP);
  prot.bits.OVERTEMP_FET = bitRead(regData, BIT_SB_OTC);
  prot.bits.OVERTEMP_INTERNAL = bitRead(regData, BIT_SB_OTINT);
  prot.bits.OVERTEMP_DCHG = bitRead(regData, BIT_SB_OTD);
  prot.bits.OVERTEMP_CHG = bitRead(regData, BIT_SB_OTC);
  prot.bits.UNDERTEMP_INTERNAL = bitRead(regData, BIT_SB_UTINT);
  prot.bits.UNDERTEMP_DCHG = bitRead(regData, BIT_SB_UTD);
  prot.bits.UNDERTEMP_CHG = bitRead(regData, BIT_SB_UTC);
  return prot;
}

void bq76952::setFET(bq76952_fet fet, bq76952_fet_state state)
{
  uint32_t subcmd;
  switch (state)
  {
  case FET_OFF:
    switch (fet)
    {
    case DCH:
      subcmd = 0x0093;
      break;
    case CHG:
      subcmd = 0x0094;
      break;
    default:
      subcmd = 0x0095;
      break;
    }
    break;
  case FET_ON:
    subcmd = 0x0096;
    break;
  }
  subCommand(subcmd);
}

// is Charging FET ON?
bool bq76952::isCharging(void)
{
  uint8_t regData = (uint8_t)directCommand(CMD_DIR_FET_STAT);
  if (regData & 0x01)
  {
    // BLog_d(TAG,"Charging FET -> ON");
    return true;
  }
  // BLog_d(TAG,"Charging FET -> OFF");
  return false;
}

// is Discharging FET ON?
bool bq76952::isDischarging(void)
{
  uint8_t regData = (uint8_t)directCommand(CMD_DIR_FET_STAT);
  if (regData & 0x04)
  {
    // BLog_d(TAG,"Discharging FET -> ON");
    return true;
  }
  BLog_d(TAG, "Discharging FET -> OFF");
  return false;
}

// Read vcell mode
uint32_t bq76952::getVcellMode(void)
{
  uint8_t vCellMode[2];
  if (!readDataMemory(0x9304, &vCellMode[0]))
  {
    BLog_e(TAG, "Error reading vcellMode0");
    return 0;
  }
  if (!readDataMemory(0x9305, &vCellMode[1]))
  {
    BLog_e(TAG, "Error reading vcellMode1");
    return 0;
  }
  BLog_d(TAG, "reading vcellMode = %d", (uint32_t)(vCellMode[1] << 8 | vCellMode[0]));

  return vCellMode[1] << 8 | vCellMode[0];
}

// Set user-defined overvoltage protection
void bq76952::setCellOvervoltageProtection(uint32_t mv, uint32_t ms)
{
  uint8_t thresh = (uint8_t)mv / 50.6;
  uint16_t dly = (uint16_t)(ms / 3.3) - 2;
  if (thresh < 20 || thresh > 110)
    thresh = 86;
  else
  {
    // BLog_d(TAG,"COV Threshold => ", thresh);
    writeDataMemory(0x9278, thresh, 1);
  }
  if (dly < 1 || dly > 2047)
    dly = 74;
  else
  {
    // BLog_d(TAG,"COV Delay => %i", dly);
    writeDataMemory(0x9279, dly, 2);
  }
}

// Set user-defined undervoltage protection
void bq76952::setCellUndervoltageProtection(uint32_t mv, uint32_t ms)
{
  uint8_t thresh = (uint8_t)mv / 50.6;
  uint16_t dly = (uint16_t)(ms / 3.3) - 2;
  if (thresh < 20 || thresh > 90)
    thresh = 50;
  else
  {
    BLog_d(TAG, "CUV Threshold => %i", thresh);
    writeDataMemory(0x9275, thresh, 1);
  }
  if (dly < 1 || dly > 2047)
    dly = 74;
  else
  {
    BLog_d(TAG, "CUV Delay => %i", dly);
    writeDataMemory(0x9276, dly, 2);
  }
}

// Set user-defined charging current protection
void bq76952::setChargingOvercurrentProtection(uint8_t mv, uint8_t ms)
{
  uint8_t thresh = (uint8_t)mv / 2;
  uint8_t dly = (uint8_t)(ms / 3.3) - 2;
  if (thresh < 2 || thresh > 62)
    thresh = 2;
  else
  {
    BLog_d(TAG, "OCC Threshold => %i", thresh);
    writeDataMemory(0x9280, thresh, 1);
  }
  if (dly < 1 || dly > 127)
    dly = 4;
  else
  {
    BLog_d(TAG, "OCC Delay => %i", dly);
    writeDataMemory(0x9281, dly, 1);
  }
}

// Set user-defined discharging current protection
void bq76952::setDischargingOvercurrentProtection(uint8_t mv, uint8_t ms)
{
  uint8_t thresh = (uint8_t)mv / 2;
  uint8_t dly = (uint8_t)(ms / 3.3) - 2;
  if (thresh < 2 || thresh > 100)
    thresh = 2;
  else
  {
    BLog_d(TAG, "OCD Threshold => %i", thresh);
    writeDataMemory(0x9282, thresh, 1);
  }
  if (dly < 1 || dly > 127)
    dly = 1;
  else
  {
    BLog_d(TAG, "OCD Delay => %i", dly);
    writeDataMemory(0x9283, dly, 1);
  }
}

// Set user-defined discharging current protection
void bq76952::setDischargingShortcircuitProtection(bq76952_scd_thresh thresh, uint32_t us)
{
  uint8_t dly = (uint8_t)(us / 15) + 1;
  BLog_d(TAG, "SCD Threshold => %i", thresh);
  writeDataMemory(0x9286, thresh, 1);
  if (dly < 1 || dly > 31)
    dly = 2;
  else
  {
    BLog_d(TAG, "SCD Delay (uS) => %i", dly);
    writeDataMemory(0x9287, dly, 1);
  }
}

// Set user-defined charging over temperature protection
void bq76952::setChargingTemperatureMaxLimit(signed int temp, uint8_t sec)
{
  if (temp < -40 || temp > 120)
    temp = 55;
  else
  {
    BLog_d(TAG, "OTC Threshold => %i", temp);
    writeDataMemory(0x929A, temp, 1);
  }
  if (sec < 0 || sec > 255)
    sec = 2;
  else
  {
    BLog_d(TAG, "OTC Delay => %i", sec);
    writeDataMemory(0x929B, sec, 1);
  }
}

// Set user-defined discharging over temperature protection
void bq76952::setDischargingTemperatureMaxLimit(signed int temp, uint8_t sec)
{
  if (temp < -40 || temp > 120)
    temp = 60;
  else
  {
    BLog_d(TAG, "OTD Threshold => %i", temp);
    writeDataMemory(0x929D, temp, 1);
  }
  if (sec < 0 || sec > 255)
    sec = 2;
  else
  {
    BLog_d(TAG, "OTD Delay => %i", sec);
    writeDataMemory(0x929E, sec, 1);
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////

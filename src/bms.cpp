#include "bms.h"

static constexpr const char *const TAG = "bms";

#undef BLog_d
#define BLog_d

// TODO:
// make this a single thread-update  model
// may need binding this thread to one core
// clients read from cached-state only

bq76952 BMS::bq;
BQ34Z100G1 BMS::bqz;
uint32_t BMS::cellMv[16];
uint32_t BMS::cellIsBalancing[16];
uint32_t BMS::cellBalancingMah[16];
uint32_t BMS::voltageMultiplier = 1;
uint32_t BMS::currentMultiplier = 10;
uint32_t BMS::numberCells;
uint32_t BMS::cellVoltageMinMv;
uint32_t BMS::cellVoltageMaxMv;
uint32_t BMS::voltageRangeMv;
uint32_t BMS::chipTempC;
uint32_t BMS::fETTempC;
uint32_t BMS::minCellTemp;
uint32_t BMS::maxCellTemp;
uint32_t BMS::voltageMv;
uint32_t BMS::socPct;
uint32_t BMS::sohPct;
uint32_t BMS::remainingCapacityMah;
uint32_t BMS::fullChargeCapacityMah;
uint32_t BMS::currentMa;
uint32_t BMS::currentInstantMa;
uint32_t BMS::chargeVoltage;
bool BMS::isChargingState = false;
std::map<std::string, int> BMS::bqzDiagData;

BMS::BMS()
{
}

void BMS::init()
{
    BLog_d(TAG, "Releasing M5 external I2C...");
    // If using Wire library on M5, first need to release the external port from M5's library
    m5::I2C_Class *m5_i2c_class = &M5.Ex_I2C;
    m5_i2c_class->release();
    BLog_d(TAG, "Initializing Wire external port...");
    Wire1.end();
    Wire1.setPins(32, 33); // Core2 External
    Wire1.begin();
    Wire1.setClock(100000);
    BLog_d(TAG, "Initializing BQ76952...");
    bq.begin();
    bqz.it_enable();
}

void BMS::update()
{
    BMS::bqUpdate();
    int volts = bqz.voltage();
    BLog_d(TAG, "bqz volts = %d..", volts);
}

int BMS::getVoltageLimLowMv() { return (3000); }
int BMS::getVoltageLimHighMv() { return (4200); }
int BMS::getNumberCells() { return (13); }

int BMS::getCellVoltageMv(int cell)
{
    // BLog_d(TAG,"getCellVoltageMv %d", cell);
    // return (bq.getCellVoltageMv(cell));
    return cellMv[cell];
}

int BMS::getCellVoltageMinMv(int cell)
{
    BLog_d(TAG, "getCellVoltageMinMv");
    return (cellVoltageMaxMv);
}

int BMS::getCellVoltageMaxMv(int cell)
{
    BLog_d(TAG, "getCellVoltageMaxMv");
    return (cellVoltageMaxMv);
}

float BMS::getChipTempC()
{
    BLog_d(TAG, "getChipTempC");
    return (chipTempC);
};

float BMS::getFETTempC()
{
    BLog_d(TAG, "getFETTempC");
    return (fETTempC);
}

float BMS::getMinCellTemp()
{
    BLog_d(TAG, "getMinCellTemp");
    return minCellTemp;
}

float BMS::getMaxCellTemp()
{
    BLog_d(TAG, "getMaxCellTemp");
    return maxCellTemp;
}

bool BMS::isCellBalancing(int cell) { return false; }

int BMS::getCellBalanceMah(int cell)
{
    return cellBalancingMah[cell];
}

int BMS::getVoltageMv()
{
    BLog_d(TAG, "getVoltageMv");
    return (voltageMv);
}

int BMS::getVoltageRangeMv()
{
    BLog_d(TAG, "getVoltageRangeMv");
    return (voltageRangeMv);
}

int BMS::getSocPct()
{
    return (socPct);
}

int BMS::getSohPct()
{
    return (sohPct);
}

unsigned int BMS::getRemainingCapacityMah() { return (remainingCapacityMah); }

unsigned int BMS::getFullChargeCapacityMah() { return (fullChargeCapacityMah); }

int BMS::getCurrentMa()
{
    BLog_d(TAG, "getCurrentMa");
    return (currentMa);
}

int BMS::getCurrentInstantMa()
{
    BLog_d(TAG, "getCurrentInstantMa");
    return (currentInstantMa);
}

// Read All cell voltages in given array - Call like readAllCellVoltages(&myArray)
void BMS::getAllCellVoltages(uint32_t *cellArray, uint32_t mode)
{
    // Additional 3 to read top of stack voltages
    for (int x = 0; x < 19; x++)
        //  for(uint8_t x=1;x<2;x++)
        if ((mode & (1 << x)))
        {
            cellArray[x] = bq.getCellVoltageMv(x);
        }
}

int BMS::getChargeVoltage()
{
    return chargeVoltage;
}

bool BMS::isCharging()
{
    return isChargingState;
}

bool BMS::isBalancing()
{
    bool balancing = false;
    for (int cell = 0; cell < numberCells; cell++)
    {
        if (cellIsBalancing[cell])
        {
            balancing = true;
        }
    }
    return balancing;
}

std::map<std::string, int> BMS::getBqzDiagData() {
    return bqzDiagData;
}


void BMS::bqUpdate()
{
    BLog_d(TAG, "update");
    // Additional 3 to read top of stack voltages
    uint32_t cells[19];
    bool cellBalanceStatus[16];
    uint32_t cellBalanceTimes[16];
    bq.getDASTATUS5();
    BLog_d(TAG, "reading bq mode");
    uint32_t mode = bq.getVcellMode();
    // BLog_d(TAG, "bq mode = %x", mode);
    getAllCellVoltages(&cells[0], mode);
    bq.getCellBalanceStatus(&cellBalanceStatus[0]);
    bq.getCellBalanceTimes(&cellBalanceTimes[0]);
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
            cellBalancingMah[configuredCell] = 0; // cellBalanceTimes[i] * 25 / 3600;
            cellIsBalancing[configuredCell] = cellBalanceStatus[i];
        }
        configuredCell++;
    }
    BLog_d(TAG, "bq cells = %d", configuredCell);
    cellVoltageMinMv = bq.getMinCellVoltMv();
    cellVoltageMaxMv = bq.getMaxCellVoltMv();
    chipTempC = bq.getInternalTemp();
    fETTempC = bq.getFETTemp();
    minCellTemp = bq.getMinCellTemp();
    maxCellTemp = bq.getMaxCellTemp();
    voltageRangeMv = bq.getMaxCellVoltMv() - bq.getMinCellVoltMv();
    voltageMv = bqz.voltage() * BMS::voltageMultiplier;
    socPct = bqz.state_of_charge();
    sohPct = bqz.state_of_health();
    remainingCapacityMah = (unsigned int)bqz.remaining_capacity() * BMS::currentMultiplier;
    fullChargeCapacityMah = (unsigned int)bqz.full_charge_capacity() * BMS::currentMultiplier;
    currentMa = bqz.average_current() * BMS::currentMultiplier;
    currentInstantMa = bqz.current() * BMS::currentMultiplier;
    chargeVoltage = (unsigned int)bqz.charge_voltage();
    isChargingState = bq.isCharging();
    bqzDiagData = bqz.getDiagData();
}

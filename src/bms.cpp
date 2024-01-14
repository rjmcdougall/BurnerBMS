#include "bms.h"

bq76952 BMS::bq;

BMS::BMS()
{
}

void BMS::init()
{
    bq.begin();
}

void BMS::update()
{
    bq.update();
}

int BMS::getVoltageLimLowMv() { return (3000); }
int BMS::getVoltageLimHighMv() { return (4200); }
int BMS::getNumberCells() { return (13); }

int BMS::getCellVoltageMv(int cell)
{
    // BLog_d(TAG,"getCellVoltageMv %d", cell);
    return (bq.getCellVoltageMv(cell));
}

int BMS::getCellVoltageMinMv(int cell)
{
    BLog_d(TAG, "getCellVoltageMinMv");
    return (bq.getMinCellVoltMv());
}

int BMS::getCellVoltageMaxMv(int cell)
{
    BLog_d(TAG, "getCellVoltageMaxMv");
    return (bq.getMaxCellVoltMv());
}

float BMS::getChipTempC()
{
    BLog_d(TAG, "getChipTempC");
    return (bq.getInternalTemp());
};

float BMS::getFETTempC()
{
    BLog_d(TAG, "getFETTempC");
    return (bq.getThermistorTemp((bq76952_thermistor)DCHG));
}

float BMS::getMinCellTemp()
{
    BLog_d(TAG, "getMinCellTemp");
    return bq.getMinCellTemp();
}

float BMS::getMaxCellTemp()
{
    BLog_d(TAG, "getMaxCellTemp");
    return bq.getMaxCellTemp();
}

bool BMS::isCellBalancing(int cell) { return false; }

int BMS::getCellBalanceCurrentCount(int cell) { return 0; }

int BMS::getVoltageMv()
{
    BLog_d(TAG, "getVoltageMv");
    return (bq.getVoltageMv());
}

int BMS::getVoltageRangeMv()
{
    BLog_d(TAG, "getVoltageRangeMv");
    return (bq.getMaxCellVoltMv() - bq.getMinCellVoltMv());
}

int BMS::getSocPct() { return (90); }

int BMS::getSohPct() { return (100); }

int BMS::getRemainingCapacityMah() { return (20000); }

int BMS::getFullChargeCapacityMah() { return (21000); }

int BMS::getCurrentMa()
{
    BLog_d(TAG, "getCurrentMa");
    return (bq.getCurrentMa());
}

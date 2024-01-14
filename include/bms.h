#include "burner_bms.h"

#include "bq76952.h"

class BMS {
    public:
        BMS();
        static void init();
        static void update();
        static int getVoltageLimLowMv();
        static int getVoltageLimHighMv();
        static int getNumberCells();
        static int getCellVoltageMv(int cell);
        static int getCellVoltageMinMv(int cell);
        static int getCellVoltageMaxMv(int cell);
        static int getCellInternalTempC(int cell);
        static float getChipTempC();
        static float getFETTempC();
        static float getMinCellTemp();
        static float getMaxCellTemp();
        static bool isCellBalancing(int cell);
        static int getCellBalanceCurrentCount(int cell);
        static int getVoltageMv();
        static int getVoltageRangeMv();
        static int getSocPct();
        static int getSohPct();
        static int getRemainingCapacityMah();
        static int getFullChargeCapacityMah();
        static int getCurrentMa();

    private:
        static bq76952 bq;

};

extern BMS bms;
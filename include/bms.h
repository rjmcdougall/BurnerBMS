#include "burner_bms.h"

#include "bq76952.h"
#include "bq34z100g1.h"
#include <map>


typedef enum bms_error {
    BMS_ERROR_OVERTEMP,
    BMS_ERROR_OVERCURRENT,
    BMS_ERROR_OVERVOLT,
    BMS_ERROR_UNDERVOLT,
    BMS_ERROR_UNDERTEMP
} bms_error_t;

typedef enum bms_state {
    BMS_STATE_IDLE,
    BMS_STATE_DISCHARGING,
    BMS_STATE_CHARGING,
    BMS_STATE_ERROR
} bms_state_t;

class BMS
{
public:
    BMS();
    static void init();
    static void update();
    static bms_state_t getBmsState();
    static bms_error_t getBmsError();
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
    static int getCellBalanceMah(int cell);
    static int getVoltageMv();
    static int getVoltageRangeMv();
    static int getSocPct();
    static int getSohPct();
    static unsigned int getRemainingCapacityMah();
    static unsigned int getFullChargeCapacityMah();
    static int getCurrentMa();
    static int getCurrentInstantMa();
    static int getChargeVoltage();
    static bool isCharging();
    static bool isBalancing();
    static std::map<std::string, int> getBqzDiagData();

private:
	static void getAllCellVoltages(uint32_t *cellArray, uint32_t mode);
    static void bqUpdate();
    static bq76952 bq;
    static BQ34Z100G1 bqz;
    static uint32_t cellMv[16];
    static uint32_t cellIsBalancing[16];
    static uint32_t cellBalancingMah[16];
    static uint32_t voltageMultiplier;
    static uint32_t currentMultiplier;
    static uint32_t numberCells;
    static uint32_t cellVoltageMinMv;
    static uint32_t cellVoltageMaxMv;
    static uint32_t voltageRangeMv;
    static uint32_t chipTempC;
    static uint32_t fETTempC;
    static uint32_t minCellTemp;
    static uint32_t maxCellTemp;
    static uint32_t voltageMv;
    static uint32_t socPct;
    static uint32_t sohPct;
    static uint32_t remainingCapacityMah;
    static uint32_t fullChargeCapacityMah;
    static uint32_t currentMa;
    static uint32_t currentInstantMa;
    static uint32_t chargeVoltage;
    static bool isChargingState;
    static std::map<std::string, int> bqzDiagData;
};

extern BMS bms;


#include "burner_bms.h"
#include <Wire.h>

	enum bq76952_thermistor
	{
		TS1,
		TS2,
		TS3,
		HDQ,
		DCHG,
		DDSG
	};

	enum bq76952_fet
	{
		CHG,
		DCH,
		ALL
	};

	enum bq76952_fet_state
	{
		FET_OFF,
		FET_ON
	};

	enum bq76952_scd_thresh
	{
		SCD_10,
		SCD_20,
		SCD_40,
		SCD_60,
		SCD_80,
		SCD_100,
		SCD_125,
		SCD_150,
		SCD_175,
		SCD_200,
		SCD_250,
		SCD_300,
		SCD_350,
		SCD_400,
		SCD_450,
		SCD_500
	};

	typedef union protection
	{
		struct
		{
			uint8_t SC_DCHG : 1;
			uint8_t OC2_DCHG : 1;
			uint8_t OC1_DCHG : 1;
			uint8_t OC_CHG : 1;
			uint8_t CELL_OV : 1;
			uint8_t CELL_UV : 1;
		} bits;
	} bq76952_protection_t;

	typedef union temperatureProtection
	{
		struct
		{
			uint8_t OVERTEMP_FET : 1;
			uint8_t OVERTEMP_INTERNAL : 1;
			uint8_t OVERTEMP_DCHG : 1;
			uint8_t OVERTEMP_CHG : 1;
			uint8_t UNDERTEMP_INTERNAL : 1;
			uint8_t UNDERTEMP_DCHG : 1;
			uint8_t UNDERTEMP_CHG : 1;
		} bits;
	} bq76952_temperature_t;
	
class bq76952
{

public:

	bq76952();
	void begin();
	void reset(void);
	bool isConnected(void);
	bool update(void);
	uint32_t getCellVoltageMv(int cellNumber);
	uint32_t getVoltageMv();
	uint16_t getCurrentMa(void);
	float getInternalTemp(void);
	bq76952_protection_t getProtectionStatus(void);
	bq76952_temperature_t getTemperatureStatus(void);

	void setFET(bq76952_fet, bq76952_fet_state);
	bool isDischarging(void);
	bool isCharging(void);
	void setDebug(bool);
	void setCellOvervoltageProtection(uint32_t, uint32_t);
	void setCellUndervoltageProtection(uint32_t, uint32_t);
	void setChargingOvercurrentProtection(uint8_t, uint8_t);
	void setChargingTemperatureMaxLimit(signed int, uint8_t);
	void setDischargingOvercurrentProtection(uint8_t, uint8_t);
	void setDischargingShortcircuitProtection(bq76952_scd_thresh, uint32_t);
	void setDischargingTemperatureMaxLimit(signed int, uint8_t);
	bool getDASTATUS5();
	float getMaxCellTemp();
	float getMinCellTemp();
	float getThermistorTemp(bq76952_thermistor);
	uint32_t getMaxCellVoltMv();
	uint32_t getMinCellVoltMv();

	// Cache for multi-uint8_t subcommands
	uint8_t subcmdCache[64];


private:
	uint32_t directCommand(uint8_t value);
	void subCommand(uint16_t value);
	uint16_t subCommandResponseInt(void);
	bool subCommandResponseBlock(uint8_t *data, uint16_t len);
	void enterConfigUpdate(void);
	void exitConfigUpdate(void);
	void writeDataMemory(uint16_t addr, uint16_t value, uint16_t len);
	bool readDataMemory(uint16_t addr, uint8_t *data);
	uint8_t computeChecksum(uint8_t oldChecksum, uint8_t data);
	bool read16(uint8_t reg, uint16_t *value);
	uint32_t getVcellMode(void);
	void getCellBalanceStatus(bool *cellArray);
	void getCellBalanceTimes(uint32_t *cellArray);
	void getAllCellVoltages(uint32_t *cellArray);
	uint32_t getCellVoltageInternalMv(int cell);

	static uint32_t cellTempMin;
	static uint32_t cellTempMax;
	static uint32_t chipTemp;
	static uint32_t bmsTemp;
	static uint32_t cellMv[16];
	static uint32_t cellIsBalancing[16];
	static uint32_t cellBalancingMs[16];
	static uint32_t voltageMultiplier;
	static uint32_t currentMultiplier;
};
//
//  bq34z100g1.cpp
//  SMC
//
//  Created by Kamran Ahmad on 08/05/2019.
//  Copyright © 2019 xkam1x. All rights reserved.
//

// Also see for flash https://github.com/Ralim/BQ34Z100/blob/master/bq34z100.cpp

#include "bq34z100g1.h"

#undef BLog_d
#define BLog_d

const uint8_t BQ34Z100_G1_ADDRESS = 0x55;

uint16_t BQ34Z100G1::read_register(uint8_t address, uint8_t length)
{
    Wire1.beginTransmission(BQ34Z100_G1_ADDRESS);
    Wire1.write(address);
    Wire1.endTransmission(false);
    Wire1.requestFrom(BQ34Z100_G1_ADDRESS, length, true);
    uint16_t temp = 0;
    for (uint8_t i = 0; i < length; i++)
    {
        temp |= Wire1.read() << (8 * i);
    }
    return temp;
}

uint16_t BQ34Z100G1::read_control(uint8_t address_lsb, uint8_t address_msb)
{
    Wire1.beginTransmission(BQ34Z100_G1_ADDRESS);
    Wire1.write(0x00);
    Wire1.write(address_lsb);
    Wire1.write(address_msb);
    Wire1.endTransmission(true);
    return read_register(0x00, 2);
}

void BQ34Z100G1::read_flash_block(uint8_t sub_class, uint8_t offset)
{
    write_reg(0x61, 0x00);        // Block control
    write_reg(0x3e, sub_class);   // Flash class
    write_reg(0x3f, offset / 32); // Flash block

    Wire1.beginTransmission(BQ34Z100_G1_ADDRESS);
    Wire1.write(0x40); // Block data
    Wire1.requestFrom(BQ34Z100_G1_ADDRESS, 32, true);
    for (uint8_t i = 0; i < 32; i++)
    {
        flash_block_data[i] = Wire1.read(); // Data
    }
}

void BQ34Z100G1::write_reg(uint8_t addr, uint8_t val)
{
    Wire1.beginTransmission(BQ34Z100_G1_ADDRESS);
    Wire1.write(addr);
    Wire1.write(val);
    Wire1.endTransmission(true);
}

void BQ34Z100G1::write_flash_block(uint8_t sub_class, uint8_t offset)
{
    write_reg(0x61, 0x00);        // Block control
    write_reg(0x3e, sub_class);   // Flash class
    write_reg(0x3f, offset / 32); // Flash block

    Wire1.beginTransmission(BQ34Z100_G1_ADDRESS);
    Wire1.write(0x40); // Block data
    for (uint8_t i = 0; i < 32; i++)
    {
        Wire1.write(flash_block_data[i]); // Data
    }
    Wire1.endTransmission(true);
}

uint8_t BQ34Z100G1::flash_block_checksum()
{
    uint8_t temp = 0;
    for (uint8_t i = 0; i < 32; i++)
    {
        temp += flash_block_data[i];
    }
    return 255 - temp;
}

double BQ34Z100G1::xemics_to_double(uint32_t value)
{
    bool is_negetive = false;
    if (value & 0x800000)
    {
        is_negetive = true;
    }
    int16_t exp_gain = (value >> 24) - 128 - 24;
    double exponent = pow(2, exp_gain);
    double mantissa = (int32_t)((value & 0xffffff) | 0x800000);
    if (is_negetive)
    {
        return mantissa * exponent * -1;
    }
    return mantissa * exponent;
}

uint32_t BQ34Z100G1::double_to_xemics(double value)
{
    bool is_negetive = false;
    if (value < 0)
    {
        is_negetive = true;
        value *= -1;
    }
    int8_t exponent;
    if (value > 1)
    {
        exponent = (log(value) / log(2)) + 1;
    }
    else
    {
        exponent = (log(value) / log(2));
    }
    double mantissa = value / (pow(2, (double)exponent) * pow(2, -24));
    if (is_negetive)
    {
        return (((exponent + 128) << 24) | (uint32_t)mantissa) | 0x800000;
    }
    return ((exponent + 128) << 24) | ((uint32_t)mantissa & 0x7fffff);
}

void BQ34Z100G1::unsealed()
{
    Wire1.beginTransmission(BQ34Z100_G1_ADDRESS);
    Wire1.write(0x00); // Control
    Wire1.write(0x14);
    Wire1.write(0x04);
    Wire1.endTransmission();

    Wire1.beginTransmission(BQ34Z100_G1_ADDRESS);
    Wire1.write(0x00); // Control
    Wire1.write(0x72);
    Wire1.write(0x36);
    Wire1.endTransmission();
}

void BQ34Z100G1::enter_calibration()
{
    unsealed();
    do
    {
        cal_enable();
        enter_cal();
        delay(1000);
    } while (!(control_status() & 0x1000)); // CALEN
}

void BQ34Z100G1::exit_calibration()
{
    do
    {
        exit_cal();
        delay(1000);
    } while (!(control_status() & ~0x1000)); // CALEN

    delay(150);
    reset();
    delay(150);
}

bool BQ34Z100G1::update_design_capacity(int16_t capacity)
{
    unsealed();
    read_flash_block(48, 0);

    flash_block_data[6] = 0; // Cycle Count
    flash_block_data[7] = 0;

    flash_block_data[8] = capacity >> 8; // CC Threshold
    flash_block_data[9] = capacity & 0xff;

    flash_block_data[11] = capacity >> 8; // Design Capacity
    flash_block_data[12] = capacity & 0xff;

    for (uint8_t i = 6; i <= 9; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    for (uint8_t i = 11; i <= 12; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);

    unsealed();
    read_flash_block(48, 0);
    int16_t updated_cc_threshold = flash_block_data[8] << 8;
    updated_cc_threshold |= flash_block_data[9];

    int16_t updated_capacity = flash_block_data[11] << 8;
    updated_capacity |= flash_block_data[12];

    if (flash_block_data[6] != 0 || flash_block_data[7] != 0)
    {
        return false;
    }
    if (capacity != updated_cc_threshold)
    {
        return false;
    }
    if (capacity != updated_capacity)
    {
        return false;
    }
    return true;
}

bool BQ34Z100G1::update_q_max(int16_t capacity)
{
    unsealed();
    read_flash_block(82, 0);
    flash_block_data[0] = capacity >> 8; // Q Max
    flash_block_data[1] = capacity & 0xff;

    flash_block_data[2] = 0; // Cycle Count
    flash_block_data[3] = 0;

    for (uint8_t i = 0; i <= 3; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);

    unsealed();
    read_flash_block(82, 0);
    int16_t updated_q_max = flash_block_data[0] << 8;
    updated_q_max |= flash_block_data[1];

    if (capacity != updated_q_max)
    {
        return false;
    }
    return true;
}

bool BQ34Z100G1::update_design_energy(int16_t energy)
{
    unsealed();
    read_flash_block(48, 0);
    flash_block_data[13] = energy >> 8; // Design Energy
    flash_block_data[14] = energy & 0xff;

    for (uint8_t i = 13; i <= 14; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);

    unsealed();
    read_flash_block(48, 0);
    int16_t updated_energy = flash_block_data[13] << 8;
    updated_energy |= flash_block_data[14];

    if (energy != updated_energy)
    {
        return false;
    }
    return true;
}

bool BQ34Z100G1::update_cell_charge_voltage_range(uint16_t t1_t2, uint16_t t2_t3, uint16_t t3_t4)
{
    unsealed();
    read_flash_block(48, 0);

    flash_block_data[17] = t1_t2 >> 8; // Cell Charge Voltage T1-T2
    flash_block_data[18] = t1_t2 & 0xff;

    flash_block_data[19] = t2_t3 >> 8; // Cell Charge Voltage T2-T3
    flash_block_data[20] = t2_t3 & 0xff;

    flash_block_data[21] = t3_t4 >> 8; // Cell Charge Voltage T3-T4
    flash_block_data[22] = t3_t4 & 0xff;

    for (uint8_t i = 17; i <= 22; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);

    unsealed();
    read_flash_block(48, 0);
    uint16_t updated_t1_t2 = flash_block_data[17] << 8;
    updated_t1_t2 |= flash_block_data[18];

    uint16_t updated_t2_t3 = flash_block_data[19] << 8;
    updated_t2_t3 |= flash_block_data[20];

    uint16_t updated_t3_t4 = flash_block_data[21] << 8;
    updated_t3_t4 |= flash_block_data[22];

    if (t1_t2 != updated_t1_t2 || t2_t3 != updated_t2_t3 || t3_t4 != updated_t3_t4)
    {
        return false;
    }
    return true;
}

bool BQ34Z100G1::update_number_of_series_cells(uint8_t cells)
{
    unsealed();
    read_flash_block(64, 0);

    flash_block_data[7] = cells; // Number of Series Cell

    write_reg(0x40 + 7, flash_block_data[7]);

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);

    unsealed();
    read_flash_block(64, 0);

    if (cells != flash_block_data[7])
    {
        return false;
    }
    return true;
}

bool BQ34Z100G1::update_pack_configuration(uint16_t config)
{
    unsealed();
    read_flash_block(64, 0);

    flash_block_data[0] = config >> 8; // Pack Configuration
    flash_block_data[1] = config & 0xff;

    for (uint8_t i = 0; i <= 1; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);

    unsealed();
    read_flash_block(64, 0);
    uint16_t updated_config = flash_block_data[0] << 8;
    updated_config |= flash_block_data[1];
    if (config != updated_config)
    {
        return false;
    }
    return true;
}

bool BQ34Z100G1::update_charge_termination_parameters(int16_t taper_current, int16_t min_taper_capacity, int16_t cell_taper_voltage, uint8_t taper_window, int8_t tca_set, int8_t tca_clear, int8_t fc_set, int8_t fc_clear)
{
    unsealed();
    read_flash_block(36, 0);

    flash_block_data[0] = taper_current >> 8; // Taper Current
    flash_block_data[1] = taper_current & 0xff;

    flash_block_data[2] = min_taper_capacity >> 8; // Min Taper Capacity
    flash_block_data[3] = min_taper_capacity & 0xff;

    flash_block_data[4] = cell_taper_voltage >> 8; // Cell Taper Voltage
    flash_block_data[5] = cell_taper_voltage & 0xff;

    flash_block_data[6] = taper_window; // Current Taper Window

    flash_block_data[7] = tca_set & 0xff; // TCA Set %

    flash_block_data[8] = tca_clear & 0xff; // TCA Clear %

    flash_block_data[9] = fc_set & 0xff; // FC Set %

    flash_block_data[10] = fc_clear & 0xff; // FC Clear %

    for (uint8_t i = 0; i <= 10; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);

    unsealed();
    read_flash_block(36, 0);
    int16_t updated_taper_current, updated_min_taper_capacity, updated_cell_taper_voltage;
    uint8_t updated_taper_window;
    int8_t updated_tca_set, updated_tca_clear, updated_fc_set, updated_fc_clear;

    updated_taper_current = flash_block_data[0] << 8;
    updated_taper_current |= flash_block_data[1];

    updated_min_taper_capacity = flash_block_data[2] << 8;
    updated_min_taper_capacity |= flash_block_data[3];

    updated_cell_taper_voltage = flash_block_data[4] << 8;
    updated_cell_taper_voltage |= flash_block_data[5];

    updated_taper_window = flash_block_data[6];

    updated_tca_set = flash_block_data[7] & 0xff;

    updated_tca_clear = flash_block_data[8] & 0xff;

    updated_fc_set = flash_block_data[9] & 0xff;

    updated_fc_clear = flash_block_data[10] & 0xff;

    if (taper_current != updated_taper_current)
    {
        return false;
    }
    if (min_taper_capacity != updated_min_taper_capacity)
    {
        return false;
    }
    if (cell_taper_voltage != updated_cell_taper_voltage)
    {
        return false;
    }
    if (taper_window != updated_taper_window)
    {
        return false;
    }
    if (tca_set != updated_tca_set)
    {
        return false;
    }
    if (tca_clear != updated_tca_clear)
    {
        return false;
    }
    if (fc_set != updated_fc_set)
    {
        return false;
    }
    if (fc_clear != updated_fc_clear)
    {
        return false;
    }
    return true;
}

void BQ34Z100G1::calibrate_cc_offset()
{
    enter_calibration();
    do
    {
        cc_offset();
        delay(1000);
    } while (!(control_status() & 0x0800)); // CCA

    do
    {
        delay(1000);
    } while (!(control_status() & ~0x0800)); // CCA

    cc_offset_save();
    exit_calibration();
}

void BQ34Z100G1::calibrate_board_offset()
{
    enter_calibration();
    do
    {
        board_offset();
        delay(1000);
    } while (!(control_status() & 0x0c00)); // CCA + BCA

    do
    {
        delay(1000);
    } while (!(control_status() & ~0x0c00)); // CCA + BCA

    cc_offset_save();
    exit_calibration();
}

void BQ34Z100G1::calibrate_voltage_divider(uint16_t applied_voltage, uint8_t cells_count)
{
    double volt_array[50];
    for (uint8_t i = 0; i < 50; i++)
    {
        volt_array[i] = voltage();
        delay(150);
    }
    double volt_mean = 0;
    for (uint8_t i = 0; i < 50; i++)
    {
        volt_mean += volt_array[i];
    }
    volt_mean /= 50.0;

    double volt_sd = 0;
    for (uint8_t i = 0; i < 50; i++)
    {
        volt_sd += pow(volt_array[i] - volt_mean, 2);
    }
    volt_sd /= 50.0;
    volt_sd = sqrt(volt_sd);

    if (volt_sd > 100)
    {
        return;
    }

    unsealed();
    read_flash_block(104, 0);

    uint16_t current_voltage_divider = flash_block_data[14] << 8;
    current_voltage_divider |= flash_block_data[15];

    uint16_t new_voltage_divider = ((double)applied_voltage / volt_mean) * (double)current_voltage_divider;

    flash_block_data[14] = new_voltage_divider >> 8;
    flash_block_data[15] = new_voltage_divider & 0xff;

    for (uint8_t i = 14; i <= 15; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());
    delay(150);

    unsealed();
    read_flash_block(68, 0);

    int16_t flash_update_of_cell_voltage = (double)(2800 * cells_count * 5000) / (double)new_voltage_divider;

    flash_block_data[0] = flash_update_of_cell_voltage << 8;
    flash_block_data[1] = flash_update_of_cell_voltage & 0xff;

    for (uint8_t i = 0; i <= 1; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);
}

void BQ34Z100G1::calibrate_sense_resistor(int16_t applied_current)
{
    double current_array[50];
    for (uint8_t i = 0; i < 50; i++)
    {
        current_array[i] = current();
        delay(150);
    }
    double current_mean = 0;
    for (uint8_t i = 0; i < 50; i++)
    {
        current_mean += current_array[i];
    }
    current_mean /= 50.0;

    double current_sd = 0;
    for (uint8_t i = 0; i < 50; i++)
    {
        current_sd += pow(current_array[i] - current_mean, 2);
    }
    current_sd /= 50.0;
    current_sd = sqrt(current_sd);

    if (current_sd > 100)
    {
        return;
    }

    unsealed();
    read_flash_block(104, 0);

    uint32_t cc_gain = flash_block_data[0] << 24;
    cc_gain |= flash_block_data[1] << 16;
    cc_gain |= flash_block_data[2] << 8;
    cc_gain |= flash_block_data[3];

    double gain_resistence = 4.768 / xemics_to_double(cc_gain);

    double temp = (current_mean * gain_resistence) / (double)applied_current;

    uint32_t new_cc_gain = double_to_xemics(4.768 / temp);
    flash_block_data[0] = new_cc_gain >> 24;
    flash_block_data[1] = new_cc_gain >> 16;
    flash_block_data[2] = new_cc_gain >> 8;
    flash_block_data[3] = new_cc_gain & 0xff;

    new_cc_gain = double_to_xemics(5677445.6 / temp);
    flash_block_data[4] = new_cc_gain >> 24;
    flash_block_data[5] = new_cc_gain >> 16;
    flash_block_data[6] = new_cc_gain >> 8;
    flash_block_data[7] = new_cc_gain & 0xff;

    for (uint8_t i = 0; i <= 3; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    for (uint8_t i = 4; i <= 7; i++)
    {
        write_reg(0x40 + i, flash_block_data[i]);
    }

    write_reg(0x60, flash_block_checksum());
    delay(150);
    reset();
    delay(150);
}

void BQ34Z100G1::set_current_deadband(uint8_t deadband)
{
    unsealed();
    read_flash_block(107, 0);

    flash_block_data[1] = deadband;

    write_reg(0x40 + 1, flash_block_data[1]);

    write_reg(0x60, flash_block_checksum());

    delay(150);
    reset();
    delay(150);
}

void BQ34Z100G1::ready()
{
    unsealed();
    it_enable();
    sealed();
}

uint16_t BQ34Z100G1::control_status()
{
    return read_control(0x00, 0x00);
}

uint16_t BQ34Z100G1::device_type()
{
    return read_control(0x01, 0x00);
}

uint16_t BQ34Z100G1::fw_version()
{
    return read_control(0x02, 0x00);
}

uint16_t BQ34Z100G1::hw_version()
{
    return read_control(0x03, 0x00);
}

uint16_t BQ34Z100G1::reset_data()
{
    return read_control(0x05, 0x00);
}

uint16_t BQ34Z100G1::prev_macwrite()
{
    return read_control(0x07, 0x00);
}

uint16_t BQ34Z100G1::chem_id()
{
    return read_control(0x08, 0x00);
}

uint16_t BQ34Z100G1::board_offset()
{
    return read_control(0x09, 0x00);
}

uint16_t BQ34Z100G1::cc_offset()
{
    return read_control(0x0a, 0x00);
}

uint16_t BQ34Z100G1::cc_offset_save()
{
    return read_control(0x0b, 0x00);
}

uint16_t BQ34Z100G1::df_version()
{
    return read_control(0x0c, 0x00);
}

uint16_t BQ34Z100G1::set_fullsleep()
{
    return read_control(0x10, 0x00);
}

uint16_t BQ34Z100G1::static_chem_chksum()
{
    return read_control(0x17, 0x00);
}

uint16_t BQ34Z100G1::sealed()
{
    return read_control(0x20, 0x00);
}

uint16_t BQ34Z100G1::it_enable()
{
    return read_control(0x21, 0x00);
}

uint16_t BQ34Z100G1::cal_enable()
{
    return read_control(0x2d, 0x00);
}

uint16_t BQ34Z100G1::reset()
{
    return read_control(0x41, 0x00);
}

uint16_t BQ34Z100G1::exit_cal()
{
    return read_control(0x80, 0x00);
}

uint16_t BQ34Z100G1::enter_cal()
{
    return read_control(0x81, 0x00);
}

uint16_t BQ34Z100G1::offset_cal()
{
    return read_control(0x82, 0x00);
}

uint8_t BQ34Z100G1::state_of_charge()
{
    return (uint8_t)read_register(0x02, 1);
}

uint8_t BQ34Z100G1::state_of_charge_max_error()
{
    return (uint8_t)read_register(0x03, 1);
}

uint16_t BQ34Z100G1::remaining_capacity()
{
    return read_register(0x04, 2);
}

uint16_t BQ34Z100G1::full_charge_capacity()
{
    return read_register(0x06, 2);
}

uint16_t BQ34Z100G1::voltage()
{
    return read_register(0x08, 2);
}

int16_t BQ34Z100G1::average_current()
{
    return (int16_t)read_register(0x0a, 2);
}

uint16_t BQ34Z100G1::temperature()
{
    return read_register(0x0c, 2);
}

uint16_t BQ34Z100G1::flags()
{
    return read_register(0x0e, 2);
}

uint16_t BQ34Z100G1::flags_b()
{
    return read_register(0x12, 2);
}

int16_t BQ34Z100G1::current()
{
    return (int16_t)read_register(0x10, 2);
}

uint16_t BQ34Z100G1::average_time_to_empty()
{
    return read_register(0x18, 2);
}

uint16_t BQ34Z100G1::average_time_to_full()
{
    return read_register(0x1a, 2);
}

int16_t BQ34Z100G1::passed_charge()
{
    return read_register(0x1c, 2);
}

uint16_t BQ34Z100G1::do_d0_time()
{
    return read_register(0x1e, 2);
}

uint16_t BQ34Z100G1::available_energy()
{
    return read_register(0x24, 2);
}

uint16_t BQ34Z100G1::average_power()
{
    return read_register(0x26, 2);
}

uint16_t BQ34Z100G1::serial_number()
{
    return read_register(0x28, 2);
}

uint16_t BQ34Z100G1::internal_temperature()
{
    return read_register(0x2a, 2);
}

uint16_t BQ34Z100G1::cycle_count()
{
    return read_register(0x2c, 2);
}

uint16_t BQ34Z100G1::state_of_health()
{
    return read_register(0x2e, 2);
}

uint16_t BQ34Z100G1::charge_voltage()
{
    return read_register(0x30, 2);
}

uint16_t BQ34Z100G1::charge_current()
{
    return read_register(0x32, 2);
}

uint16_t BQ34Z100G1::pack_configuration()
{
    return read_register(0x3a, 2);
}

uint16_t BQ34Z100G1::design_capacity()
{
    return read_register(0x3c, 2);
}

uint8_t BQ34Z100G1::grid_number()
{
    return (uint8_t)read_register(0x62, 1);
}

uint8_t BQ34Z100G1::learned_status()
{
    return (uint8_t)read_register(0x63, 1);
}

uint16_t BQ34Z100G1::dod_at_eoc()
{
    return read_register(0x64, 2);
}

uint16_t BQ34Z100G1::q_start()
{
    return read_register(0x66, 2);
}

uint16_t BQ34Z100G1::true_fcc()
{
    return read_register(0x6a, 2);
}

uint16_t BQ34Z100G1::state_time()
{
    return read_register(0x6c, 2);
}

uint16_t BQ34Z100G1::q_max_passed_q()
{
    return read_register(0x6e, 2);
}

uint16_t BQ34Z100G1::dod_0()
{
    return read_register(0x70, 2);
}

uint16_t BQ34Z100G1::q_max_dod_0()
{
    return read_register(0x72, 2);
}

uint16_t BQ34Z100G1::q_max_time()
{
    return read_register(0x74, 2);
}



std::map<std::string, int> BQ34Z100G1::getDiagData()
{
    std::map<std::string, int> diagData;

    diagData.insert({"hw_version", hw_version()});
    diagData.insert({"fw_version", fw_version()});
    diagData.insert({"device_type", device_type()});
    diagData.insert({"chem_id", chem_id()});
    diagData.insert({"control_status", control_status()});
    diagData.insert({"state_of_charge", state_of_charge()});
    diagData.insert({"state_of_charge_max_error", state_of_charge_max_error()});
    diagData.insert({"remaining_capacity", remaining_capacity()});
    diagData.insert({"full_charge_capacity", full_charge_capacity()});
    diagData.insert({"voltage", voltage()});
    diagData.insert({"average_current", average_current()});
    diagData.insert({"flags", flags()});
    diagData.insert({"flags_b", flags_b()});
    diagData.insert({"current", current()});
    diagData.insert({"average_time_to_empty", average_time_to_empty()});
    diagData.insert({"average_time_to_full", average_time_to_full()});
    diagData.insert({"passed_charge", passed_charge()});
    diagData.insert({"do_d0_time", do_d0_time()});
    diagData.insert({"available_energy", available_energy()});
    diagData.insert({"average_power", average_power()});
    diagData.insert({"serial_number", serial_number()});
    diagData.insert({"internal_temperature", internal_temperature()});
    diagData.insert({"cycle_count", cycle_count()});
    diagData.insert({"state_of_health", state_of_health()});
    diagData.insert({"charge_voltage", charge_voltage()});
    diagData.insert({"charge_current", charge_current()});
    diagData.insert({"design_capacity", design_capacity()});
    diagData.insert({"grid_number", grid_number()});
    diagData.insert({"dod_at_eoc", dod_at_eoc()});
    diagData.insert({"q_start", q_start()});
    diagData.insert({"true_fcc", true_fcc()});
    diagData.insert({"state_time", state_time()});
    diagData.insert({"q_max_passed_q", (int16_t)q_max_passed_q()});
    diagData.insert({"dod_0", dod_0()});
    diagData.insert({"q_max_dod_0", q_max_dod_0()});
    diagData.insert({"q_max_time", q_max_time()});

    control_status_t cs;
    cs.raw = control_status();

    diagData.insert({"cs_qen", (int)cs.bits.QEN});
    diagData.insert({"cs_vok", (int)cs.bits.VOK});
    diagData.insert({"cs_rup_dis", (int)cs.bits.RUP_DIS});
    diagData.insert({"cs_ldmd", (int)cs.bits.LDMD});
    diagData.insert({"cs_sleep", (int)cs.bits.SLEEP});
    diagData.insert({"cs_fullsleep", (int)cs.bits.FULLSLEEP});
    diagData.insert({"cs_csv", (int)cs.bits.CSV});
    diagData.insert({"cs_bca", (int)cs.bits.BCA});
    diagData.insert({"cs_calen", (int)cs.bits.CALEN});
    diagData.insert({"cs_ss", (int)cs.bits.SS});
    diagData.insert({"cs_fas", (int)cs.bits.SS});

    flags_a_t flags_a;
    flags_a.raw = flags();
    diagData.insert({"fa_ocvtaken", (int)flags_a.bits.OCVTAKEN});
    diagData.insert({"fa_chg", (int)flags_a.bits.CHG});
    diagData.insert({"fa_dsg", (int)flags_a.bits.DSG});

    learned_status_t learned_sts;
    learned_sts.raw = learned_status();
    diagData.insert({"learned_cf", (int)learned_sts.bits.CF});
    diagData.insert({"learned_iten", (int)learned_sts.bits.ITEN});

    return diagData;
}

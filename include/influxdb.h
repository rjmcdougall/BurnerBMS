#ifndef influxdb_H_
#define influxdb_H_

#pragma once

#include "burner_bms.h"

void influxEvent();
void influxEventDiag();

extern bool wifi_isconnected;

#endif
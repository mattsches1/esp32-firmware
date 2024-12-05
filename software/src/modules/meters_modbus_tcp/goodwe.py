specs = [
    {
        'name': 'Goodwe Hybrid Inverter',
        'register_type': 'HoldingRegister',
        'values': [
            {
                'name': 'Total Power of Inverter [W]',
                'value_id': 'PowerActiveLSumImExDiff',
                'start_address': 35138,
                'value_type': 'S16',
                'scale_factor': -1.0,
            },
            {
                'name': 'Inverter Internal Temperature [0.1 °C]',
                'value_id': 'TemperatureCabinet',
                'start_address': 35174,
                'value_type': 'S16',
                'scale_factor': 0.1,
            },
            {
                'name': 'Radiator Temperature [0.1 °C]',
                'value_id': 'TemperatureHeatSink',
                'start_address': 35176,
                'value_type': 'S16',
                'scale_factor': 0.1,
            },
            {
                'name': 'Total PV Energy [0.1 kWh]',
                'value_id': 'EnergyActiveLSumExport',
                'start_address': 35191,
                'value_type': 'U32BE',
                'scale_factor': 0.1,
            },
        ],
    },
    {
        'name': 'Goodwe Hybrid Inverter Grid',
        'register_type': 'HoldingRegister',
        'values': [
            {
                'name': 'R Phase Grid Voltage [0.1 V]',
                'value_id': 'VoltageL1N',
                'start_address': 35121,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'R Phase Grid Current [0.1 A]',
                'value_id': 'CurrentL1ImExSum',
                'start_address': 35122,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'R Phase Grid Frequency [0.01 Hz]',
                'value_id': 'FrequencyL1',
                'start_address': 35123,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'R Phase Grid Power [W]',  # FIXME: this value matches voltage times current, but seems to be too big compared to the total
                'value_id': 'PowerActiveL1ImExDiff',
                'start_address': 35125,
                'value_type': 'S16',
                'scale_factor': -1.0,
            },
            {
                'name': 'S Phase Grid Voltage [0.1 V]',
                'value_id': 'VoltageL2N',
                'start_address': 35126,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'S Phase Grid Current [0.1 A]',
                'value_id': 'CurrentL2ImExSum',
                'start_address': 35127,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'S Phase Grid Frequency [0.01 Hz]',
                'value_id': 'FrequencyL2',
                'start_address': 35128,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'S Phase Grid Power [W]',  # FIXME: this value matches voltage times current, but seems to be too big compared to the total
                'value_id': 'PowerActiveL2ImExDiff',
                'start_address': 35130,
                'value_type': 'S16',
                'scale_factor': -1.0,
            },
            {
                'name': 'T Phase Grid Voltage [0.1 V]',
                'value_id': 'VoltageL3N',
                'start_address': 35131,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'T Phase Grid Current [0.1 A]',
                'value_id': 'CurrentL3ImExSum',
                'start_address': 35132,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'T Phase Grid Frequency [0.01 Hz]',
                'value_id': 'FrequencyL3',
                'start_address': 35133,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'T Phase Grid Power [W]',  # FIXME: this value matches voltage times current, but seems to be too big compared to the total
                'value_id': 'PowerActiveL3ImExDiff',
                'start_address': 35135,
                'value_type': 'S16',
                'scale_factor': -1.0,
            },
            {
                'name': 'AC Active Power [W]',
                'value_id': 'PowerActiveLSumImExDiff',
                'start_address': 35140,
                'value_type': 'S16',
                'scale_factor': -1.0,
            },
            {
                'name': 'AC Reactive Power [var]',
                'value_id': 'PowerReactiveLSumIndCapDiff',
                'start_address': 35142,
                'value_type': 'S16',
                'scale_factor': -1.0,
            },
            {
                'name': 'AC Apparent Power [VA]',
                'value_id': 'PowerApparentLSumImExDiff',
                'start_address': 35144,
                'value_type': 'S16',
                'scale_factor': -1.0,
            },
            {
                'name': 'Total Feed Energy to Grid [0.1 kWh]',
                'value_id': 'EnergyActiveLSumExport',
                'start_address': 35195,
                'value_type': 'U32BE',
                'scale_factor': 0.1,
            },
            {
                'name': 'Total Draw Energy from Grid [0.1 kWh]',
                'value_id': 'EnergyActiveLSumImport',
                'start_address': 35200,
                'value_type': 'U32BE',
                'scale_factor': 0.1,
            },
        ],
    },
    {
        'name': 'Goodwe Hybrid Inverter Battery',
        'register_type': 'HoldingRegister',
        'values': [
            {
                'name': 'First Group Battery Voltage [0.1 V]',
                'value_id': 'VoltageDC',
                'start_address': 35180,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'First Group Battery Current [0.1 A]',
                'value_id': 'CurrentDCChaDisDiff',
                'start_address': 35181,
                'value_type': 'S16',
                'scale_factor': -0.1,
            },
            {
                'name': 'First Group Battery Power [W]',
                'value_id': 'PowerDCChaDisDiff',
                'start_address': 35183,
                'value_type': 'S16',
                'scale_factor': -1.0,
            },
            {
                'name': 'Charge Energy [0.1 kWh]',
                'value_id': 'EnergyDCCharge',
                'start_address': 35206,
                'value_type': 'U32BE',
                'scale_factor': 0.1,
            },
            {
                'name': 'Discharge Energy [0.1 kWh]',
                'value_id': 'EnergyDCDischarge',
                'start_address': 35209,
                'value_type': 'U32BE',
                'scale_factor': 0.1,
            },
            {
                'name': 'BMS Pack Temperature [0.1 °C]',
                'value_id': 'Temperature',
                'start_address': 37003,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'First Group Battery Capacity [%]',
                'value_id': 'StateOfCharge',
                'start_address': 37007,
                'value_type': 'U16',
            },
        ],
    },
    {
        'name': 'Goodwe Hybrid Inverter Load',
        'register_type': 'HoldingRegister',
        'values': [
            {
                'name': 'R Phase Load Power [W]',
                'value_id': 'PowerActiveL1ImExDiff',
                'start_address': 35164,
                'value_type': 'S16',
            },
            {
                'name': 'S Phase Load Power [W]',
                'value_id': 'PowerActiveL2ImExDiff',
                'start_address': 35166,
                'value_type': 'S16',
            },
            {
                'name': 'T Phase Load Power [W]',
                'value_id': 'PowerActiveL3ImExDiff',
                'start_address': 35168,
                'value_type': 'S16',
            },
            {
                'name': 'Total Load Power [W]',  # FIXME: this seems to be less than the sum of the RST phase load powers
                'value_id': 'PowerActiveLSumImExDiff',
                'start_address': 35172,
                'value_type': 'S16',
            },
            {
                'name': 'Total Energy of Load [0.1 kWh]',
                'value_id': 'EnergyActiveLSumImport',
                'start_address': 35200,
                'value_type': 'U32BE',
                'scale_factor': 0.1,
            },
        ],
    },
    {
        'name': 'Goodwe Hybrid Inverter Backup Load',
        'register_type': 'HoldingRegister',
        'values': [
            {
                'name': 'R Phase Load Voltage of Backup [0.1 V]',
                'value_id': 'VoltageL1N',
                'start_address': 35145,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'R Phase Load Current of Backup [0.1 A]',
                'value_id': 'CurrentL1ImExSum',
                'start_address': 35146,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'R Phase Load Frequency of Backup [0.01 Hz]',
                'value_id': 'FrequencyL1',
                'start_address': 35147,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'R Phase Load Power of Backup [W]',
                'value_id': 'PowerActiveL1ImExDiff',
                'start_address': 35150,
                'value_type': 'S16',
            },
            {
                'name': 'S Phase Load Voltage of Backup [0.1 V]',
                'value_id': 'VoltageL2N',
                'start_address': 35151,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'S Phase Load Current of Backup [0.1 A]',
                'value_id': 'CurrentL2ImExSum',
                'start_address': 35152,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'S Phase Load Frequency of Backup [0.01 Hz]',
                'value_id': 'FrequencyL2',
                'start_address': 35153,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'S Phase Load Power of Backup [W]',
                'value_id': 'PowerActiveL2ImExDiff',
                'start_address': 35156,
                'value_type': 'S16',
            },
            {
                'name': 'T Phase Load Voltage of Backup [0.1 V]',
                'value_id': 'VoltageL3N',
                'start_address': 35157,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'T Phase Load Current of Backup [0.1 A]',
                'value_id': 'CurrentL3ImExSum',
                'start_address': 35158,
                'value_type': 'U16',
                'scale_factor': 0.1,
            },
            {
                'name': 'T Phase Load Frequency of Backup [0.01 Hz]',
                'value_id': 'FrequencyL3',
                'start_address': 35159,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'T Phase Load Power of Backup [W]',
                'value_id': 'PowerActiveL3ImExDiff',
                'start_address': 35162,
                'value_type': 'S16',
            },
            {
                'name': 'Total Load Power of Backup [W]',  # FIXME: this seems to be less than the sum of the RST phase load powers
                'value_id': 'PowerActiveLSumImExDiff',
                'start_address': 35170,
                'value_type': 'S16',
            },
        ],
    },
    {
        'name': 'Goodwe Hybrid Inverter Meter',
        'register_type': 'HoldingRegister',
        'values': [
            {
                'name': 'R Phase Active Power [W]',
                'value_id': 'PowerActiveL1ImExDiff',
                'start_address': 36005,
                'value_type': 'S16',
            },
            {
                'name': 'S Phase Active Power [W]',
                'value_id': 'PowerActiveL2ImExDiff',
                'start_address': 36006,
                'value_type': 'S16',
            },
            {
                'name': 'T Phase Active Power [W]',
                'value_id': 'PowerActiveL3ImExDiff',
                'start_address': 36007,
                'value_type': 'S16',
            },
            {
                'name': 'Total Active Power [W]',
                'value_id': 'PowerActiveLSumImExDiff',
                'start_address': 36008,
                'value_type': 'S16',
            },
            {
                'name': 'Total Reactive Power [W]',
                'value_id': 'PowerReactiveLSumIndCapDiff',
                'start_address': 36009,
                'value_type': 'S16',
            },
            {
                'name': 'R Phase Power Factor [0.01]',
                'value_id': 'PowerFactorL1',
                'start_address': 36010,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'S Phase Power Factor [0.01]',
                'value_id': 'PowerFactorL2',
                'start_address': 36011,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'T Phase Power Factor [0.01]',
                'value_id': 'PowerFactorL3',
                'start_address': 36012,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'Total Power Factor [0.01]',
                'value_id': 'PowerFactorLSum',
                'start_address': 36013,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'Frequency [0.01 Hz]',
                'value_id': 'FrequencyLAvg',
                'start_address': 36014,
                'value_type': 'U16',
                'scale_factor': 0.01,
            },
            {
                'name': 'Total Feed Energy to Grid [0.1 kWh]',
                'value_id': 'EnergyActiveLSumExport',
                'start_address': 36015,
                'value_type': 'F32BE',
                'scale_factor': 0.1,
            },
            {
                'name': 'Total Draw Energy from Grid [0.1 kWh]',
                'value_id': 'EnergyActiveLSumImport',
                'start_address': 36017,
                'value_type': 'F32BE',
                'scale_factor': 0.1,
            },
            {
                'name': 'R Phase Active Power S32 [W]',
                #'value_id': 'PowerActiveL1ImExDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36019,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'S Phase Active Power S32 [W]',
                #'value_id': 'PowerActiveL2ImExDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36021,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'T Phase Active Power S32 [W]',
                #'value_id': 'PowerActiveL3ImExDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36023,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'Total Active Power S32 [W]',
                #'value_id': 'PowerActiveLSumImExDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36025,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'R Phase Reactive Power S32 [W]',
                #'value_id': 'PowerReactiveL1IndCapDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36027,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'S Phase Reactive Power S32 [var]',
                #'value_id': 'PowerReactiveL2IndCapDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36029,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'T Phase Reactive Power S32 [var]',
                #'value_id': 'PowerReactiveL3IndCapDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36031,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'Total Reactive Power S32 [var]',
                #'value_id': 'PowerReactiveLSumIndCapDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36033,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'R Phase Apparent Power S32 [VA]',
                #'value_id': 'PowerApparentL1ImExDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36035,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'S Phase Apparent Power S32 [VA]',
                #'value_id': 'PowerApparentL2ImExDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36037,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'T Phase Apparent Power S32 [VA]',
                #'value_id': 'PowerApparentL3ImExDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36039,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
            {
                'name': 'Total Apparent Power S32 [VA]',
                #'value_id': 'PowerApparentLSumImExDiff',
                'value_id': 'VALUE_ID_DEBUG',
                'start_address': 36041,
                'value_type': 'S32BE',
                'scale_factor': -1.0,
            },
        ],
    },
]

//PiLab Panel - Angelscript Editor

void SetStatus(const string &in msg)
{
    TagWriteString("Script_Status", msg);
    LogInfo(msg);
}

void UpdateProcessValues()
{
    bool running = TagReadBool("Motor_Running", false);
    float sp = TagReadFloat("Motor_Speed_SP", 1200.0f);
    float levelSp = TagReadFloat("Tank_Level_SP", 55.0f);
    float tempSp = TagReadFloat("Temp_SP", 32.0f);

    if (running)
    {
        TagWriteFloat("Motor_Speed_PV", sp);
        TagWriteFloat("Tank_Level", levelSp);
        TagWriteFloat("Temp_PV", tempSp);
        TagWriteFloat("Flow_PV", 62.5f);
        TagWriteFloat("Pressure_PV", 245.0f);
        TagWriteBool("Pump_Enable", true);
        TagWriteBool("Valve_Open", true);
    }
    else
    {
        TagWriteFloat("Motor_Speed_PV", 0.0f);
        TagWriteFloat("Flow_PV", 0.0f);
        TagWriteFloat("Pressure_PV", 20.0f);
        TagWriteBool("Pump_Enable", false);
        TagWriteBool("Valve_Open", false);
    }
}

void OnPanelStart()
{
    TagWriteString("Factory_Title", "PiLab Panel S3 Factory Demo");
    TagWriteBool("Run_Command", false);
    TagWriteBool("Motor_Running", false);
    TagWriteBool("Pump_Enable", false);
    TagWriteBool("Valve_Open", false);
    TagWriteBool("Fault_Active", false);
    TagWriteBool("Auto_Mode", true);
    TagWriteFloat("Motor_Speed_SP", 1200.0f);
    TagWriteFloat("Tank_Level_SP", 55.0f);
    TagWriteFloat("Temp_SP", 32.0f);
    TagWriteFloat("Tank_Level", 45.0f);
    TagWriteFloat("Temp_PV", 24.0f);
    TagWriteFloat("Flow_PV", 0.0f);
    TagWriteFloat("Pressure_PV", 20.0f);
    TagWriteFloat("Batch_Count", 1.0f);
    UpdateProcessValues();
    SetStatus("Factory demo ready - tap START / STOP");
}

void StartStop_Click()
{
    bool running = TagReadBool("Motor_Running", false);
    running = !running;
    TagWriteBool("Run_Command", running);
    TagWriteBool("Motor_Running", running);
    if (running)
    {
        float count = TagReadFloat("Batch_Count", 0.0f) + 1.0f;
        TagWriteFloat("Batch_Count", count);
    }
    UpdateProcessValues();
    SetStatus(running ? "Running - process values updated" : "Stopped from START / STOP");
}

void Stop_Click()
{
    TagWriteBool("Run_Command", false);
    TagWriteBool("Motor_Running", false);
    UpdateProcessValues();
    SetStatus("Stopped");
}

void AlarmReset_Click()
{
    TagWriteBool("Fault_Active", false);
    SetStatus("Alarm reset pressed");
}

void AutoMode_Changed()
{
    bool autoMode = TagReadBool("Auto_Mode", false);
    SetStatus(autoMode ? "Auto mode enabled" : "Manual mode enabled");
}

void SpeedSetpoint_Changed()
{
    UpdateProcessValues();
    SetStatus("Speed setpoint changed");
}

void LevelSetpoint_Changed()
{
    UpdateProcessValues();
    SetStatus("Tank level setpoint changed");
}

void TempSetpoint_Changed()
{
    UpdateProcessValues();
    SetStatus("Temperature setpoint changed");
}

void MainPage_Click()
{
    ScreenShow("Main");
    SetStatus("Main screen");
}

void ProcessPage_Click()
{
    ScreenShow("Process");
    SetStatus("Process screen");
}

void DiagnosticsPage_Click()
{
    ScreenShow("Diagnostics");
    SetStatus("Diagnostics screen");
}


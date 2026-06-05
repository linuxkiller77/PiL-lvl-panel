const isDev = import.meta.env.DEV;

const DEFAULT_LAYOUT = {
  "version": 2,
  "startupPage": "Main",
  "screen": {
    "width": 800,
    "height": 480,
    "background": "#101828"
  },
  "pages": [
    {
      "id": "Main",
      "name": "Main",
      "widgets": [
        {
          "id": "title",
          "type": "label",
          "x": 20,
          "y": 14,
          "w": 430,
          "h": 42,
          "props": {
            "label": "PiLab Panel S3 Factory Demo",
            "pin": "Factory_Title",
            "color": "#FFFFFF",
            "fontSize": 28
          }
        },
        {
          "id": "heartbeat",
          "type": "led",
          "x": 714,
          "y": 14,
          "w": 66,
          "h": 70,
          "props": {
            "label": "HB",
            "pin": "Panel_Heartbeat",
            "color": "#22C55E"
          }
        },
        {
          "id": "status",
          "type": "label",
          "x": 24,
          "y": 62,
          "w": 560,
          "h": 36,
          "props": {
            "label": "Status",
            "pin": "Script_Status",
            "color": "#FACC15",
            "fontSize": 21
          }
        },
        {
          "id": "start_stop",
          "type": "button",
          "x": 30,
          "y": 118,
          "w": 164,
          "h": 72,
          "events": {
            "onClick": "StartStop_Click"
          },
          "props": {
            "label": "START / STOP",
            "pin": "Run_Command",
            "buttonMode": "script",
            "color": "#0EA5E9"
          }
        },
        {
          "id": "stop",
          "type": "button",
          "x": 210,
          "y": 118,
          "w": 112,
          "h": 72,
          "events": {
            "onClick": "Stop_Click"
          },
          "props": {
            "label": "STOP",
            "pin": "Run_Command",
            "buttonMode": "script",
            "color": "#EF4444"
          }
        },
        {
          "id": "motor_led",
          "type": "led",
          "x": 348,
          "y": 112,
          "w": 112,
          "h": 88,
          "props": {
            "label": "Motor",
            "pin": "Motor_Running",
            "color": "#22C55E"
          }
        },
        {
          "id": "pump_led",
          "type": "led",
          "x": 478,
          "y": 112,
          "w": 112,
          "h": 88,
          "props": {
            "label": "Pump",
            "pin": "Pump_Enable",
            "color": "#38BDF8"
          }
        },
        {
          "id": "speed_pv",
          "type": "readout",
          "x": 610,
          "y": 112,
          "w": 160,
          "h": 88,
          "props": {
            "label": "Speed PV",
            "pin": "Motor_Speed_PV",
            "unit": " rpm",
            "decimals": 0,
            "min": 0,
            "max": 5000,
            "color": "#38BDF8"
          }
        },
        {
          "id": "speed_sp",
          "type": "setpoint",
          "x": 30,
          "y": 222,
          "w": 180,
          "h": 96,
          "events": {
            "onChange": "SpeedSetpoint_Changed"
          },
          "props": {
            "label": "Speed SP",
            "pin": "Motor_Speed_SP",
            "unit": " rpm",
            "min": 0,
            "max": 5000,
            "step": 1,
            "decimals": 0,
            "color": "#F97316"
          }
        },
        {
          "id": "auto_toggle",
          "type": "toggle",
          "x": 232,
          "y": 226,
          "w": 150,
          "h": 86,
          "events": {
            "onChange": "AutoMode_Changed"
          },
          "props": {
            "label": "Auto",
            "pin": "Auto_Mode",
            "color": "#A78BFA"
          }
        },
        {
          "id": "fault_led",
          "type": "led",
          "x": 405,
          "y": 222,
          "w": 110,
          "h": 92,
          "props": {
            "label": "Fault",
            "pin": "Fault_Active",
            "color": "#F43F5E"
          }
        },
        {
          "id": "temp_pv",
          "type": "readout",
          "x": 540,
          "y": 222,
          "w": 110,
          "h": 92,
          "props": {
            "label": "Temp",
            "pin": "Temp_PV",
            "unit": " C",
            "decimals": 1,
            "min": 0,
            "max": 100,
            "color": "#F59E0B"
          }
        },
        {
          "id": "uptime",
          "type": "readout",
          "x": 668,
          "y": 222,
          "w": 104,
          "h": 92,
          "props": {
            "label": "Uptime",
            "pin": "Panel_Uptime_s",
            "unit": " s",
            "decimals": 0,
            "min": 0,
            "max": 1000000,
            "color": "#CBD5E1"
          }
        },
        {
          "id": "to_process",
          "type": "button",
          "x": 30,
          "y": 350,
          "w": 150,
          "h": 58,
          "events": {
            "onClick": "ProcessPage_Click"
          },
          "props": {
            "label": "PROCESS",
            "buttonMode": "script",
            "color": "#16A34A"
          }
        },
        {
          "id": "to_diag",
          "type": "button",
          "x": 200,
          "y": 350,
          "w": 150,
          "h": 58,
          "events": {
            "onClick": "DiagnosticsPage_Click"
          },
          "props": {
            "label": "DIAG",
            "buttonMode": "script",
            "color": "#64748B"
          }
        },
        {
          "id": "alarm_reset",
          "type": "button",
          "x": 370,
          "y": 350,
          "w": 166,
          "h": 58,
          "events": {
            "onClick": "AlarmReset_Click"
          },
          "props": {
            "label": "RESET ALARM",
            "buttonMode": "script",
            "color": "#7C3AED"
          }
        },
        {
          "id": "note",
          "type": "label",
          "x": 555,
          "y": 348,
          "w": 222,
          "h": 62,
          "props": {
            "label": "Tap setpoints for numeric keypad.",
            "color": "#94A3B8",
            "fontSize": 17
          }
        }
      ]
    },
    {
      "id": "Process",
      "name": "Process",
      "widgets": [
        {
          "id": "process_title",
          "type": "label",
          "x": 24,
          "y": 14,
          "w": 360,
          "h": 42,
          "props": {
            "label": "Process View",
            "color": "#FFFFFF",
            "fontSize": 28
          }
        },
        {
          "id": "status2",
          "type": "label",
          "x": 24,
          "y": 62,
          "w": 520,
          "h": 34,
          "props": {
            "label": "Status",
            "pin": "Script_Status",
            "color": "#FACC15",
            "fontSize": 20
          }
        },
        {
          "id": "tank",
          "type": "tank",
          "x": 38,
          "y": 115,
          "w": 160,
          "h": 250,
          "props": {
            "label": "Tank",
            "pin": "Tank_Level",
            "unit": " %",
            "min": 0,
            "max": 100,
            "decimals": 0,
            "color": "#06B6D4"
          }
        },
        {
          "id": "level_sp",
          "type": "setpoint",
          "x": 226,
          "y": 120,
          "w": 170,
          "h": 94,
          "events": {
            "onChange": "LevelSetpoint_Changed"
          },
          "props": {
            "label": "Level SP",
            "pin": "Tank_Level_SP",
            "unit": " %",
            "min": 0,
            "max": 100,
            "step": 5,
            "decimals": 0,
            "color": "#22C55E"
          }
        },
        {
          "id": "flow",
          "type": "gauge",
          "x": 226,
          "y": 245,
          "w": 210,
          "h": 100,
          "props": {
            "label": "Flow",
            "pin": "Flow_PV",
            "unit": " L/min",
            "min": 0,
            "max": 100,
            "decimals": 1,
            "color": "#38BDF8"
          }
        },
        {
          "id": "pressure",
          "type": "gauge",
          "x": 462,
          "y": 120,
          "w": 210,
          "h": 100,
          "props": {
            "label": "Pressure",
            "pin": "Pressure_PV",
            "unit": " kPa",
            "min": 0,
            "max": 500,
            "decimals": 0,
            "color": "#A78BFA"
          }
        },
        {
          "id": "temp_sp",
          "type": "setpoint",
          "x": 462,
          "y": 245,
          "w": 170,
          "h": 94,
          "events": {
            "onChange": "TempSetpoint_Changed"
          },
          "props": {
            "label": "Temp SP",
            "pin": "Temp_SP",
            "unit": " C",
            "min": 0,
            "max": 100,
            "step": 1,
            "decimals": 0,
            "color": "#F97316"
          }
        },
        {
          "id": "valve",
          "type": "led",
          "x": 670,
          "y": 120,
          "w": 92,
          "h": 88,
          "props": {
            "label": "Valve",
            "pin": "Valve_Open",
            "color": "#22C55E"
          }
        },
        {
          "id": "batch",
          "type": "readout",
          "x": 670,
          "y": 245,
          "w": 92,
          "h": 94,
          "props": {
            "label": "Batch",
            "pin": "Batch_Count",
            "unit": "",
            "min": 0,
            "max": 9999,
            "decimals": 0,
            "color": "#FACC15"
          }
        },
        {
          "id": "main_btn",
          "type": "button",
          "x": 36,
          "y": 392,
          "w": 150,
          "h": 58,
          "events": {
            "onClick": "MainPage_Click"
          },
          "props": {
            "label": "MAIN",
            "buttonMode": "script",
            "color": "#0EA5E9"
          }
        },
        {
          "id": "diag_btn",
          "type": "button",
          "x": 206,
          "y": 392,
          "w": 150,
          "h": 58,
          "events": {
            "onClick": "DiagnosticsPage_Click"
          },
          "props": {
            "label": "DIAG",
            "buttonMode": "script",
            "color": "#64748B"
          }
        }
      ]
    },
    {
      "id": "Diagnostics",
      "name": "Diagnostics",
      "widgets": [
        {
          "id": "diag_title",
          "type": "label",
          "x": 24,
          "y": 18,
          "w": 420,
          "h": 42,
          "props": {
            "label": "Diagnostics / Runtime",
            "color": "#FFFFFF",
            "fontSize": 28
          }
        },
        {
          "id": "diag_status",
          "type": "label",
          "x": 24,
          "y": 68,
          "w": 610,
          "h": 34,
          "props": {
            "label": "Status",
            "pin": "Script_Status",
            "color": "#FACC15",
            "fontSize": 19
          }
        },
        {
          "id": "heartbeat_diag",
          "type": "led",
          "x": 688,
          "y": 20,
          "w": 72,
          "h": 76,
          "props": {
            "label": "HB",
            "pin": "Panel_Heartbeat",
            "color": "#22C55E"
          }
        },
        {
          "id": "uptime_diag",
          "type": "readout",
          "x": 35,
          "y": 128,
          "w": 150,
          "h": 90,
          "props": {
            "label": "Uptime",
            "pin": "Panel_Uptime_s",
            "unit": " s",
            "decimals": 0,
            "min": 0,
            "max": 1000000,
            "color": "#CBD5E1"
          }
        },
        {
          "id": "tag_count_label",
          "type": "label",
          "x": 220,
          "y": 130,
          "w": 270,
          "h": 74,
          "props": {
            "label": "Tags, layout, and scripts can be saved to RAM or Flash from the browser.",
            "color": "#CBD5E1",
            "fontSize": 17
          }
        },
        {
          "id": "run_diag",
          "type": "led",
          "x": 520,
          "y": 128,
          "w": 102,
          "h": 90,
          "props": {
            "label": "Run",
            "pin": "Motor_Running",
            "color": "#22C55E"
          }
        },
        {
          "id": "fault_diag",
          "type": "led",
          "x": 650,
          "y": 128,
          "w": 102,
          "h": 90,
          "props": {
            "label": "Fault",
            "pin": "Fault_Active",
            "color": "#F43F5E"
          }
        },
        {
          "id": "memory_note",
          "type": "label",
          "x": 38,
          "y": 250,
          "w": 690,
          "h": 74,
          "props": {
            "label": "Open http://192.168.4.1 after joining WiFi SSID PiLab-Panel. Use Load from LCD to pull the running demo into the browser editor.",
            "color": "#94A3B8",
            "fontSize": 18
          }
        },
        {
          "id": "main_btn2",
          "type": "button",
          "x": 36,
          "y": 374,
          "w": 150,
          "h": 58,
          "events": {
            "onClick": "MainPage_Click"
          },
          "props": {
            "label": "MAIN",
            "buttonMode": "script",
            "color": "#0EA5E9"
          }
        },
        {
          "id": "process_btn2",
          "type": "button",
          "x": 206,
          "y": 374,
          "w": 150,
          "h": 58,
          "events": {
            "onClick": "ProcessPage_Click"
          },
          "props": {
            "label": "PROCESS",
            "buttonMode": "script",
            "color": "#16A34A"
          }
        }
      ]
    }
  ]
};

const DEFAULT_TAGS = [
  {
    "name": "Factory_Title",
    "type": "string",
    "initialValue": "PiLab Panel S3 Factory Demo",
    "writable": true,
    "description": "Main factory demo title"
  },
  {
    "name": "Script_Status",
    "type": "string",
    "initialValue": "Factory demo booting...",
    "writable": true,
    "description": "Text status written by AngelScript"
  },
  {
    "name": "Run_Command",
    "type": "bool",
    "initialValue": false,
    "writable": true,
    "description": "Start/stop command"
  },
  {
    "name": "Motor_Running",
    "type": "bool",
    "initialValue": false,
    "writable": true,
    "description": "Motor running indicator"
  },
  {
    "name": "Pump_Enable",
    "type": "bool",
    "initialValue": false,
    "writable": true,
    "description": "Pump output indicator"
  },
  {
    "name": "Valve_Open",
    "type": "bool",
    "initialValue": false,
    "writable": true,
    "description": "Valve output indicator"
  },
  {
    "name": "Fault_Active",
    "type": "bool",
    "initialValue": false,
    "writable": true,
    "description": "Demo alarm indicator"
  },
  {
    "name": "Auto_Mode",
    "type": "bool",
    "initialValue": true,
    "writable": true,
    "description": "Auto/manual toggle"
  },
  {
    "name": "Panel_Heartbeat",
    "type": "bool",
    "initialValue": false,
    "writable": false,
    "description": "Firmware heartbeat"
  },
  {
    "name": "Panel_Uptime_s",
    "type": "float",
    "initialValue": 0,
    "units": "s",
    "min": 0,
    "max": 1000000,
    "writable": false,
    "description": "Firmware uptime seconds"
  },
  {
    "name": "Motor_Speed_SP",
    "type": "float",
    "initialValue": 1200,
    "units": "rpm",
    "min": 0,
    "max": 5000,
    "writable": true,
    "description": "Motor speed setpoint"
  },
  {
    "name": "Motor_Speed_PV",
    "type": "float",
    "initialValue": 0,
    "units": "rpm",
    "min": 0,
    "max": 5000,
    "writable": true,
    "description": "Motor speed process value"
  },
  {
    "name": "Tank_Level_SP",
    "type": "float",
    "initialValue": 55,
    "units": "%",
    "min": 0,
    "max": 100,
    "writable": true,
    "description": "Tank level setpoint"
  },
  {
    "name": "Tank_Level",
    "type": "float",
    "initialValue": 45,
    "units": "%",
    "min": 0,
    "max": 100,
    "writable": true,
    "description": "Tank level process value"
  },
  {
    "name": "Temp_SP",
    "type": "float",
    "initialValue": 32,
    "units": "C",
    "min": 0,
    "max": 100,
    "writable": true,
    "description": "Temperature setpoint"
  },
  {
    "name": "Temp_PV",
    "type": "float",
    "initialValue": 24,
    "units": "C",
    "min": 0,
    "max": 100,
    "writable": true,
    "description": "Temperature process value"
  },
  {
    "name": "Flow_PV",
    "type": "float",
    "initialValue": 0,
    "units": "L/min",
    "min": 0,
    "max": 100,
    "writable": true,
    "description": "Flow process value"
  },
  {
    "name": "Pressure_PV",
    "type": "float",
    "initialValue": 20,
    "units": "kPa",
    "min": 0,
    "max": 500,
    "writable": true,
    "description": "Pressure process value"
  },
  {
    "name": "Batch_Count",
    "type": "float",
    "initialValue": 1,
    "units": "",
    "min": 0,
    "max": 9999,
    "writable": true,
    "description": "Demo batch counter"
  }
];

const DEFAULT_SCRIPT = `//PiLab Panel - Angelscript Editor

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
`;

// Dev-mode mock data intentionally lives in JS RAM only. Do not use browser
// localStorage as a hidden source of truth; the real product source is the panel.
let mockFlashLayout = structuredClone(DEFAULT_LAYOUT);
let mockRuntimeLayout = structuredClone(DEFAULT_LAYOUT);
let mockTags = structuredClone(DEFAULT_TAGS);
let mockFlashScript = DEFAULT_SCRIPT;
let mockRuntimeScript = DEFAULT_SCRIPT;

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function mockResponse(path, options = {}) {
  const method = String(options.method || 'GET').toUpperCase();
  if (path === '/api/hmi/layout' && method === 'GET') return clone(mockFlashLayout);
  if (path === '/api/hmi/runtime' && method === 'GET') return clone(mockRuntimeLayout || mockFlashLayout);
  if (path === '/api/hmi/layout' && method === 'POST') { mockFlashLayout = JSON.parse(options.body || '{}'); return { ok:true, mock:true, saved:true, applied:false }; }
  if (path === '/api/hmi/apply' && method === 'POST') { mockRuntimeLayout = JSON.parse(options.body || '{}'); return { ok:true, mock:true, queued:true, saved:false }; }
  if (path === '/api/hmi/reload') { mockRuntimeLayout = clone(mockRuntimeLayout || mockFlashLayout); return { ok:true, mock:true }; }
  if (path === '/api/tags' && method === 'GET') return { tags: clone(mockTags), count: mockTags.length, generation: 1, source: 'mock lcd runtime', ok: true };
  if (path === '/api/tags' && method === 'POST') { const body = JSON.parse(options.body || '{}'); const incoming = Array.isArray(body?.tags) ? body.tags : []; mockTags = clone(incoming); return { ok:true, mock:true, saved:false, applied:true, target:'ram', mode:'replace', count: mockTags.length }; }
  if (path === '/api/tags/save' && method === 'POST') { const body = JSON.parse(options.body || '{}'); const incoming = Array.isArray(body?.tags) ? body.tags : []; mockTags = clone(incoming); return { ok:true, mock:true, saved:true, applied:true, target:'flash', mode:'replace', count: mockTags.length }; }
  if (path === '/api/tags/write' && method === 'POST') {
    const body = JSON.parse(options.body || '{}');
    const t = mockTags.find(x => x.name === body.name);
    if (t) t.value = body.value; else mockTags.push({ name: body.name, type: typeof body.value, value: body.value, writable:true });
    return { ok:true, mock:true };
  }
  if (path === '/api/script' && method === 'GET') return { script: mockFlashScript, status:'Local browser mock. Not running on device.', active:'mock.as', source:'mock flash', ok:true };
  if (path === '/api/script/runtime' && method === 'GET') return { script: mockRuntimeScript || mockFlashScript, status:'Local browser mock. Not running on device.', active:'mock.as', source:'mock runtime RAM', ok:true };
  if (path === '/api/script' && method === 'POST') { const body = JSON.parse(options.body || '{}'); mockFlashScript = body.script || ''; return { ok:true, mock:true, saved:true }; }
  if (path === '/api/script/compile') { const body = JSON.parse(options.body || '{}'); mockRuntimeScript = body.script || ''; return { ok:true, status:'Compile queued locally (mock). Flash/connect to panel for real compile.', saved:false }; }
  if (path === '/api/script/call') return { ok:true, status:'Call queued locally (mock).' };
  if (path === '/api/script/status') return { state:'MOCK', message:'Local browser preview. The real AngelScript engine runs on the panel.', active:'mock.as', free_internal:0, free_psram:0 };
  if (path === '/api/device/status') return { name:'PiLab Panel', mode:'Local browser preview', connected:false, note:'These values are mock data. Connect to the ESP32 panel to see real memory and runtime diagnostics.' };
  if (path === '/api/wifi/status') return { started:true, mode:'ap', ap_ssid:'PiLab-Panel', ap_password:'pilabpanel', ap_ip:'192.168.4.1', hostname:'pilab-panel', mdns_url:'http://pilab-panel.local', sta_saved:false, sta_connected:false, sta_connecting:false, sta_ssid:'', sta_ip:'' };
  if (path === '/api/wifi/scan') return { ok:true, count:3, networks:[{ssid:'ShopWiFi', rssi:-43, channel:6, auth:'wpa2', secure:true},{ssid:'Office', rssi:-61, channel:11, auth:'wpa2/wpa3', secure:true},{ssid:'Guest', rssi:-72, channel:1, auth:'open', secure:false}] };
  if (path === '/api/wifi/connect' && method === 'POST') return { ok:true, mock:true, saved:true, connecting:true };
  if (path === '/api/wifi/hostname' && method === 'POST') return { ok:true, mock:true, saved:true };
  if (path === '/api/wifi/forget' && method === 'POST') return { ok:true, mock:true, forgot:true, mode:'ap' };
  if (path === '/api/mqtt/config' && method === 'GET') return { version:1, enabled:false, activeServer:0, servers:[{name:'Local Broker',enabled:true,autoConnect:true,host:'192.168.1.50',port:1883,useTls:false,username:'',password:'',clientId:'pilab-panel',baseTopic:'pilab/panel/pilab-panel'},{name:'Broker 2',enabled:false,autoConnect:false,host:'',port:1883,useTls:false,username:'',password:'',clientId:'pilab-panel',baseTopic:'pilab/panel/pilab-panel'},{name:'Broker 3',enabled:false,autoConnect:false,host:'',port:1883,useTls:false,username:'',password:'',clientId:'pilab-panel',baseTopic:'pilab/panel/pilab-panel'}], publish:[{enabled:true,server:0,tag:'Panel_Heartbeat',topic:'tag/Panel_Heartbeat/state',rateMs:1000,qos:0,retain:false},{enabled:true,server:0,tag:'Panel_Uptime_s',topic:'tag/Panel_Uptime_s/state',rateMs:1000,qos:0,retain:false}], subscribe:[{enabled:true,server:0,tag:'Run_Command',topic:'tag/Run_Command/set',qos:0}] };
  if (path === '/api/mqtt/config' && method === 'POST') return { ok:true, mock:true, saved:true };
  if (path === '/api/mqtt/status' && method === 'GET') return { ok:true, enabled:false, started:false, connected:false, activeServer:0, broker:'mock broker', port:1883, baseTopic:'pilab/panel/pilab-panel', publishCount:2, subscribeCount:1, txCount:0, rxCount:0, errorCount:0, lastError:'browser mock - not connected to device', lastRxTopic:'', lastRxPayload:'' };
  if (path === '/api/mqtt/start' && method === 'POST') return { ok:true, mock:true, started:true };
  if (path === '/api/mqtt/stop' && method === 'POST') return { ok:true, mock:true, started:false };
  if (path === '/api/mqtt/test_publish' && method === 'POST') return { ok:true, mock:true, published:true };
  throw new Error(`No mock handler for ${method} ${path}`);
}

export async function api(path, options = {}) {
  try {
    const response = await fetch(path, options);
    if (!response.ok) throw new Error(await response.text());
    const type = response.headers.get('content-type') || '';
    if (type.includes('text/html') && path.startsWith('/api/')) throw new Error('Vite fallback HTML received for API path');
    return type.includes('json') ? response.json() : response.text();
  } catch (e) {
    if (isDev && path.startsWith('/api/')) return mockResponse(path, options);
    throw e;
  }
}

export function postJson(path, body) {
  return api(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
}

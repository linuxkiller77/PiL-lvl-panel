#include "hmi_script_engine.h"

#include <string>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include <angelscript.h>
#include <scriptstdstring.h>
#include <scriptmath.h>

#include "hmi/hmi_tag_db.h"
#include "hmi/hmi_lvgl_runtime.h"
#include "hmi/hmi_diag_log.h"
#include "hmi/hmi_memory_diag.h"

static const char *TAG = "hmi_script";
static asIScriptEngine *s_engine = nullptr;
static asIScriptModule *s_module = nullptr;
static SemaphoreHandle_t s_mutex = nullptr;
static QueueHandle_t s_event_queue = nullptr;
static TaskHandle_t s_event_task = nullptr;
static QueueHandle_t s_compile_queue = nullptr;
static TaskHandle_t s_compile_worker_task = nullptr;
static volatile bool s_compile_task_active = false;
static uint32_t s_engine_create_count = 0;
static uint32_t s_compile_ok_count = 0;
static uint32_t s_compile_fail_count = 0;
static uint32_t s_old_module_discard_count = 0;

typedef struct {
    char *script;
    bool show_boot_status;
    bool run_start_inside_task;
    SemaphoreHandle_t done;
    esp_err_t result;
} script_init_request_t;

typedef struct {
    char function[64];
    bool panel_tick;
} script_event_t;
static bool s_panel_tick_pending = false;
static bool s_panel_tick_missing = false;
static bool s_panel_tick_missing_logged = false;
static uint32_t s_panel_tick_posted_count = 0;
static uint32_t s_panel_tick_skipped_count = 0;
static uint32_t s_panel_tick_executed_count = 0;
static uint32_t s_panel_tick_missing_count = 0;
static uint32_t s_panel_tick_queue_full_count = 0;
static char s_last_error[512] = "not initialized";
static char s_compile_state[32] = "not_initialized";
static bool s_runtime_valid = false;
static char s_active_script_name[64] = "embedded_demo";
static char s_active_module_name[32] = "";
static char *s_runtime_script_text = nullptr;
static uint32_t s_compile_generation = 0;
#define HMI_SCRIPT_PATH "/spiffs/hmi_script.as"
#define HMI_MAX_SCRIPT_BYTES (96 * 1024)

// AngelScript can allocate a lot while compiling. The PLC firmware solved this by
// installing AngelScript global memory functions that prefer PSRAM and fall back
// to internal RAM. Do the same here, but only when the script engine task starts,
// after LCD/I2C/LVGL are already initialized.
static bool s_as_memory_functions_installed = false;

static void *as_psram_alloc(size_t size)
{
    if (size == 0) size = 1;
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return p;
}

static void as_psram_free(void *ptr)
{
    if (ptr) heap_caps_free(ptr);
}

static void install_angelscript_psram_allocator_once(void)
{
    if (!s_as_memory_functions_installed) {
        asSetGlobalMemoryFunctions(as_psram_alloc, as_psram_free);
        s_as_memory_functions_installed = true;
#if HMI_LOG_BOOT
        ESP_LOGI(TAG, "AngelScript allocator installed: PSRAM preferred");
#endif
    }
}

static void log_memory(const char *where)
{
#if HMI_LOG_MEMORY_DETAIL
    ESP_LOGI(TAG, "%s: internal=%u psram=%u largest_internal=%u largest_psram=%u",
             where,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    (void)where;
#endif
}

static void set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_last_error, sizeof(s_last_error), fmt, ap);
    va_end(ap);
}

static void set_compile_state(const char *state)
{
    snprintf(s_compile_state, sizeof(s_compile_state), "%s", state ? state : "unknown");
}

static void set_compile_error(const char *fmt, ...)
{
    set_compile_state("error");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_last_error, sizeof(s_last_error), fmt, ap);
    va_end(ap);
}

static void message_callback(const asSMessageInfo *msg, void *)
{
    const char *type = "ERR";
    if (msg->type == asMSGTYPE_WARNING) type = "WARN";
    else if (msg->type == asMSGTYPE_INFORMATION) type = "INFO";
    ESP_LOGW(TAG, "%s (%d,%d) %s: %s", msg->section, msg->row, msg->col, type, msg->message);
    if (msg->type == asMSGTYPE_ERROR) {
        set_error("%s (%d,%d): %s", msg->section, msg->row, msg->col, msg->message);
    }
}

static const std::string *arg_string(asIScriptGeneric *gen, asUINT index)
{
    return static_cast<const std::string *>(gen->GetArgObject(index));
}

static void as_log_info(asIScriptGeneric *gen)
{
    const std::string *s = arg_string(gen, 0);
    ESP_LOGI("AS_HMI", "%s", s ? s->c_str() : "");
}

static void as_tag_read_bool(asIScriptGeneric *gen)
{
    const std::string *name = arg_string(gen, 0);
    bool fallback = gen->GetArgByte(1) != 0;
    bool v = name ? hmi_tag_db_get_bool(name->c_str(), fallback) : fallback;
    gen->SetReturnByte(v ? 1 : 0);
}

static void as_tag_write_bool(asIScriptGeneric *gen)
{
    const std::string *name = arg_string(gen, 0);
    bool value = gen->GetArgByte(1) != 0;
    if (name) hmi_tag_db_set_bool(name->c_str(), value);
}

static void as_tag_read_float(asIScriptGeneric *gen)
{
    const std::string *name = arg_string(gen, 0);
    float fallback = gen->GetArgFloat(1);
    float v = name ? hmi_tag_db_get_number(name->c_str(), fallback) : fallback;
    gen->SetReturnFloat(v);
}

static void as_tag_write_float(asIScriptGeneric *gen)
{
    const std::string *name = arg_string(gen, 0);
    float value = gen->GetArgFloat(1);
    if (name) hmi_tag_db_set_number(name->c_str(), value);
}

static void as_tag_read_string(asIScriptGeneric *gen)
{
    const std::string *name = arg_string(gen, 0);
    const std::string *fallback = arg_string(gen, 1);
    const char *v = name ? hmi_tag_db_get_string(name->c_str(), fallback ? fallback->c_str() : "") : (fallback ? fallback->c_str() : "");
    new (gen->GetAddressOfReturnLocation()) std::string(v ? v : "");
}

static void as_tag_write_string(asIScriptGeneric *gen)
{
    const std::string *name = arg_string(gen, 0);
    const std::string *value = arg_string(gen, 1);
    if (name) hmi_tag_db_set_string(name->c_str(), value ? value->c_str() : "");
}

static void as_screen_show(asIScriptGeneric *gen)
{
    const std::string *name = arg_string(gen, 0);
    if (name) hmi_lvgl_runtime_show_page(name->c_str());
}

static int register_api(asIScriptEngine *engine)
{
    int r = 0;
    RegisterStdString(engine);
    RegisterScriptMath(engine);

    r = engine->RegisterGlobalFunction("void LogInfo(const string &in)", asFUNCTION(as_log_info), asCALL_GENERIC); if (r < 0) return r;
    r = engine->RegisterGlobalFunction("bool TagReadBool(const string &in, bool)", asFUNCTION(as_tag_read_bool), asCALL_GENERIC); if (r < 0) return r;
    r = engine->RegisterGlobalFunction("void TagWriteBool(const string &in, bool)", asFUNCTION(as_tag_write_bool), asCALL_GENERIC); if (r < 0) return r;
    r = engine->RegisterGlobalFunction("float TagReadFloat(const string &in, float)", asFUNCTION(as_tag_read_float), asCALL_GENERIC); if (r < 0) return r;
    r = engine->RegisterGlobalFunction("void TagWriteFloat(const string &in, float)", asFUNCTION(as_tag_write_float), asCALL_GENERIC); if (r < 0) return r;
    r = engine->RegisterGlobalFunction("string TagReadString(const string &in, const string &in)", asFUNCTION(as_tag_read_string), asCALL_GENERIC); if (r < 0) return r;
    r = engine->RegisterGlobalFunction("void TagWriteString(const string &in, const string &in)", asFUNCTION(as_tag_write_string), asCALL_GENERIC); if (r < 0) return r;
    r = engine->RegisterGlobalFunction("void ScreenShow(const string &in)", asFUNCTION(as_screen_show), asCALL_GENERIC); if (r < 0) return r;
    return 0;
}

static const char *demo_script = R"AS(
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

)AS";

const char *hmi_script_engine_default_script(void)
{
    return demo_script;
}

static char *alloc_text_psram(size_t len)
{
    char *p = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p) p[len] = 0;
    return p;
}

static char *dup_text_psram(const char *text)
{
    if (!text) return nullptr;
    size_t len = strlen(text);
    char *copy = alloc_text_psram(len);
    if (copy) memcpy(copy, text, len);
    return copy;
}

static void remember_runtime_script_locked(const char *script_text)
{
    char *copy = dup_text_psram(script_text);
    if (!copy) {
        ESP_LOGW(TAG, "failed to remember runtime script source");
        return;
    }
    heap_caps_free(s_runtime_script_text);
    s_runtime_script_text = copy;
}

esp_err_t hmi_script_engine_load_script(char **out_script)
{
    if (!out_script) return ESP_ERR_INVALID_ARG;
    *out_script = NULL;
    FILE *f = fopen(HMI_SCRIPT_PATH, "rb");
    if (!f) {
        size_t len = strlen(demo_script);
        char *copy = alloc_text_psram(len);
        if (!copy) return ESP_ERR_NO_MEM;
        memcpy(copy, demo_script, len);
        *out_script = copy;
        return ESP_OK;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len <= 0 || len > HMI_MAX_SCRIPT_BYTES) {
        fclose(f);
        ESP_LOGW(TAG, "stored script is empty or too large (%ld); using embedded demo script", len);
        size_t dlen = strlen(demo_script);
        char *copy = alloc_text_psram(dlen);
        if (!copy) return ESP_ERR_NO_MEM;
        memcpy(copy, demo_script, dlen);
        snprintf(s_active_script_name, sizeof(s_active_script_name), "embedded_demo");
        *out_script = copy;
        return ESP_OK;
    }
    char *buf = alloc_text_psram((size_t)len);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (n != (size_t)len) {
        heap_caps_free(buf);
        ESP_LOGW(TAG, "stored script read failed; using embedded demo script");
        size_t dlen = strlen(demo_script);
        char *copy = alloc_text_psram(dlen);
        if (!copy) return ESP_ERR_NO_MEM;
        memcpy(copy, demo_script, dlen);
        snprintf(s_active_script_name, sizeof(s_active_script_name), "embedded_demo");
        *out_script = copy;
        return ESP_OK;
    }
    snprintf(s_active_script_name, sizeof(s_active_script_name), "hmi_script.as");
    *out_script = buf;
    return ESP_OK;
}


static asIScriptEngine *ensure_script_engine_created(void)
{
    install_angelscript_psram_allocator_once();
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return nullptr;

    if (s_engine) {
#if HMI_LOG_TIMING_DETAIL
        ESP_LOGI(TAG, "AngelScript engine already exists; reusing engine=%p module=%p active=%s generation=%lu module_count=%lu",
                 (void *)s_engine, (void *)s_module, s_active_module_name,
                 (unsigned long)s_compile_generation,
                 (unsigned long)s_engine->GetModuleCount());
#endif
        return s_engine;
    }

    s_engine = asCreateScriptEngine();
    if (!s_engine) {
        set_error("asCreateScriptEngine failed");
        return nullptr;
    }
    s_engine_create_count++;
    s_engine->SetMessageCallback(asFUNCTION(message_callback), nullptr, asCALL_CDECL);
    int r = register_api(s_engine);
    if (r < 0) {
        set_error("register_api failed: %d", r);
        s_engine->ShutDownAndRelease();
        s_engine = nullptr;
        return nullptr;
    }
#if HMI_LOG_SUMMARY
    ESP_LOGI(TAG, "AngelScript engine created: engine=%p create_count=%lu",
             (void *)s_engine, (unsigned long)s_engine_create_count);
#endif
    return s_engine;
}

static void log_script_lifetime_locked(const char *where)
{
#if HMI_LOG_SCRIPT_RUNTIME_DETAIL
    ESP_LOGI(TAG,
             "AngelScript runtime: %s engine=%p module=%p active=%s modules=%lu generation=%lu ok=%lu fail=%lu discarded=%lu compile_task=%d worker=%p event_task=%p queue=%p state=%s valid=%d",
             where ? where : "?",
             (void *)s_engine,
             (void *)s_module,
             s_active_module_name[0] ? s_active_module_name : "<none>",
             (unsigned long)(s_engine ? s_engine->GetModuleCount() : 0),
             (unsigned long)s_compile_generation,
             (unsigned long)s_compile_ok_count,
             (unsigned long)s_compile_fail_count,
             (unsigned long)s_old_module_discard_count,
             s_compile_task_active ? 1 : 0,
             (void *)s_compile_worker_task,
             (void *)s_event_task,
             (void *)s_event_queue,
             s_compile_state,
             s_runtime_valid ? 1 : 0);
#else
    (void)where;
#endif
}

static bool validate_script_lifetime_locked(const char *where)
{
    bool ok = true;
    uint32_t module_count = s_engine ? (uint32_t)s_engine->GetModuleCount() : 0;

    if (s_engine_create_count > 1) {
        ESP_LOGW(TAG, "AngelScript lifetime warning: %s engine_create_count=%lu expected=1",
                 where ? where : "?", (unsigned long)s_engine_create_count);
        ok = false;
    }
    if (s_engine && s_runtime_valid && module_count != 1) {
        ESP_LOGW(TAG, "AngelScript lifetime warning: %s module_count=%lu expected=1 active=%s module=%p",
                 where ? where : "?", (unsigned long)module_count,
                 s_active_module_name[0] ? s_active_module_name : "<none>", (void *)s_module);
        ok = false;
    }
    if (s_runtime_valid && (!s_engine || !s_module)) {
        ESP_LOGW(TAG, "AngelScript lifetime warning: %s runtime_valid=1 but engine/module missing engine=%p module=%p",
                 where ? where : "?", (void *)s_engine, (void *)s_module);
        ok = false;
    }
    if (!s_runtime_valid && s_module) {
        ESP_LOGW(TAG, "AngelScript lifetime warning: %s module exists while runtime_valid=0 module=%p",
                 where ? where : "?", (void *)s_module);
        ok = false;
    }
    return ok;
}

static void validate_script_lifetime(const char *where)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool ok = validate_script_lifetime_locked(where);
#if HMI_LOG_SCRIPT_LIFETIME_OK
    if (ok) {
        ESP_LOGI(TAG, "AngelScript lifetime OK: %s engines=%lu modules=%lu generation=%lu discarded=%lu",
                 where ? where : "?",
                 (unsigned long)s_engine_create_count,
                 (unsigned long)(s_engine ? s_engine->GetModuleCount() : 0),
                 (unsigned long)s_compile_generation,
                 (unsigned long)s_old_module_discard_count);
    }
#else
    (void)ok;
#endif
    if (s_mutex) xSemaphoreGive(s_mutex);
}

static uint32_t free_internal_8bit(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static uint32_t free_psram_8bit(void)
{
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void warn_if_compile_memory_did_not_recover(const char *where, uint32_t before_internal, uint32_t before_psram)
{
    uint32_t after_internal = free_internal_8bit();
    uint32_t after_psram = free_psram_8bit();
    int32_t delta_internal = (int32_t)after_internal - (int32_t)before_internal;
    int32_t delta_psram = (int32_t)after_psram - (int32_t)before_psram;

#if HMI_LOG_SCRIPT_MEMORY_RECOVERY
    ESP_LOGI(TAG, "AngelScript compile memory recovery: %s internal_before=%lu internal_after=%lu delta=%ld psram_before=%lu psram_after=%lu delta=%ld",
             where ? where : "?",
             (unsigned long)before_internal, (unsigned long)after_internal, (long)delta_internal,
             (unsigned long)before_psram, (unsigned long)after_psram, (long)delta_psram);
#endif

    // A replacement compile keeps the newly compiled module, so exact recovery is
    // not required. These thresholds are meant to catch runaway loss over repeated
    // Compile-to-RAM operations, not normal module-size differences.
    if (delta_internal < -8192 || delta_psram < -131072) {
        ESP_LOGW(TAG, "AngelScript compile memory warning: %s recovered poorly internal_delta=%ld psram_delta=%ld",
                 where ? where : "?", (long)delta_internal, (long)delta_psram);
    }
}

esp_err_t hmi_script_engine_get_runtime_script(char **out_script, const char **out_source)
{
    if (!out_script) return ESP_ERR_INVALID_ARG;
    *out_script = nullptr;
    if (out_source) *out_source = "unknown";

    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    const char *src = (s_runtime_valid && s_runtime_script_text) ? s_runtime_script_text : nullptr;
    char *copy = src ? dup_text_psram(src) : nullptr;
    if (s_mutex) xSemaphoreGive(s_mutex);

    if (copy) {
        *out_script = copy;
        if (out_source) *out_source = "runtime RAM";
        return ESP_OK;
    }

    esp_err_t err = hmi_script_engine_load_script(out_script);
    if (err == ESP_OK && out_source) *out_source = "device flash fallback";
    return err;
}

esp_err_t hmi_script_engine_save_script(const char *script_text)
{
    if (!script_text) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(HMI_SCRIPT_PATH, "wb");
    if (!f) return ESP_FAIL;
    size_t len = strlen(script_text);
    size_t n = fwrite(script_text, 1, len, f);
    fclose(f);
    if (n != len) return ESP_FAIL;
    snprintf(s_active_script_name, sizeof(s_active_script_name), "hmi_script.as");
    return ESP_OK;
}

esp_err_t hmi_script_engine_call_on_panel_start(void)
{
    return hmi_script_engine_post_call("OnPanelStart");
}

esp_err_t hmi_script_engine_compile(const char *script_text)
{
    if (!script_text) {
        set_compile_error("compile failed: invalid script argument");
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_script_engine_created()) {
        set_compile_error("compile failed: script engine could not be created");
        return ESP_ERR_NO_MEM;
    }

    int64_t compile_start_us = esp_timer_get_time();
    size_t script_len = strlen(script_text);
    hmi_memory_diag_checkpoint("script: compile begin");
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    set_compile_state("compiling");
    snprintf(s_last_error, sizeof(s_last_error), "compiling");

    // IMPORTANT SAFETY RULE:
    // Never build into the active module name with asGM_ALWAYS_CREATE. That can
    // invalidate the currently running/old module before we know the new script
    // actually compiles. If build then fails, LVGL button callbacks may later
    // queue events against a dangling module pointer and crash/reboot the panel.
    // Build into a fresh candidate module first. Only swap s_module after Build
    // succeeds. A failed compile leaves the previous runtime module alive.
    char candidate_name[32];
    snprintf(candidate_name, sizeof(candidate_name), "hmi_%lu", (unsigned long)(++s_compile_generation));

    asIScriptModule *module = s_engine->GetModule(candidate_name, asGM_ALWAYS_CREATE);
    if (!module) {
        s_compile_fail_count++;
        set_compile_error("GetModule failed");
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    int r = module->AddScriptSection("hmi_script", script_text, (unsigned int)strlen(script_text));
    if (r < 0) {
        s_compile_fail_count++;
        s_engine->DiscardModule(candidate_name);
        validate_script_lifetime_locked("AddScriptSection failed after candidate discard");
        set_compile_error("AddScriptSection failed: %d", r);
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }

    r = module->Build();
    if (r < 0) {
        // Keep the previous compiled runtime active. Discard only the failed
        // candidate module.
        s_compile_fail_count++;
        ESP_LOGE(TAG, "AngelScript compile FAILED: bytes=%u time=%lld ms error=%s",
                 (unsigned)script_len, (long long)((esp_timer_get_time() - compile_start_us) / 1000), s_last_error);
        s_engine->DiscardModule(candidate_name);
        validate_script_lifetime_locked("Build failed after candidate discard");
        set_compile_state("error");
        if (!s_last_error[0] || strcmp(s_last_error, "compiling") == 0) {
            snprintf(s_last_error, sizeof(s_last_error), "AngelScript build failed: %d", r);
        }
        xSemaphoreGive(s_mutex);
        hmi_memory_diag_checkpoint("script: compile FAILED");
        return ESP_FAIL;
    }

    char old_module_name[32];
    snprintf(old_module_name, sizeof(old_module_name), "%s", s_active_module_name);

    s_module = module;
    snprintf(s_active_module_name, sizeof(s_active_module_name), "%s", candidate_name);
    s_runtime_valid = true;
    set_compile_state("ok");
    snprintf(s_last_error, sizeof(s_last_error), "ok");
    remember_runtime_script_locked(script_text);

    if (old_module_name[0] && strcmp(old_module_name, s_active_module_name) != 0) {
        int discard_r = s_engine->DiscardModule(old_module_name);
        s_old_module_discard_count++;
#if HMI_LOG_SCRIPT_MODULE_DISCARD
        ESP_LOGI(TAG, "AngelScript old module discarded: old=%s result=%d remaining_modules=%lu", old_module_name, discard_r, (unsigned long)s_engine->GetModuleCount());
#else
        (void)discard_r;
#endif
        hmi_memory_diag_checkpoint("script: old module discarded");
    }
    validate_script_lifetime_locked("compile OK after swap/discard");
    s_compile_ok_count++;

#if HMI_LOG_SUMMARY
    ESP_LOGI(TAG, "AngelScript compile OK: module=%s bytes=%u time=%lld ms generation=%lu modules=%lu engine_creates=%lu",
             s_active_module_name, (unsigned)script_len,
             (long long)((esp_timer_get_time() - compile_start_us) / 1000),
             (unsigned long)s_compile_generation,
             (unsigned long)(s_engine ? s_engine->GetModuleCount() : 0),
             (unsigned long)s_engine_create_count);
    log_script_lifetime_locked("compile OK");
#endif
    xSemaphoreGive(s_mutex);
    hmi_memory_diag_checkpoint("script: compile OK");
    s_panel_tick_pending = false;
    s_panel_tick_missing = false;
    s_panel_tick_missing_logged = false;
    return ESP_OK;
}

esp_err_t hmi_script_engine_call(const char *function_name)
{
    if (!function_name || !function_name[0]) return ESP_ERR_INVALID_ARG;
    if (!s_engine || !s_module || !s_runtime_valid) {
        set_error("script runtime is not valid; compile a valid script first");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    char decl[96];
    snprintf(decl, sizeof(decl), "void %s()", function_name);
    asIScriptFunction *func = s_module->GetFunctionByDecl(decl);
    if (!func) {
        set_error("script function not found: %s", decl);
        ESP_LOGW(TAG, "%s", s_last_error);
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    asIScriptContext *ctx = s_engine->CreateContext();
    if (!ctx) {
        set_error("CreateContext failed");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }
    int r = ctx->Prepare(func);
    if (r >= 0) r = ctx->Execute();
    if (r != asEXECUTION_FINISHED) {
        set_error("script call failed: %s result=%d", function_name, r);
        ESP_LOGE(TAG, "%s", s_last_error);
        ctx->Release();
        xSemaphoreGive(s_mutex);
        return ESP_FAIL;
    }
    ctx->Release();
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

static esp_err_t hmi_script_engine_init_from_text_ex(const char *script_text, bool run_start)
{
    log_memory("before AngelScript init");
    if (!ensure_script_engine_created()) {
        return s_mutex ? ESP_FAIL : ESP_ERR_NO_MEM;
    }
    log_memory("after AngelScript engine ensure/register");

    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (script_text && script_text[0]) {
        err = hmi_script_engine_compile(script_text);
    } else {
        set_error("no script text provided");
    }

    log_memory("after AngelScript compile");
    hmi_script_engine_log_stats("after init compile");
    if (err == ESP_OK && run_start) {
        hmi_script_engine_call("OnPanelStart");
        log_memory("after OnPanelStart");
    }
    return err;
}

static esp_err_t hmi_script_engine_init_from_text(const char *script_text)
{
    return hmi_script_engine_init_from_text_ex(script_text, true);
}

esp_err_t hmi_script_engine_init(void)
{
    // Safety-only synchronous initializer. It intentionally does NOT read SPIFFS.
    // Some AngelScript tasks may use a PSRAM-backed stack; ESP-IDF asserts if
    // flash/SPIFFS access disables cache while the active stack is in PSRAM.
    // Normal startup must use hmi_script_engine_start_async(), which preloads the
    // script from SPIFFS on the internal main task before creating the PSRAM-stack
    // compile task.
    return hmi_script_engine_init_from_text(demo_script);
}


typedef struct {
    char *script;
    bool save;
    bool run_start;
} script_compile_request_t;

static void process_script_compile_request(script_compile_request_t *req)
{
    if (!req || !req->script) {
        if (req) heap_caps_free(req);
        s_compile_task_active = false;
        return;
    }

    // Script compilation is heavy PSRAM/cache work. Show a static foreground
    // status overlay, let it render, then pause LVGL's timer/render path by
    // holding the esp_lvgl_port lock while the compiler runs. Do not call any
    // LVGL APIs until hmi_lvgl_runtime_blocking_status_end() resumes LVGL.
    bool paused = hmi_lvgl_runtime_blocking_status_begin(
        "Compiling script...",
        "The LCD will blank briefly while AngelScript compiles, then the HMI will reload.",
        220,
        "script compile RAM");
    if (!paused) {
        hmi_lvgl_runtime_external_activity_begin("script compile RAM fallback");
    }

    uint32_t compile_start_internal = free_internal_8bit();
    uint32_t compile_start_psram = free_psram_8bit();
    hmi_script_engine_log_stats("async compile task before compile");
    validate_script_lifetime("async compile task before compile");
    hmi_memory_diag_checkpoint("script: async compile task before compile");
    log_memory("before async AngelScript compile");
    esp_err_t err = hmi_script_engine_compile(req->script);
    log_memory("after async AngelScript compile");
    hmi_script_engine_log_stats(err == ESP_OK ? "async compile task after compile OK" : "async compile task after compile ERROR");
    validate_script_lifetime(err == ESP_OK ? "async compile task after compile OK" : "async compile task after compile ERROR");
    hmi_memory_diag_checkpoint(err == ESP_OK ? "script: async compile task after OK" : "script: async compile task after ERROR");

    char result_msg[512];
    if (err == ESP_OK) {
        snprintf(result_msg, sizeof(result_msg), "Compile OK. Script runtime was updated in RAM.");
    } else {
        snprintf(result_msg, sizeof(result_msg), "%s", s_last_error[0] ? s_last_error : "AngelScript compile failed.");
    }

    if (paused) {
        hmi_lvgl_runtime_blocking_status_end(
            err == ESP_OK ? "Script compile OK" : "Script compile error",
            result_msg,
            err != ESP_OK,
            err == ESP_OK ? 450 : 4500,
            "script compile RAM complete");
    } else {
        hmi_lvgl_runtime_external_activity_end("script compile RAM fallback complete");
    }

    // Do not write SPIFFS from this task. It may be using a PSRAM-backed stack,
    // and flash/SPIFFS temporarily disables cache. Compile requests update the
    // active runtime module only; saving is handled by POST /api/script.
    if (err == ESP_OK && req->run_start) {
        hmi_script_engine_call("OnPanelStart");
        log_memory("after async OnPanelStart");
        hmi_memory_diag_checkpoint("script: async OnPanelStart complete");
    }

    heap_caps_free(req->script);
    heap_caps_free(req);
    s_compile_task_active = false;
    hmi_memory_diag_checkpoint("script: async compile task released request buffers");
    warn_if_compile_memory_did_not_recover("async compile task complete", compile_start_internal, compile_start_psram);
    hmi_script_engine_log_stats("async compile task complete");
    validate_script_lifetime("async compile task complete");
#if HMI_LOG_TIMING_DETAIL
    ESP_LOGI(TAG, "AngelScript async compile worker complete, err=%d stack high-water=%u", (int)err, (unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif
}

static void script_compile_worker_task(void *)
{
#if HMI_LOG_BOOT
    ESP_LOGI(TAG, "AngelScript compile worker task started: stack_high_water=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif
    while (true) {
        script_compile_request_t *req = nullptr;
        if (xQueueReceive(s_compile_queue, &req, portMAX_DELAY) == pdTRUE) {
            process_script_compile_request(req);
        }
    }
}

static esp_err_t ensure_script_compile_worker(void)
{
    if (!s_compile_queue) {
        s_compile_queue = xQueueCreate(1, sizeof(script_compile_request_t *));
        if (!s_compile_queue) {
            set_compile_error("failed to create script compile queue");
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_compile_worker_task) return ESP_OK;

    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(script_compile_worker_task, "hmi_script_compile", 96 * 1024, nullptr, 2, &s_compile_worker_task, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "PSRAM script compile worker task create failed; trying 32KB internal stack");
        ok = xTaskCreatePinnedToCore(script_compile_worker_task, "hmi_script_compile", 32 * 1024, nullptr, 2, &s_compile_worker_task, 1);
    }
    if (ok != pdPASS) {
        s_compile_worker_task = nullptr;
        set_compile_error("failed to create persistent script compile worker");
        return ESP_ERR_NO_MEM;
    }
#if HMI_LOG_SUMMARY
    ESP_LOGI(TAG, "AngelScript persistent compile worker created: task=%p queue=%p", (void *)s_compile_worker_task, (void *)s_compile_queue);
    hmi_memory_diag_checkpoint("script: persistent compile worker created");
#endif
    return ESP_OK;
}

esp_err_t hmi_script_engine_compile_async(const char *script_text)
{
    if (!script_text || !script_text[0]) return ESP_ERR_INVALID_ARG;
    if (s_compile_task_active) {
        ESP_LOGW(TAG, "script compile already active; dropping duplicate Compile RAM request");
        return ESP_ERR_INVALID_STATE;
    }
    size_t len = strlen(script_text);
    if (len > HMI_MAX_SCRIPT_BYTES) return ESP_ERR_INVALID_SIZE;
    script_compile_request_t *req = (script_compile_request_t *)heap_caps_calloc(1, sizeof(script_compile_request_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!req) req = (script_compile_request_t *)heap_caps_calloc(1, sizeof(script_compile_request_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!req) return ESP_ERR_NO_MEM;
    req->script = alloc_text_psram(len);
    if (!req->script) { heap_caps_free(req); return ESP_ERR_NO_MEM; }
    memcpy(req->script, script_text, len);
    req->save = false;
    req->run_start = true;

    // Compile-to-RAM only. Saving to SPIFFS is intentionally handled by
    // POST /api/script so iterative script edits do not burn flash.
    set_compile_state("queued");
    snprintf(s_last_error, sizeof(s_last_error), "compile queued");
    hmi_script_engine_log_stats("Compile-to-RAM queued");
    hmi_memory_diag_checkpoint("script: Compile-to-RAM queued");

    esp_err_t worker_err = ensure_script_compile_worker();
    if (worker_err != ESP_OK) {
        heap_caps_free(req->script);
        heap_caps_free(req);
        return worker_err;
    }

    s_compile_task_active = true;
    if (xQueueSend(s_compile_queue, &req, 0) != pdTRUE) {
        s_compile_task_active = false;
        heap_caps_free(req->script);
        heap_caps_free(req);
        set_compile_error("script compile worker queue full");
        ESP_LOGW(TAG, "script compile worker queue full; dropping Compile RAM request");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void script_event_task(void *)
{
#if HMI_LOG_BOOT
    ESP_LOGI(TAG, "AngelScript event task started");
#endif
    script_event_t ev;
    while (true) {
        if (xQueueReceive(s_event_queue, &ev, portMAX_DELAY) == pdTRUE) {
            if (ev.function[0]) {
                esp_err_t err = hmi_script_engine_call(ev.function);
                if (ev.panel_tick) {
                    s_panel_tick_pending = false;
                    if (err == ESP_OK) {
                        ++s_panel_tick_executed_count;
                        s_panel_tick_missing = false;
                    } else if (err == ESP_ERR_NOT_FOUND) {
                        s_panel_tick_missing = true;
                        ++s_panel_tick_missing_count;
                    }
                }
                if (err != ESP_OK && !(ev.panel_tick && err == ESP_ERR_NOT_FOUND)) {
                    ESP_LOGW(TAG, "script event failed: %s err=%d last=%s", ev.function, (int)err, s_last_error);
                }
            } else if (ev.panel_tick) {
                s_panel_tick_pending = false;
            }
        }
    }
}

esp_err_t hmi_script_engine_post_call(const char *function_name)
{
    if (!function_name || !function_name[0]) return ESP_ERR_INVALID_ARG;
    if (!s_event_queue || !s_runtime_valid || !s_module) {
        set_error("script runtime is not valid; event not queued: %s", function_name);
        return ESP_ERR_INVALID_STATE;
    }

    script_event_t ev = {};
    snprintf(ev.function, sizeof(ev.function), "%s", function_name);
    BaseType_t ok = xQueueSend(s_event_queue, &ev, 0);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "script event queue full, dropped: %s", ev.function);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t hmi_script_engine_post_panel_tick(void)
{
    if (!s_event_queue || !s_runtime_valid || !s_module) {
        ++s_panel_tick_skipped_count;
        return ESP_ERR_INVALID_STATE;
    }
    if (s_panel_tick_pending) {
        ++s_panel_tick_skipped_count;
        return ESP_ERR_INVALID_STATE;
    }

    // Check for the function before queueing. If the user enabled the tick but
    // has not created OnPanelTick(), do not fill the event queue and do not log
    // on every scheduler interval. The Designer shows this state and its Edit
    // button can create the stub.
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    asIScriptFunction *func = s_module ? s_module->GetFunctionByDecl("void OnPanelTick()") : nullptr;
    if (s_mutex) xSemaphoreGive(s_mutex);
    if (!func) {
        s_panel_tick_missing = true;
        ++s_panel_tick_skipped_count;
        ++s_panel_tick_missing_count;
        if (!s_panel_tick_missing_logged) {
            s_panel_tick_missing_logged = true;
            ESP_LOGW(TAG, "OnPanelTick enabled but void OnPanelTick() was not found; ticks will be skipped until the script is updated");
        }
        return ESP_ERR_NOT_FOUND;
    }

    script_event_t ev = {};
    snprintf(ev.function, sizeof(ev.function), "%s", "OnPanelTick");
    ev.panel_tick = true;
    s_panel_tick_pending = true;
    BaseType_t ok = xQueueSend(s_event_queue, &ev, 0);
    if (ok != pdPASS) {
        s_panel_tick_pending = false;
        ++s_panel_tick_skipped_count;
        ++s_panel_tick_queue_full_count;
        ESP_LOGW(TAG, "script event queue full, dropped: OnPanelTick");
        return ESP_ERR_TIMEOUT;
    }
    s_panel_tick_missing = false;
    ++s_panel_tick_posted_count;
    return ESP_OK;
}

static void script_init_task(void *arg)
{
#if HMI_LOG_BOOT
    ESP_LOGI(TAG, "AngelScript init task started");
#endif
    script_init_request_t *req = (script_init_request_t *)arg;
    char *script = req ? req->script : NULL;
    bool show_boot_status = req ? req->show_boot_status : true;
    bool run_start_inside_task = req ? req->run_start_inside_task : true;
    bool paused = false;

    if (show_boot_status) {
        paused = hmi_lvgl_runtime_blocking_status_begin(
            "Compiling startup script...",
            "The HMI screen is loaded. LVGL is paused while AngelScript initializes.",
            220,
            "boot script compile");
        if (!paused) {
            hmi_lvgl_runtime_external_activity_begin("boot script compile fallback");
        }
    } else {
#if HMI_LOG_BOOT
        ESP_LOGI(TAG, "Boot pre-HMI AngelScript compile: no LVGL status screen shown");
#endif
    }

    esp_err_t err = hmi_script_engine_init_from_text_ex(script, false);
    if (script) heap_caps_free(script);
    if (req) req->script = NULL;

    if (show_boot_status) {
        char result_msg[512];
        if (err == ESP_OK) {
            snprintf(result_msg, sizeof(result_msg), "Startup script compiled OK.");
        } else {
            snprintf(result_msg, sizeof(result_msg), "%s", s_last_error[0] ? s_last_error : "Startup script compile failed.");
        }

        if (paused) {
            hmi_lvgl_runtime_blocking_status_end(
                err == ESP_OK ? "Startup script OK" : "Startup script error",
                result_msg,
                err != ESP_OK,
                err == ESP_OK ? 450 : 4500,
                "boot script compile complete");
        } else {
            hmi_lvgl_runtime_external_activity_end("boot script compile fallback complete");
        }
    }

    if (err == ESP_OK && run_start_inside_task) {
        hmi_script_engine_call("OnPanelStart");
        log_memory("after boot OnPanelStart");
    }
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    if (!s_event_queue) {
        s_event_queue = xQueueCreate(12, sizeof(script_event_t));
    }
    if (s_event_queue && !s_event_task) {
        BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
            script_event_task,
            "hmi_script_evt",
            32 * 1024,
            NULL,
            3,
            &s_event_task,
            1,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ok != pdPASS) {
            ESP_LOGW(TAG, "PSRAM script event task create failed; trying 12KB internal stack");
            ok = xTaskCreatePinnedToCore(script_event_task, "hmi_script_evt", 12 * 1024, NULL, 3, &s_event_task, 1);
        }
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "failed to create hmi_script_evt task");
        }
    }
    if (err == ESP_OK) {
#if HMI_LOG_BOOT
        ESP_LOGI(TAG, "AngelScript init complete: stack_high_water=%u bytes", (unsigned)hwm);
#endif
        hmi_script_engine_log_stats("init task complete");
    } else {
        ESP_LOGE(TAG, "AngelScript init task failed: %s, stack high-water=%u bytes", s_last_error, (unsigned)hwm);
    }
    if (req) {
        req->result = err;
        if (req->done) {
            xSemaphoreGive(req->done);
        } else {
            heap_caps_free(req);
        }
    }
    vTaskDelete(NULL);
}


esp_err_t hmi_script_engine_boot_compile_pre_hmi(uint32_t timeout_ms)
{
    TaskHandle_t handle = NULL;
    hmi_memory_diag_checkpoint("script: boot pre-HMI task create begin");
    log_memory("before boot pre-HMI hmi_script_init task");

    char *script = NULL;
    esp_err_t load_err = hmi_script_engine_load_script(&script);
    if (load_err != ESP_OK || !script) {
        set_error("failed to load script before pre-HMI init task: %d", (int)load_err);
        ESP_LOGE(TAG, "%s", s_last_error);
        return load_err == ESP_OK ? ESP_FAIL : load_err;
    }

    script_init_request_t *req = (script_init_request_t *)heap_caps_calloc(1, sizeof(script_init_request_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!req) {
        heap_caps_free(script);
        return ESP_ERR_NO_MEM;
    }
    req->done = xSemaphoreCreateBinary();
    if (!req->done) {
        heap_caps_free(script);
        heap_caps_free(req);
        return ESP_ERR_NO_MEM;
    }
    req->script = script;
    req->show_boot_status = false;
    req->run_start_inside_task = false;
    req->result = ESP_ERR_INVALID_STATE;

    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        script_init_task,
        "hmi_script_init",
        96 * 1024,
        req,
        2,
        &handle,
        1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ok != pdPASS) {
        ESP_LOGW(TAG, "PSRAM pre-HMI script init task create failed; trying 32KB internal stack");
        ok = xTaskCreatePinnedToCore(script_init_task, "hmi_script_init", 32 * 1024, req, 2, &handle, 1);
    }
    if (ok != pdPASS) {
        vSemaphoreDelete(req->done);
        heap_caps_free(script);
        heap_caps_free(req);
        set_error("failed to create pre-HMI hmi_script_init task");
        return ESP_ERR_NO_MEM;
    }

    TickType_t wait_ticks = timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY;
    if (xSemaphoreTake(req->done, wait_ticks) != pdTRUE) {
        set_error("pre-HMI AngelScript compile timed out after %lu ms", (unsigned long)timeout_ms);
        ESP_LOGE(TAG, "%s", s_last_error);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = req->result;
    hmi_memory_diag_checkpoint(result == ESP_OK ? "script: boot pre-HMI task complete OK" : "script: boot pre-HMI task complete ERROR");
    vSemaphoreDelete(req->done);
    heap_caps_free(req);
    return result;
}

esp_err_t hmi_script_engine_start_async(void)
{
    TaskHandle_t handle = NULL;
    log_memory("before creating hmi_script_init task");

    // Load the script from SPIFFS before creating the PSRAM-stack task. SPIFFS
    // uses flash reads that temporarily disable cache; ESP-IDF asserts if the
    // current task stack is in PSRAM during those operations. app_main/main_task
    // has an internal stack, so this is the safe place to do the file read.
    char *script = NULL;
    esp_err_t load_err = hmi_script_engine_load_script(&script);
    if (load_err != ESP_OK || !script) {
        set_error("failed to load script before init task: %d", (int)load_err);
        ESP_LOGE(TAG, "%s", s_last_error);
        return load_err == ESP_OK ? ESP_FAIL : load_err;
    }

    // Do not start the heavy AngelScript compiler while the first LCD screen is
    // still being built/settled. The RGB panel scans from PSRAM, and the compiler
    // also creates a burst of PSRAM/cache traffic. Starting after the first HMI
    // render avoids the intermittent offset/unstable boot display symptom.
    if (!hmi_lvgl_runtime_wait_until_ready(5000)) {
        ESP_LOGW(TAG, "HMI runtime did not report ready before script init; starting script anyway");
    } else {
        vTaskDelay(pdMS_TO_TICKS(400));
    }

    script_init_request_t *req = (script_init_request_t *)heap_caps_calloc(1, sizeof(script_init_request_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!req) {
        heap_caps_free(script);
        return ESP_ERR_NO_MEM;
    }
    req->script = script;
    req->show_boot_status = true;
    req->run_start_inside_task = true;
    req->done = NULL;
    req->result = ESP_ERR_INVALID_STATE;

    // After LVGL + Wi-Fi + HTTP are running there may be less than 64 KB of
    // contiguous internal RAM left. A large internal compile stack can fail even
    // though PSRAM is mostly free. Use an ESP-IDF stack-with-caps task so the
    // temporary AngelScript compiler stack comes from PSRAM.
    const uint32_t stack_bytes = 96 * 1024;
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        script_init_task,
        "hmi_script_init",
        stack_bytes,
        req,
        2,
        &handle,
        1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (ok != pdPASS) {
        // Fallback: try a smaller internal-stack task. This may still work now
        // that AngelScript heap allocations prefer PSRAM.
        ESP_LOGW(TAG, "PSRAM stack task create failed; trying 32KB internal stack");
        ok = xTaskCreatePinnedToCore(
            script_init_task,
            "hmi_script_init",
            32 * 1024,
            req,
            2,
            &handle,
            1);
    }

    if (ok != pdPASS) {
        heap_caps_free(script);
        heap_caps_free(req);
        set_error("failed to create hmi_script_init task");
        ESP_LOGE(TAG, "%s", s_last_error);
        log_memory("after failed hmi_script_init task create");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}


void hmi_script_engine_get_stats(hmi_script_engine_stats_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    out->engine_created = s_engine != nullptr;
    out->module_active = s_module != nullptr;
    out->runtime_valid = s_runtime_valid;
    out->compile_task_active = s_compile_task_active;
    out->compile_worker_running = s_compile_worker_task != nullptr;
    out->event_queue_created = s_event_queue != nullptr;
    out->event_task_running = s_event_task != nullptr;
    out->compile_generation = s_compile_generation;
    out->engine_create_count = s_engine_create_count;
    out->module_count = s_engine ? s_engine->GetModuleCount() : 0;
    out->compile_ok_count = s_compile_ok_count;
    out->compile_fail_count = s_compile_fail_count;
    out->old_module_discard_count = s_old_module_discard_count;
    out->event_queue_waiting = s_event_queue ? (uint32_t)uxQueueMessagesWaiting(s_event_queue) : 0;
    out->event_task_stack_high_water = s_event_task ? (uint32_t)uxTaskGetStackHighWaterMark(s_event_task) : 0;
    out->compile_worker_stack_high_water = s_compile_worker_task ? (uint32_t)uxTaskGetStackHighWaterMark(s_compile_worker_task) : 0;
    out->panel_tick_pending = s_panel_tick_pending;
    out->panel_tick_missing = s_panel_tick_missing;
    out->panel_tick_posted_count = s_panel_tick_posted_count;
    out->panel_tick_skipped_count = s_panel_tick_skipped_count;
    out->panel_tick_executed_count = s_panel_tick_executed_count;
    out->panel_tick_missing_count = s_panel_tick_missing_count;
    out->panel_tick_queue_full_count = s_panel_tick_queue_full_count;
    out->compile_state = s_compile_state;
    out->active_module_name = s_active_module_name;
    out->active_script_name = s_active_script_name;
    if (s_mutex) xSemaphoreGive(s_mutex);
}

void hmi_script_engine_log_stats(const char *where)
{
#if HMI_LOG_SCRIPT_RUNTIME_DETAIL
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    log_script_lifetime_locked(where ? where : "stats");
    if (s_mutex) xSemaphoreGive(s_mutex);
#else
    (void)where;
#endif
}

const char *hmi_script_engine_last_error(void)
{
    return s_last_error;
}

const char *hmi_script_engine_compile_state(void)
{
    return s_compile_state;
}

bool hmi_script_engine_runtime_valid(void)
{
    return s_runtime_valid;
}

uint32_t hmi_script_engine_compile_generation(void)
{
    return s_compile_generation;
}

const char *hmi_script_engine_active_script_name(void)
{
    return s_active_script_name;
}

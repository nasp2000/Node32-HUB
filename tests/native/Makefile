CXX       ?= g++
CXXFLAGS  ?= -std=c++11 -Wall -Wextra -Wno-sign-conversion -Wno-unused-parameter -g -O0 -MMD
LDFLAGS   ?= -static-libgcc -static-libstdc++

# Coverage support (Linux/GCC only; Windows/LLVM MinGW has known crash with C++ exceptions)
ifdef COVERAGE
CXXFLAGS += --coverage
LDFLAGS  += --coverage
endif

# Detect OS for binary extension and copy command.
# OBJ_TAG suffixes intermediate objects (.o/.d) per OS: Windows and Linux can
# share this folder (Samba) and both run the native tests without their
# incompatible object files colliding.
ifeq ($(OS),Windows_NT)
  BIN_EXT := .exe
  OBJ_TAG := _win
  RM      := del /f /q
  COPY    := copy /y
else
  BIN_EXT :=
  OBJ_TAG := _linux
  RM      := rm -f
  COPY    := cp
endif

SRC_DIR   := ../../chatgpt/src
STUBS_DIR := stubs

INCLUDES  := -I$(STUBS_DIR) -I$(SRC_DIR)

TEST_SRCS := test_modbus_crc test_charset test_string_utils test_recipient_matching test_unikon_checksum test_unikon_training test_grbl_parser test_grbl_resume test_email_builder test_config_utils test_config_validator test_pack_validator test_ftp_client test_probe_grid test_grbl_sender test_proximity_debounce test_proximity_oee test_proximity_goal_do test_proximity_persist test_dualfile_utils test_atomic_write test_storage_default test_bricon_mode test_config_registry test_config_parse test_littlefs_store test_route_registry test_auth test_alert_pipeline test_scheduler_commands test_day_report_fallback test_sensor_config_cycle test_config_defaults test_handler_json test_sensor_base_persist test_handler_post_persist test_radio_redirect test_radio_browser test_i2c_scan_logic test_radio_schedule_core test_radio_elapsed test_upnp_didl test_audio_i2s_poc test_audio_mp3_decoder test_audio_flac_decoder test_audio_source_buffer test_adts_parser test_wav_parser test_audio_limiter test_radio_favicon test_audio_timing test_audio_rolling_buffer test_audio_mp3_eof test_icy_demux test_audio_ring test_do_config test_shift_window test_email_body_format test_proximity_chart test_proximity_inactivity test_proximity_setup_routing test_alerts_save_safe test_chart_history_stream test_eth_config_mac test_usb_chip_table test_usb_ch340_divisor test_usb_ch340_init_seq test_proximity_day_aligned test_proximity_oee_csv test_proximity_day_oee test_proximity_oee_alerts test_proximity_shiftbase test_alerts_persist_real test_system_persist_real test_ntp_persist_real test_camera_stream test_camera_api test_camera_motion test_camera_stream_fps test_camera_sensor test_camera_autostart test_camera_initguard test_camera_p4_h264 test_camera_p4_vision test_camera_record_naming test_camera_record_error test_camera_motion_uyvy test_camera_p4_perf test_camera_psram_ring test_camera_resolution test_touch7_logic test_touch7_geo test_sd_layout test_sensor_history_sources test_sensor_rtc_tail test_proximity_csv_restore test_rtc_state_util test_reboot_anchor_logic test_nvs_write_guard test_chart_ring_serve test_email_day_dedupe test_ogg_parser test_opus_head test_stb_vorbis_decode test_sensor_day_report test_email_body_archive test_lfs_mutex_guard test_email_flash_rpc test_usbip_protocol test_email_recipients test_email_event_fields
TEST_BINS := $(addsuffix $(BIN_EXT),$(TEST_SRCS))

CHARSET_SRC := $(SRC_DIR)/charset_conversion.cpp
CHARSET_OBJ := charset_conversion$(OBJ_TAG).o

-include *.d

.PHONY: all clean test

all: $(TEST_BINS)

# --- modbus_crc: standalone (no prod .cpp needed) ---
test_modbus_crc$(BIN_EXT): test_modbus_crc.cpp $(SRC_DIR)/modbus_crc.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_modbus_crc.cpp

# --- charset: needs production .cpp + stub Arduino.h ---
$(CHARSET_OBJ): $(CHARSET_SRC) $(SRC_DIR)/charset_conversion.h $(STUBS_DIR)/Arduino.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

test_charset$(BIN_EXT): test_charset.cpp $(CHARSET_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_charset.cpp $(CHARSET_OBJ)

STRING_UTILS_SRC  := $(SRC_DIR)/string_utils.cpp
STRING_UTILS_STUB := string_utils_stub.cpp
STRING_UTILS_OBJ  := string_utils$(OBJ_TAG).o

# string_utils_stub.cpp is a copy of the production source, compiled from tests/native/
# so that "commands.h" resolves to stubs/commands.h (not the production one in src/).
ifeq ($(OS),Windows_NT)
$(STRING_UTILS_STUB): $(STRING_UTILS_SRC)
	copy /y $(subst /,\,$<) $(subst /,\,$@) > nul
else
$(STRING_UTILS_STUB): $(STRING_UTILS_SRC)
	$(COPY) $< $@
endif

$(STRING_UTILS_OBJ): $(STRING_UTILS_STUB) $(SRC_DIR)/string_utils.h $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/commands.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

test_string_utils$(BIN_EXT): test_string_utils.cpp $(STRING_UTILS_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_string_utils.cpp $(STRING_UTILS_OBJ)

# --- recipient_matching: uses production header (header-only, stubs provide String) ---
test_recipient_matching$(BIN_EXT): test_recipient_matching.cpp $(SRC_DIR)/recipient_matching_utils.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_recipient_matching.cpp

# --- unikon_checksum: uses production header (header-only) ---
test_unikon_checksum$(BIN_EXT): test_unikon_checksum.cpp $(SRC_DIR)/unikon_checksum.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_unikon_checksum.cpp

# --- unikon_training: standalone reimpl of training-send pure logic ---
test_unikon_training$(BIN_EXT): test_unikon_training.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_unikon_training.cpp

# --- grbl_parser: uses production header (header-only) ---
test_grbl_parser$(BIN_EXT): test_grbl_parser.cpp $(SRC_DIR)/grbl_line_parser.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_grbl_parser.cpp
test_grbl_resume$(BIN_EXT): test_grbl_resume.cpp $(SRC_DIR)/grbl_resume_scan.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_grbl_resume.cpp

# --- email_builder: uses production header (header-only, stubs provide String) ---
test_email_builder$(BIN_EXT): test_email_builder.cpp $(SRC_DIR)/email_parser.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_email_builder.cpp

# --- config_utils: uses production header (header-only, stubs provide String) ---
test_config_utils$(BIN_EXT): test_config_utils.cpp $(SRC_DIR)/config_utils.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_config_utils.cpp

CONFIGVALIDATOR_SRC := $(SRC_DIR)/ConfigValidator.cpp
CONFIGVALIDATOR_OBJ := config_validator$(OBJ_TAG).o

# --- config_validator: compiles production ConfigValidator.cpp (isSafeConfigJsonKey, no ArduinoJson needed) ---
$(CONFIGVALIDATOR_OBJ): $(CONFIGVALIDATOR_SRC) $(SRC_DIR)/ConfigValidator.h $(STUBS_DIR)/Arduino.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

test_config_validator$(BIN_EXT): test_config_validator.cpp $(CONFIGVALIDATOR_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_config_validator.cpp $(CONFIGVALIDATOR_OBJ)

# --- pack_validator: standalone reimpl (10 conflict/dependency rules) ---
test_pack_validator$(BIN_EXT): test_pack_validator.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_pack_validator.cpp

# --- ftp_client: standalone reimpl (PASV parse, filters, upload decisions) ---
test_ftp_client$(BIN_EXT): test_ftp_client.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_ftp_client.cpp

# --- probe_grid: standalone reimpl (buildGridPoints + interpolateZ) ---
test_probe_grid$(BIN_EXT): test_probe_grid.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_probe_grid.cpp

# --- grbl_sender: standalone reimpl (state machine + FIFO + flow) ---
test_grbl_sender$(BIN_EXT): test_grbl_sender.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_grbl_sender.cpp

# --- proximity_debounce: standalone reimpl (debounce, burst, rate) ---
test_proximity_debounce$(BIN_EXT): test_proximity_debounce.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_proximity_debounce.cpp

# --- proximity_oee: compiles the REAL production header helpers — the exact
#     A/P/Q/OEE standard formula (Calculo TRS) + shift-duration planned time ---
test_proximity_oee$(BIN_EXT): test_proximity_oee.cpp $(SRC_DIR)/sensors/proximity_manager.h $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_proximity_oee.cpp

# --- proximity_goal_do: standalone reimpl (goal gating + DO pulse) ---
test_proximity_goal_do$(BIN_EXT): test_proximity_goal_do.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_proximity_goal_do.cpp

# --- proximity_persist: standalone reimpl (production JSON schema save/load) ---
test_proximity_persist$(BIN_EXT): test_proximity_persist.cpp $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h $(STUBS_DIR)/LittleFS.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_proximity_persist.cpp

# --- dualfile_utils: standalone reimpl (normalizePath, isPathSafe) ---
test_dualfile_utils$(BIN_EXT): test_dualfile_utils.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_dualfile_utils.cpp

# --- atomic_write: standalone reimpl (tmp->rename atomic pattern) ---
test_atomic_write$(BIN_EXT): test_atomic_write.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_atomic_write.cpp

# --- storage_default: files explorer default backend order (sd, flash, psram) ---
test_storage_default$(BIN_EXT): test_storage_default.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_storage_default.cpp

# --- bricon_mode: module exclusion (rambox XOR bricon) + RS232 profile rules ---
test_bricon_mode$(BIN_EXT): test_bricon_mode.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_bricon_mode.cpp

# --- config_registry: standalone reimpl (pages.reg slots + max_bytes) ---
test_config_registry$(BIN_EXT): test_config_registry.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_config_registry.cpp

# --- config_parse: standalone reimpl (JSON resilience + default merge) ---
test_config_parse$(BIN_EXT): test_config_parse.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_config_parse.cpp

LITTLEFS_STORE_SRC := $(SRC_DIR)/LittleFsStore.cpp
LITTLEFS_STORE_STUB := littlefs_store_stub.cpp
LITTLEFS_STORE_OBJ := littlefs_store$(OBJ_TAG).o

# littlefs_store_stub.cpp is a copy of the production source, compiled from tests/native/
# so that quoted includes ("littlefs_manager.h") resolve to stubs/ not src/.
ifeq ($(OS),Windows_NT)
$(LITTLEFS_STORE_STUB): $(LITTLEFS_STORE_SRC)
	copy /y $(subst /,\,$<) $(subst /,\,$@) > nul
else
$(LITTLEFS_STORE_STUB): $(LITTLEFS_STORE_SRC)
	$(COPY) $< $@
endif

$(LITTLEFS_STORE_OBJ): $(LITTLEFS_STORE_STUB) $(SRC_DIR)/LittleFsStore.h $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h $(STUBS_DIR)/LittleFS.h $(STUBS_DIR)/littlefs_manager.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

test_littlefs_store$(BIN_EXT): test_littlefs_store.cpp $(LITTLEFS_STORE_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_littlefs_store.cpp $(LITTLEFS_STORE_OBJ)

# --- real_ConfigManager: compiles the REAL production ConfigManager.cpp (+ NvsStore.cpp)
#     against the REAL ArduinoJson header-only library from the waveshare .pio libdeps,
#     so persistence bugs surface on the host with the exact pool/HW semantics of the board.
#     USER_CONFIG_H_ROOT skips user_config.h/feature_flags (no packs), FILESYSTEM_PROVIDER=1
#     plus ENABLE_FILESYSTEM=1 keep the LittleFS code paths active, and the mandatory
#     PACK_* macros are provided explicitly (ConfigManager.cpp:87 / :211 / :495).
REAL_AJ_DIR  := ../../chatgpt/.pio/libdeps/waveshare_8di8do/ArduinoJson/src
REAL_AJ_FLAGS := -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1
REAL_CM_FLAGS := -DUSER_CONFIG_H_ROOT=1 -DFILESYSTEM_PROVIDER=1 -DENABLE_FILESYSTEM=1 \
 -DENABLE_SCHEDULER=0 -DENABLE_MODBUS=0 \
 -DPACK_EMAIL_SUBJECT=\"Node32-HUB\" -DPACK_SYSTEM_NAME=\"Node32-HUB\"
REAL_INCLUDES := -I$(REAL_AJ_DIR) $(INCLUDES)

CONFIGMGR_SRC  := $(SRC_DIR)/ConfigManager.cpp
CONFIGMGR_STUB := config_manager_stub.cpp
CONFIGMGR_OBJ  := config_manager$(OBJ_TAG).o
NVSSTORE_OBJ   := nvs_store$(OBJ_TAG).o
CM_HARNESS_OBJ := config_manager_harness$(OBJ_TAG).o
CONFIGVALIDATOR_AJ_SRC := $(SRC_DIR)/ConfigValidator.cpp
CONFIGVALIDATOR_AJ_OBJ := config_validator_aj$(OBJ_TAG).o

ifeq ($(OS),Windows_NT)
$(CONFIGMGR_STUB): $(CONFIGMGR_SRC)
	copy /y $(subst /,\,$<) $(subst /,\,$@) > nul
else
$(CONFIGMGR_STUB): $(CONFIGMGR_SRC)
	$(COPY) $< $@
endif

$(CONFIGMGR_OBJ): $(CONFIGMGR_STUB) $(SRC_DIR)/ConfigManager.h $(STUBS_DIR)/Preferences.h $(STUBS_DIR)/nvs_flash.h $(STUBS_DIR)/freertos/FreeRTOS.h $(STUBS_DIR)/LittleFS.h
	$(CXX) $(CXXFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) $(REAL_CM_FLAGS) -c -o $@ $<

$(CONFIGVALIDATOR_AJ_OBJ): $(CONFIGVALIDATOR_AJ_SRC) $(SRC_DIR)/ConfigValidator.h
	$(CXX) $(CXXFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) $(REAL_CM_FLAGS) -c -o $@ $<

$(NVSSTORE_OBJ): $(SRC_DIR)/NvsStore.cpp $(SRC_DIR)/NvsStore.h $(STUBS_DIR)/Preferences.h
	$(CXX) $(CXXFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) $(REAL_CM_FLAGS) -c -o $@ $<

$(CM_HARNESS_OBJ): config_manager_harness.cpp $(SRC_DIR)/ConfigManager.h $(STUBS_DIR)/Preferences.h $(STUBS_DIR)/LittleFS.h
	$(CXX) $(CXXFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) $(REAL_CM_FLAGS) -c -o $@ $<

test_alerts_persist_real$(BIN_EXT): test_alerts_persist_real.cpp $(CONFIGMGR_OBJ) $(NVSSTORE_OBJ) $(CM_HARNESS_OBJ) $(CONFIGVALIDATOR_AJ_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) $(REAL_CM_FLAGS) -o $@ test_alerts_persist_real.cpp $(CONFIGMGR_OBJ) $(NVSSTORE_OBJ) $(CM_HARNESS_OBJ) $(CONFIGVALIDATOR_AJ_OBJ)

test_system_persist_real$(BIN_EXT): test_system_persist_real.cpp $(CONFIGMGR_OBJ) $(NVSSTORE_OBJ) $(CM_HARNESS_OBJ) $(CONFIGVALIDATOR_AJ_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) $(REAL_CM_FLAGS) -o $@ test_system_persist_real.cpp $(CONFIGMGR_OBJ) $(NVSSTORE_OBJ) $(CM_HARNESS_OBJ) $(CONFIGVALIDATOR_AJ_OBJ)

# --- ntp_persist_real: compiles the REAL production NtpManager.cpp against the
# --- REAL ArduinoJson, so the timezone persistence bug surfaces on the host.
# --- ntp_manager_linux.cpp is a copy of the production source with the global
# --- `timezone` renamed to ntpTimeZoneGlobal (single token, GNU sed): glibc
# --- declares `extern long int timezone` under its feature macros, which would
# --- clash with the production `extern String timezone;` on the host. Silent
# --- only on Linux; Windows/MinGW would need the same rename.
NTPMGR_HARNESS_OBJ := ntp_manager_harness$(OBJ_TAG).o
NTPMGR_OBJ         := ntp_manager$(OBJ_TAG).o
NTPMGR_STUB        := ntp_manager_linux.cpp

ifeq ($(OS),Windows_NT)
$(NTPMGR_STUB): $(SRC_DIR)/NtpManager.cpp
	$(COPY) $(subst /,\,$<) $(subst /,\,$@)
else
$(NTPMGR_STUB): $(SRC_DIR)/NtpManager.cpp
	$(COPY) $< $@ && sed -i 's/\bextern String timezone;/\nextern String ntpTimeZoneGlobal;/; s/ timezone = / ntpTimeZoneGlobal = /g; s/^void configTzTime(const char\*, const char\*);//; a void configTzTime(const char*, const char*);' $@
endif

$(NTPMGR_OBJ): $(NTPMGR_STUB) $(SRC_DIR)/NtpManager.h $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/LittleFS.h $(STUBS_DIR)/littlefs_manager.h
	$(CXX) $(CXXFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) -c -o $@ $<

$(NTPMGR_HARNESS_OBJ): ntp_manager_harness.cpp $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/LittleFS.h
	$(CXX) $(CXXFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) -c -o $@ $<

test_ntp_persist_real$(BIN_EXT): test_ntp_persist_real.cpp $(NTPMGR_OBJ) $(NTPMGR_HARNESS_OBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(REAL_INCLUDES) $(REAL_AJ_FLAGS) -o $@ test_ntp_persist_real.cpp $(NTPMGR_OBJ) $(NTPMGR_HARNESS_OBJ)

# --- route_registry: standalone reimpl (registration + lookup semantics) ---
test_route_registry$(BIN_EXT): test_route_registry.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_route_registry.cpp

# --- auth: standalone reimpl (Basic Auth decision logic, viewer read-only) ---
test_auth$(BIN_EXT): test_auth.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_auth.cpp

# --- alert_pipeline: uses MockConfigStore/MockEmailService/MockLogger ---
test_alert_pipeline$(BIN_EXT): test_alert_pipeline.cpp $(STUBS_DIR)/MockConfigStore.h $(STUBS_DIR)/MockEmailService.h $(STUBS_DIR)/MockLogger.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_alert_pipeline.cpp

# --- scheduler_commands: standalone reimpl (SYS prefix, dt slot, interval) ---
test_scheduler_commands$(BIN_EXT): test_scheduler_commands.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_scheduler_commands.cpp

# --- day_report_fallback: compiles production header (pure decision logic) ---
test_day_report_fallback$(BIN_EXT): test_day_report_fallback.cpp $(SRC_DIR)/day_report_fallback.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_day_report_fallback.cpp

# --- sensor_config_cycle: uses MockConfigStore (save/load round-trip) ---
test_sensor_config_cycle$(BIN_EXT): test_sensor_config_cycle.cpp $(STUBS_DIR)/MockConfigStore.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_sensor_config_cycle.cpp

# --- config_defaults: compiles production config_defaults.h (sentinel) ---
test_config_defaults$(BIN_EXT): test_config_defaults.cpp $(SRC_DIR)/config_defaults.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -I$(SRC_DIR) -o $@ test_config_defaults.cpp

# --- handler_json: standalone reimpl (jsonEscape, action/status JSON) ---
test_handler_json$(BIN_EXT): test_handler_json.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_handler_json.cpp

# --- handler_post_persist: POST /api/config persistence via MockConfigStore ---
# Mirrors config_handler.cpp::handleConfigPost() with real JSON round-trips.
test_handler_post_persist$(BIN_EXT): test_handler_post_persist.cpp $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h $(STUBS_DIR)/MockConfigStore.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_handler_post_persist.cpp

# --- sensor_base_persist: compiles production sensor_config_base.h CRTP ---
# sensor_config_base_stub.h is a copy of the production header so that its
# quoted include "../littlefs_manager.h" resolves via tests/littlefs_manager.h
# (forwarding to stubs/littlefs_manager.h) instead of src/. Needs -I$(SRC_DIR)/sensors
# for the production header itself and -I.. for the forwarding header.
SENSOR_BASE_SRC := $(SRC_DIR)/sensors/sensor_config_base.h
SENSOR_BASE_STUB := sensor_config_base_stub.h
ifeq ($(OS),Windows_NT)
$(SENSOR_BASE_STUB): $(SENSOR_BASE_SRC)
	copy /y $(subst /,\,$<) $(subst /,\,$@) > nul
else
$(SENSOR_BASE_STUB): $(SENSOR_BASE_SRC)
	$(COPY) $< $@
endif

test_sensor_base_persist$(BIN_EXT): test_sensor_base_persist.cpp $(SENSOR_BASE_STUB) $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h $(STUBS_DIR)/LittleFS.h $(STUBS_DIR)/littlefs_manager.h ../littlefs_manager.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -I$(SRC_DIR)/sensors -I.. -o $@ test_sensor_base_persist.cpp

# --- radio_redirect: standalone reimpl (3xx redirect chain decision) ---
test_radio_redirect$(BIN_EXT): test_radio_redirect.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_radio_redirect.cpp

# --- radio_browser: standalone reimpl (search URL + field simplification) ---
test_radio_browser$(BIN_EXT): test_radio_browser.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_radio_browser.cpp

# --- i2c_scan_logic: compiles production header (JSON builder + known-name table) ---
test_i2c_scan_logic$(BIN_EXT): test_i2c_scan_logic.cpp $(SRC_DIR)/i2c_scan_logic.h $(STUBS_DIR)/Arduino.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_i2c_scan_logic.cpp

# --- radio_schedule_core: compiles production header (pure weekly-window logic) ---
test_radio_schedule_core$(BIN_EXT): test_radio_schedule_core.cpp $(SRC_DIR)/radio_schedule_core.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_radio_schedule_core.cpp

# --- radio_elapsed: standalone reimpl (stream "minutes playing" accumulation) ---
test_radio_elapsed$(BIN_EXT): test_radio_elapsed.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_radio_elapsed.cpp

# --- upnp_didl: standalone reimpl (DIDL-Lite parser + SOAP BrowseMetadata flag) ---
test_upnp_didl$(BIN_EXT): test_upnp_didl.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_upnp_didl.cpp

# --- audio_i2s_poc: compiles production pure-PCM header (Phase 1 ESP_I2S PoC) ---
test_audio_i2s_poc$(BIN_EXT): test_audio_i2s_poc.cpp $(SRC_DIR)/audio_i2s_poc.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_audio_i2s_poc.cpp

# --- ogg_parser: compiles the production first-party OGG demuxer + Opus
# --- helpers (ogg_parser.h) against a real OGG Opus file + synthetic edges ---
test_ogg_parser$(BIN_EXT): test_ogg_parser.cpp $(SRC_DIR)/ogg_parser.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_ogg_parser.cpp

# --- opus_head: OpusHead + TOC helpers from the production ogg_parser.h ---
test_opus_head$(BIN_EXT): test_opus_head.cpp $(SRC_DIR)/ogg_parser.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_opus_head.cpp

# --- stb_vorbis_decode: compiles the real stb_vorbis decoder (Unlicense)
# --- against a known OGG Vorbis vector. The implementation lives in a
# --- separate TU (stb_vorbis_impl.cpp) because the header leaks macros (C,
# --- CHECK) that collide with doctest.h.
STB_VORBIS_OBJ := stb_vorbis_impl$(OBJ_TAG).o

$(STB_VORBIS_OBJ): stb_vorbis_impl.cpp ../../chatgpt/lib/stb_vorbis/src/stb_vorbis.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -I../../chatgpt/lib/stb_vorbis/src -c -o $@ $<

test_stb_vorbis_decode$(BIN_EXT): test_stb_vorbis_decode.cpp $(STB_VORBIS_OBJ) ../../chatgpt/lib/stb_vorbis/src/stb_vorbis.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -I../../chatgpt/lib/stb_vorbis/src -o $@ test_stb_vorbis_decode.cpp $(STB_VORBIS_OBJ)

# --- audio_mp3_decoder: compiles real minimp3 (CC0) against a known MP3 frame ---
test_audio_mp3_decoder$(BIN_EXT): test_audio_mp3_decoder.cpp ../../chatgpt/lib/minimp3/src/minimp3.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -I../../chatgpt/lib/minimp3/src -o $@ test_audio_mp3_decoder.cpp

# --- audio_flac_decoder: compiles real dr_flac (Unlicense) against a known FLAC frame ---
test_audio_flac_decoder$(BIN_EXT): test_audio_flac_decoder.cpp ../../chatgpt/lib/dr_flac/src/dr_flac.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -I../../chatgpt/lib/dr_flac/src -o $@ test_audio_flac_decoder.cpp

# --- audio_source_buffer: compiles production first-party AudioSourceBuffer
# --- against a mock source ---
test_audio_source_buffer$(BIN_EXT): test_audio_source_buffer.cpp $(SRC_DIR)/audio_source_buffer.cpp $(SRC_DIR)/audio_source_buffer.h $(SRC_DIR)/audio_core.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_audio_source_buffer.cpp $(SRC_DIR)/audio_source_buffer.cpp

# --- adts_parser: compiles production first-party ADTS header parser ---
test_adts_parser$(BIN_EXT): test_adts_parser.cpp $(SRC_DIR)/adts_parser.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_adts_parser.cpp

# --- wav_parser: compiles production first-party WAV header parser ---
test_wav_parser$(BIN_EXT): test_wav_parser.cpp $(SRC_DIR)/wav_parser.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_wav_parser.cpp

# --- audio_limiter: standalone reimpl of the EQ soft-knee limiter ---
test_audio_limiter$(BIN_EXT): test_audio_limiter.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_audio_limiter.cpp

# --- audio_timing: standalone reimpl of the I2S clock-accuracy math ---
test_audio_timing$(BIN_EXT): test_audio_timing.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_audio_timing.cpp

# --- audio_rolling_buffer: decoder's rolling input-buffer algorithm ---
test_audio_rolling_buffer$(BIN_EXT): test_audio_rolling_buffer.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_audio_rolling_buffer.cpp

# --- audio_mp3_eof: real minimp3 + device rolling loop vs teste.mp3 ---
test_audio_mp3_eof$(BIN_EXT): test_audio_mp3_eof.cpp ../../chatgpt/lib/minimp3/src/minimp3.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -I../../chatgpt/lib/minimp3/src -o $@ test_audio_mp3_eof.cpp

# --- radio_favicon: compiles production first-party favicon derivation header ---
test_radio_favicon$(BIN_EXT): test_radio_favicon.cpp $(SRC_DIR)/radio_favicon.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_radio_favicon.cpp

# --- icy_demux: production Shoutcast/Icecast metadata demuxer (pure) ---
test_icy_demux$(BIN_EXT): test_icy_demux.cpp $(SRC_DIR)/icy_demux.cpp $(SRC_DIR)/icy_demux.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_icy_demux.cpp $(SRC_DIR)/icy_demux.cpp

# --- audio_ring: production PSRAM ring + producer pump (pure) ---
test_audio_ring$(BIN_EXT): test_audio_ring.cpp $(SRC_DIR)/audio_ring.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_audio_ring.cpp

# --- do_config: standalone reimpl (DO name/invert/label decision logic) ---
test_do_config$(BIN_EXT): test_do_config.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_do_config.cpp

# --- shift_window: standalone reimpl of shift detection + window math ---
test_shift_window$(BIN_EXT): test_shift_window.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_shift_window.cpp

# --- email_body_format: standalone reimpl of shift email body format ---
test_email_body_format$(BIN_EXT): test_email_body_format.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_email_body_format.cpp

# --- camera_stream: standalone reimpl of MJPEG/recorder stability policies ---
test_camera_stream$(BIN_EXT): test_camera_stream.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_camera_stream.cpp

# --- camera_stream_fps: compiles the production header (pure, no Arduino deps) ---
test_camera_stream_fps$(BIN_EXT): test_camera_stream_fps.cpp $(SRC_DIR)/camera_stream_fps.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_camera_stream_fps.cpp

# --- camera_p4_h264: compiles the production header (pure, no Arduino deps) ---
test_camera_p4_h264$(BIN_EXT): test_camera_p4_h264.cpp $(SRC_DIR)/camera_p4_h264.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_camera_p4_h264.cpp

# --- camera_p4_vision: compiles the production header (pure, no Arduino deps) ---
test_camera_p4_vision$(BIN_EXT): test_camera_p4_vision.cpp $(SRC_DIR)/camera_p4_vision.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_camera_p4_vision.cpp

# --- camera_record_naming: compiles the production header (pure, no Arduino deps) ---
test_camera_record_naming$(BIN_EXT): test_camera_record_naming.cpp $(SRC_DIR)/camera_record_naming.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_camera_record_naming.cpp

# --- camera_record_error: compiles the production header (pure, no Arduino deps) ---
test_camera_record_error$(BIN_EXT): test_camera_record_error.cpp $(SRC_DIR)/camera_record_error.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_camera_record_error.cpp

# --- camera_motion_uyvy: compiles the production header (pure, no Arduino deps) ---
test_camera_motion_uyvy$(BIN_EXT): test_camera_motion_uyvy.cpp $(SRC_DIR)/camera_motion_uyvy.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_camera_motion_uyvy.cpp

# --- camera_p4_perf: standalone reimpl of resList ordering + P4 fps clamp ---
test_camera_p4_perf$(BIN_EXT): test_camera_p4_perf.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_camera_p4_perf.cpp

# --- camera_psram_ring: compiles the production header (pure, no Arduino deps) ---
test_camera_psram_ring$(BIN_EXT): test_camera_psram_ring.cpp $(SRC_DIR)/camera_psram_ring.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_camera_psram_ring.cpp

# --- camera_resolution: compiles the production parser + mode table (pure C, no Arduino deps) ---
test_camera_resolution$(BIN_EXT): test_camera_resolution.cpp $(SRC_DIR)/camera_resolution.h $(SRC_DIR)/camera_resolution.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_camera_resolution.cpp

# --- camera_sensor: compiles production header (PID -> name/max framesize) ---
test_camera_sensor$(BIN_EXT): test_camera_sensor.cpp $(SRC_DIR)/camera_sensor_map.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_camera_sensor.cpp

# --- camera_api: standalone reimpl of /api/camera/config POST pipeline ---
test_camera_api$(BIN_EXT): test_camera_api.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_camera_api.cpp

# --- camera_motion: standalone reimpl of motion integration guards ---
test_camera_motion$(BIN_EXT): test_camera_motion.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_camera_motion.cpp

# --- camera_initguard: standalone reimpl of init/stop serialisation ---
test_camera_initguard$(BIN_EXT): test_camera_initguard.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_camera_initguard.cpp

# --- touch7_logic: compiles the production header (pure, no Arduino deps) ---
test_touch7_logic$(BIN_EXT): test_touch7_logic.cpp $(SRC_DIR)/touch7_peers_logic.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_touch7_logic.cpp

# --- touch7_geo: compiles the production header (pure, no Arduino deps) ---
test_touch7_geo$(BIN_EXT): test_touch7_geo.cpp $(SRC_DIR)/touch7_geo.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_touch7_geo.cpp

# --- camera_autostart: standalone reimpl of Task 3 boot auto-start hooks ---
test_camera_autostart$(BIN_EXT): test_camera_autostart.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_camera_autostart.cpp

# --- proximity_oee_csv: standalone reimpl of weekly OEE CSV pure helpers ---
test_proximity_oee_csv$(BIN_EXT): test_proximity_oee_csv.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_proximity_oee_csv.cpp

# --- proximity_shiftbase: standalone reimpl of shift-baseline restore guard ---
test_proximity_shiftbase$(BIN_EXT): test_proximity_shiftbase.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_proximity_shiftbase.cpp

test_proximity_chart$(BIN_EXT): test_proximity_chart.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_proximity_chart.cpp
test_proximity_inactivity$(BIN_EXT): test_proximity_inactivity.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_proximity_inactivity.cpp
# --- proximity_setup_routing: standalone reimpl (gate-OFF pulses -> setup_count) ---
test_proximity_setup_routing$(BIN_EXT): test_proximity_setup_routing.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_proximity_setup_routing.cpp
# --- proximity_day_aligned: compiles the REAL production header — validates the exact
#     shift-aligned day logic shared by the CSV writer and the SHIFTDAY reads ---
test_proximity_day_aligned$(BIN_EXT): test_proximity_day_aligned.cpp $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h $(SRC_DIR)/sensors/proximity_manager.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_proximity_day_aligned.cpp
# --- proximity_day_oee: compiles the REAL production header — validates the
#     day-key rollover detector and the day-so-far OEE window math ---
test_proximity_day_oee$(BIN_EXT): test_proximity_day_oee.cpp $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h $(SRC_DIR)/sensors/proximity_manager.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_proximity_day_oee.cpp
# --- proximity_oee_alerts: compiles the REAL production header — validates the
#     OEE-alert fire decision used by proximityOeeAlertTick (alertas OEE tab) ---
test_proximity_oee_alerts$(BIN_EXT): test_proximity_oee_alerts.cpp $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h $(SRC_DIR)/sensors/proximity_manager.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_proximity_oee_alerts.cpp
test_chart_history_stream$(BIN_EXT): test_chart_history_stream.cpp $(SRC_DIR)/chart_stream_decimate.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_chart_history_stream.cpp

# --- alerts_save_safe: standalone reimpl (sanitize + writeJsonAtomic verify + heal-on-load) ---
test_alerts_save_safe$(BIN_EXT): test_alerts_save_safe.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_alerts_save_safe.cpp

# --- eth_config_mac: standalone reimpl (MAC override preserved vs applied decision) ---
test_eth_config_mac$(BIN_EXT): test_eth_config_mac.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_eth_config_mac.cpp

# --- usb_chip_table: extract header (chip auto-detect, pure logic) ---
test_usb_chip_table$(BIN_EXT): test_usb_chip_table.cpp $(SRC_DIR)/usb_chip_table.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_usb_chip_table.cpp

# --- usb_ch340_divisor: extract header (baud divisor encoding, pure logic) ---
test_usb_ch340_divisor$(BIN_EXT): test_usb_ch340_divisor.cpp $(SRC_DIR)/usb_ch340_divisor.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_usb_ch340_divisor.cpp

# --- usb_ch340_init_seq: extract header (kernel configure order + gates, pure logic) ---
test_usb_ch340_init_seq$(BIN_EXT): test_usb_ch340_init_seq.cpp $(SRC_DIR)/usb_ch340_init_seq.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_usb_ch340_init_seq.cpp

test_usbip_protocol$(BIN_EXT): test_usbip_protocol.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ test_usbip_protocol.cpp

# --- run all ---
test: $(TEST_BINS)
	$(info Run each test binary individually from the CI script.)

clean:
	$(RM) *.o
	$(RM) test_modbus_crc$(BIN_EXT) test_charset$(BIN_EXT) test_string_utils$(BIN_EXT) test_recipient_matching$(BIN_EXT) test_unikon_checksum$(BIN_EXT) test_grbl_parser$(BIN_EXT) test_grbl_resume$(BIN_EXT) test_email_builder$(BIN_EXT) test_config_utils$(BIN_EXT) test_config_validator$(BIN_EXT) test_pack_validator$(BIN_EXT) test_probe_grid$(BIN_EXT) test_grbl_sender$(BIN_EXT) test_proximity_debounce$(BIN_EXT) test_proximity_oee$(BIN_EXT) test_proximity_goal_do$(BIN_EXT) test_proximity_persist$(BIN_EXT) test_dualfile_utils$(BIN_EXT) test_atomic_write$(BIN_EXT) test_config_registry$(BIN_EXT) test_config_parse$(BIN_EXT) test_littlefs_store$(BIN_EXT) test_route_registry$(BIN_EXT) test_auth$(BIN_EXT) test_alert_pipeline$(BIN_EXT) test_scheduler_commands$(BIN_EXT) test_day_report_fallback$(BIN_EXT) test_sensor_config_cycle$(BIN_EXT) test_config_defaults$(BIN_EXT) test_handler_json$(BIN_EXT) test_sensor_base_persist$(BIN_EXT) test_handler_post_persist$(BIN_EXT) test_radio_redirect$(BIN_EXT) test_radio_browser$(BIN_EXT) test_i2c_scan_logic$(BIN_EXT) test_radio_schedule_core$(BIN_EXT) test_radio_elapsed$(BIN_EXT) test_upnp_didl$(BIN_EXT) test_audio_i2s_poc$(BIN_EXT) test_audio_mp3_decoder$(BIN_EXT) test_audio_flac_decoder$(BIN_EXT) test_audio_source_buffer$(BIN_EXT) test_adts_parser$(BIN_EXT) test_wav_parser$(BIN_EXT) test_audio_limiter$(BIN_EXT) test_radio_favicon$(BIN_EXT) test_audio_timing$(BIN_EXT) test_audio_rolling_buffer$(BIN_EXT) test_audio_mp3_eof$(BIN_EXT) test_icy_demux$(BIN_EXT) test_audio_ring$(BIN_EXT) test_do_config$(BIN_EXT) test_shift_window$(BIN_EXT) test_email_body_format$(BIN_EXT) test_proximity_chart$(BIN_EXT) test_proximity_inactivity$(BIN_EXT) test_proximity_setup_routing$(BIN_EXT) test_alerts_save_safe$(BIN_EXT) test_chart_history_stream$(BIN_EXT) test_eth_config_mac$(BIN_EXT) test_usb_chip_table$(BIN_EXT) test_usb_ch340_divisor$(BIN_EXT) test_usb_ch340_init_seq$(BIN_EXT) test_proximity_day_aligned$(BIN_EXT) test_proximity_day_oee$(BIN_EXT) test_proximity_oee_alerts$(BIN_EXT) test_proximity_oee_csv$(BIN_EXT) test_proximity_shiftbase$(BIN_EXT) test_alerts_persist_real$(BIN_EXT) test_system_persist_real$(BIN_EXT) test_ntp_persist_real$(BIN_EXT) test_camera_stream$(BIN_EXT) test_camera_api$(BIN_EXT) test_camera_motion$(BIN_EXT) test_camera_sensor$(BIN_EXT) test_camera_autostart$(BIN_EXT) test_camera_initguard$(BIN_EXT) test_camera_record_naming$(BIN_EXT) test_camera_record_error$(BIN_EXT) test_touch7_logic$(BIN_EXT) test_touch7_geo$(BIN_EXT) test_sensor_rtc_tail$(BIN_EXT) test_proximity_csv_restore$(BIN_EXT) test_camera_resolution$(BIN_EXT) test_sd_layout$(BIN_EXT) test_sensor_history_sources$(BIN_EXT) test_rtc_state_util test_reboot_anchor_logic$(BIN_EXT) test_nvs_write_guard$(BIN_EXT) test_ogg_parser$(BIN_EXT) test_opus_head$(BIN_EXT) test_stb_vorbis_decode$(BIN_EXT) test_sensor_day_report$(BIN_EXT) test_email_body_archive$(BIN_EXT) test_lfs_mutex_guard$(BIN_EXT) test_email_flash_rpc$(BIN_EXT) test_usbip_protocol$(BIN_EXT) test_email_recipients$(BIN_EXT) test_email_event_fields$(BIN_EXT)
	$(RM) charset_conversion*.o config_validator*.o string_utils*.o string_utils_stub.cpp
	$(RM) stb_vorbis_impl*.o
	$(RM) littlefs_store*.o littlefs_store_stub.cpp
	$(RM) config_manager*.o nvs_store*.o config_manager_stub.cpp config_manager_harness*.o config_validator_aj*.o
	$(RM) ntp_manager*.o ntp_manager_harness*.o ntp_manager_linux.cpp
	$(RM) sensor_config_base_stub.h
	$(RM) *.gcda *.gcno *.gcov *.d

# --- sd_layout: central SD path convention (all packs) ---
test_sd_layout$(BIN_EXT): test_sd_layout.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_sd_layout.cpp

# --- sensor_history_sources: pure source-selection / tail-budget helpers ---
test_sensor_history_sources$(BIN_EXT): test_sensor_history_sources.cpp $(SRC_DIR)/sensor_history_merge.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_sensor_history_sources.cpp

# --- sensor_day_report: pure ShiftEnd-style SensorDay body (resumo + 24 barras) ---
test_sensor_day_report$(BIN_EXT): test_sensor_day_report.cpp $(SRC_DIR)/sensor_day_report.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_sensor_day_report.cpp

# --- email_body_archive: T24 tipo-por-subject + path /{sys}/<tipo>_<data>_<hora>.txt ---
test_email_body_archive$(BIN_EXT): test_email_body_archive.cpp $(SRC_DIR)/email_body_archive.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_email_body_archive.cpp

# --- lfs_mutex_guard: WDT-safe sliced lock wait + race-free lazy publication ---
test_lfs_mutex_guard$(BIN_EXT): test_lfs_mutex_guard.cpp $(SRC_DIR)/lfs_mutex_guard.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_lfs_mutex_guard.cpp

# --- email_flash_rpc: PSRAM-stack flash RPC state machine (single-slot pub/sub) ---
test_email_flash_rpc$(BIN_EXT): test_email_flash_rpc.cpp $(SRC_DIR)/email_flash_rpc_core.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_email_flash_rpc.cpp

# --- sensor_rtc_tail: pure newest-K mirror math over the wrapped ring ---
test_sensor_rtc_tail$(BIN_EXT): test_sensor_rtc_tail.cpp $(SRC_DIR)/sensor_rtc_tail.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_sensor_rtc_tail.cpp

# --- proximity_csv_restore: compiles the REAL production header — validates the
#     absolute-row CSV parsing used by the power-cut SD restore point ---
test_proximity_csv_restore$(BIN_EXT): test_proximity_csv_restore.cpp $(STUBS_DIR)/Arduino.h $(STUBS_DIR)/ArduinoJson.h $(SRC_DIR)/sensors/proximity_manager.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_proximity_csv_restore.cpp

# --- rtc_state_util: pure RTC-RAM runtime-state math (continuous uptime +
#     FTP schedule phase anchor) ---
test_rtc_state_util$(BIN_EXT): test_rtc_state_util.cpp $(SRC_DIR)/rtc_state_util.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_rtc_state_util.cpp
test_reboot_anchor_logic$(BIN_EXT): test_reboot_anchor_logic.cpp $(SRC_DIR)/reboot_anchor_logic.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_reboot_anchor_logic.cpp
test_nvs_write_guard$(BIN_EXT): test_nvs_write_guard.cpp $(SRC_DIR)/nvs_write_guard.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_nvs_write_guard.cpp
# --- email_day_dedupe: pure duplicate-send guard for the day-email funnel ---
test_email_day_dedupe$(BIN_EXT): test_email_day_dedupe.cpp $(SRC_DIR)/email_day_dedupe.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_email_day_dedupe.cpp
# --- email_recipients: pure recipient normalization (dedupe/trim, one envelope) ---
test_email_recipients$(BIN_EXT): test_email_recipients.cpp $(SRC_DIR)/email_recipients.h $(STUBS_DIR)/Arduino.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_email_recipients.cpp
# --- email_event_fields: pure EMAIL_PIPELINE details formatter (err=...) ---
test_email_event_fields$(BIN_EXT): test_email_event_fields.cpp $(SRC_DIR)/email_event_fields.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_email_event_fields.cpp
# --- chart_ring_serve: pure PSRAM-first serve math shared by all .chart endpoints ---
test_chart_ring_serve$(BIN_EXT): test_chart_ring_serve.cpp $(SRC_DIR)/chart_ring_serve.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) -o $@ test_chart_ring_serve.cpp

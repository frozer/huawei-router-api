#include "hw/errors.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace hw {

namespace {

struct ApiErrorEntry {
    int code;
    std::string_view name;
};

// Every entry of apiErrorCodes in src/error.js, sorted by code for binary search.
constexpr std::array<ApiErrorEntry, 137> kApiErrorNames = {{
    {-2, "no_device"},
    {-1, "default"},
    {1, "first_send"},
    {11019, "ussd_net_no_return"},
    {100001, "unknown"},
    {100002, "system_no_support"},
    {100003, "system_no_rights"},
    {100004, "system_busy"},
    {100005, "format_error"},
    {100006, "parameter_error"},
    {100007, "save_config_file_error"},
    {100008, "get_config_file_error"},
    {101001, "no_sim_card_or_invalid_sim_card"},
    {101002, "check_sim_card_pin_lock"},
    {101003, "check_sim_card_pun_lock"},
    {101004, "check_sim_card_can_unusable"},
    {101005, "enable_pin_failed"},
    {101006, "disable_pin_failed"},
    {101007, "unlock_pin_failed"},
    {101008, "disable_auto_pin_failed"},
    {101009, "enable_auto_pin_failed"},
    {102001, "get_net_type_failed"},
    {102002, "get_service_status_failed"},
    {102003, "get_roam_status_failed"},
    {102004, "get_connect_status_failed"},
    {103001, "device_at_execute_failed"},
    {103002, "device_pin_validate_failed"},
    {103003, "device_pin_modify_failed"},
    {103004, "device_puk_modify_failed"},
    {103005, "device_get_autorun_version_failed"},
    {103006, "device_get_api_version_failed"},
    {103007, "device_get_product_information_failed"},
    {103008, "device_sim_card_busy"},
    {103009, "device_sim_lock_input_error"},
    {103010, "device_not_support_remote_operate"},
    {103011, "device_puk_dead_lock"},
    {103012, "device_get_pc_assist_information_failed"},
    {103013, "device_set_log_information_level_failed"},
    {103014, "device_get_log_information_level_failed"},
    {103015, "device_compress_log_file_failed"},
    {103016, "device_restore_file_decrypt_failed"},
    {103017, "device_restore_file_version_match_failed"},
    {103018, "device_restore_file_failed"},
    {103101, "device_set_time_failed"},
    {103102, "compress_log_file_failed"},
    {104001, "dhcp_error"},
    {106001, "safe_error"},
    {107720, "dialup_get_connect_file_error"},
    {107721, "dialup_set_connect_file_error"},
    {107722, "dialup_dialup_management_parse_error"},
    {107724, "dialup_add_profile_error"},
    {107725, "dialup_modify_profile_error"},
    {107726, "dialup_set_default_profile_error"},
    {107727, "dialup_get_profile_list_error"},
    {107728, "dialup_get_auto_apn_match_error"},
    {107729, "dialup_set_auto_apn_match_error"},
    {108001, "login_username_wrong"},
    {108002, "login_password_wrong"},
    {108003, "login_already_login"},
    {108004, "login_modify_password_failed"},
    {108005, "login_too_many_users_logged_in"},
    {108006, "login_username_pwd_wrong"},
    {108007, "login_username_pwd_orerrun"},
    {109001, "language_get_failed"},
    {109002, "language_set_failed"},
    {110001, "online_update_server_not_accessed"},
    {110002, "online_update_already_booted"},
    {110003, "online_update_get_device_information_failed"},
    {110004, "online_update_get_local_group_component_information_failed"},
    {110005, "online_update_not_find_file_on_server"},
    {110006, "online_update_need_reconnect_server"},
    {110007, "online_update_cancel_downloading"},
    {110008, "online_update_same_file_list"},
    {110009, "online_update_connect_error"},
    {110021, "online_update_invalid_url_list"},
    {110022, "online_update_not_support_url_list"},
    {110023, "online_update_not_boot"},
    {110024, "online_update_low_battery"},
    {111001, "ussd_error"},
    {111012, "ussd_function_return_error"},
    {111013, "ussd_in_ussd_session"},
    {111014, "ussd_too_long_content"},
    {111016, "ussd_empty_command"},
    {111017, "ussd_coding_error"},
    {111018, "ussd_at_send_failed"},
    {111019, "ussd_processing"},
    {111020, "ussd_timeout"},
    {111021, "ussd_xml_special_character_transfer_failed"},
    {111022, "ussd_net_not_support_ussd"},
    {112001, "set_net_mode_and_band_when_dialup_failed"},
    {112002, "set_net_search_mode_when_dialup_failed"},
    {112003, "set_net_mode_and_band_failed"},
    {112004, "set_net_search_mode_failed"},
    {112005, "net_register_net_failed"},
    {112006, "net_net_connected_order_not_match"},
    {112007, "net_current_net_mode_not_support"},
    {112008, "net_sim_card_not_ready_status"},
    {112009, "net_memory_alloc_failed"},
    {113017, "sms_null_argument_or_illegal_argument"},
    {113018, "sms_system_busy"},
    {113020, "sms_query_sms_index_list_error"},
    {113031, "sms_set_sms_center_number_failed"},
    {113036, "sms_delete_sms_failed"},
    {113047, "sms_save_config_file_failed"},
    {113053, "sms_not_enough_space"},
    {113054, "sms_telephone_number_too_long"},
    {114001, "sd_file_exist"},
    {114002, "sd_directory_exist"},
    {114004, "sd_file_or_directory_not_exist"},
    {114005, "sd_file_name_too_long"},
    {114006, "sd_no_right"},
    {114007, "sd_file_is_uploading"},
    {115001, "pb_null_argument_or_illegal_argument"},
    {115002, "pb_overtime"},
    {115003, "pb_call_system_function_error"},
    {115004, "pb_write_file_error"},
    {115005, "pb_read_file_error"},
    {115199, "pb_local_telephone_full_error"},
    {116001, "stk_null_argument_or_illegal_argument"},
    {116002, "stk_overtime"},
    {116003, "stk_call_system_function_error"},
    {116004, "stk_write_file_error"},
    {116005, "stk_read_file_error"},
    {117001, "wifi_station_connect_ap_password_error"},
    {117002, "wifi_web_password_or_dhcp_overtime_error"},
    {117003, "wifi_pbc_connect_failed"},
    {117004, "wifi_station_connect_ap_wispr_password_error"},
    {118001, "cradle_get_current_connected_user_ip_failed"},
    {118002, "cradle_get_current_connected_user_mac_failed"},
    {118003, "cradle_set_mac_failed"},
    {118004, "cradle_get_wan_information_failed"},
    {118005, "cradle_coding_failed"},
    {118006, "cradle_update_profile_failed"},
    {120001, "voice_busy"},
    {125001, "wrong_token"},
    {125002, "wrong_session"},
    {125003, "wrong_session_token"},
}};

constexpr std::string_view kHttpRequestPrefix = "http_request_";

std::string api_display_name(int api_code) {
    if (auto name = api_error_name(api_code)) {
        return std::string(*name);
    }
    return std::to_string(api_code);
}

}  // namespace

RouterError::RouterError(std::string code, const std::string& message)
    : std::runtime_error(message.empty() ? code : message), code_(std::move(code)) {}

const std::string& RouterError::code() const noexcept {
    return code_;
}

RouterApiError::RouterApiError(int api_code, const std::string& router_message)
    : RouterError("api_" + api_display_name(api_code),
                  api_display_name(api_code) +
                      (router_message.empty() ? std::string() : " : " + router_message)),
      api_code_(api_code) {}

int RouterApiError::api_code() const noexcept {
    return api_code_;
}

RequestError::RequestError(const std::string& code, const std::string& message)
    : RouterError(std::string(kHttpRequestPrefix) + code, message) {}

std::optional<std::string_view> api_error_name(int api_code) noexcept {
    const auto it = std::lower_bound(
        kApiErrorNames.begin(), kApiErrorNames.end(), api_code,
        [](const ApiErrorEntry& entry, int code) { return entry.code < code; });
    if (it != kApiErrorNames.end() && it->code == api_code) {
        return it->name;
    }
    return std::nullopt;
}

bool is_auth_error(const RouterError& e) noexcept {
    const auto* api = dynamic_cast<const RouterApiError*>(&e);
    return api != nullptr && api->api_code() >= 108001 && api->api_code() <= 108007;
}

bool is_token_error(const RouterError& e) noexcept {
    const auto* api = dynamic_cast<const RouterApiError*>(&e);
    return api != nullptr && api->api_code() >= 125001 && api->api_code() <= 125003;
}

bool is_connection_error(const RouterError& e) noexcept {
    return e.code().starts_with(kHttpRequestPrefix);
}

}  // namespace hw

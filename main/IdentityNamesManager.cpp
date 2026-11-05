#include "IdentityNamesManager.hpp"

#include "JsonGuard.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

#include <cctype>
#include <cstdlib>

void IdentityNamesManager::begin() {
  if (!load()) {
    ESP_LOGI(TAG, "No identity names stored yet.");
  } else {
    ESP_LOGI(TAG, "Loaded %u issuer names and %u access-code names.",
             static_cast<unsigned>(m_issuers.size()),
             static_cast<unsigned>(m_codes.size()));
  }
}

std::string IdentityNamesManager::normalizeIssuerId(const std::string& issuerIdHex) {
  std::string out;
  out.reserve(issuerIdHex.size());
  for (char c : issuerIdHex) {
    if (std::isxdigit(static_cast<unsigned char>(c))) {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

std::string IdentityNamesManager::issuerName(const std::string& issuerIdHex) const {
  const std::string key = normalizeIssuerId(issuerIdHex);
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_issuers.find(key);
  return it == m_issuers.end() ? std::string{} : it->second;
}

std::string IdentityNamesManager::codeName(uint16_t accessCodeId) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto it = m_codes.find(accessCodeId);
  return it == m_codes.end() ? std::string{} : it->second;
}

void IdentityNamesManager::setIssuerName(const std::string& issuerIdHex, const std::string& name) {
  const std::string key = normalizeIssuerId(issuerIdHex);
  if (key.empty()) return;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (name.empty()) {
      m_issuers.erase(key);
    } else {
      m_issuers[key] = name;
    }
  }
  save();
}

void IdentityNamesManager::setCodeName(uint16_t accessCodeId, const std::string& name) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (name.empty()) {
      m_codes.erase(accessCodeId);
    } else {
      m_codes[accessCodeId] = name;
    }
  }
  save();
}

std::map<std::string, std::string> IdentityNamesManager::allIssuerNames() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_issuers;
}

std::map<uint16_t, std::string> IdentityNamesManager::allCodeNames() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_codes;
}

bool IdentityNamesManager::load() {
  nvs_handle handle;
  if (nvs_open(NVS_NS, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  size_t len = 0;
  esp_err_t err = nvs_get_str(handle, NVS_KEY, nullptr, &len);
  if (err != ESP_OK || len == 0) {
    nvs_close(handle);
    return false;
  }
  std::string json(len, '\0');
  err = nvs_get_str(handle, NVS_KEY, json.data(), &len);
  nvs_close(handle);
  if (err != ESP_OK) {
    return false;
  }
  if (!json.empty() && json.back() == '\0') {
    json.pop_back();
  }

  JsonGuard root(cJSON_Parse(json.c_str()));
  if (!root || !cJSON_IsObject(root.get())) {
    return false;
  }
  std::map<std::string, std::string> issuers;
  std::map<uint16_t, std::string> codes;
  cJSON* issuersObj = cJSON_GetObjectItem(root.get(), "issuers");
  if (cJSON_IsObject(issuersObj)) {
    for (cJSON* child = issuersObj->child; child != nullptr; child = child->next) {
      if (!cJSON_IsString(child) || child->string == nullptr) continue;
      std::string key = normalizeIssuerId(child->string);
      std::string val = child->valuestring ? child->valuestring : "";
      if (key.empty() || val.empty()) continue;
      if (val.size() > 64) val.resize(64);
      issuers[key] = val;
    }
  }
  cJSON* codesObj = cJSON_GetObjectItem(root.get(), "codes");
  if (cJSON_IsObject(codesObj)) {
    for (cJSON* child = codesObj->child; child != nullptr; child = child->next) {
      if (!cJSON_IsString(child) || child->string == nullptr) continue;
      char* end = nullptr;
      unsigned long id = strtoul(child->string, &end, 10);
      if (end == child->string || id == 0 || id > 65535) continue;
      std::string val = child->valuestring ? child->valuestring : "";
      if (val.empty()) continue;
      if (val.size() > 64) val.resize(64);
      codes[static_cast<uint16_t>(id)] = val;
    }
  }
  std::lock_guard<std::mutex> lock(m_mutex);
  m_issuers = std::move(issuers);
  m_codes = std::move(codes);
  return true;
}

bool IdentityNamesManager::save() const {
  std::string json = serializeToJson();
  nvs_handle handle;
  esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return false;
  }
  err = nvs_set_str(handle, NVS_KEY, json.c_str());
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save identity names: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool IdentityNamesManager::updateFromJson(const std::string& json) {
  JsonGuard root(cJSON_Parse(json.c_str()));
  if (!root || !cJSON_IsObject(root.get())) {
    return false;
  }

  std::map<std::string, std::string> issuers;
  std::map<uint16_t, std::string> codes;

  cJSON* issuersObj = cJSON_GetObjectItem(root.get(), "issuers");
  if (cJSON_IsObject(issuersObj)) {
    for (cJSON* child = issuersObj->child; child != nullptr; child = child->next) {
      if (!cJSON_IsString(child) || child->string == nullptr) continue;
      std::string key = normalizeIssuerId(child->string);
      std::string val = child->valuestring ? child->valuestring : "";
      if (key.empty()) continue;
      if (val.empty()) continue;
      if (val.size() > 64) val.resize(64);
      issuers[key] = val;
    }
  }

  cJSON* codesObj = cJSON_GetObjectItem(root.get(), "codes");
  if (cJSON_IsObject(codesObj)) {
    for (cJSON* child = codesObj->child; child != nullptr; child = child->next) {
      if (!cJSON_IsString(child) || child->string == nullptr) continue;
      char* end = nullptr;
      unsigned long id = strtoul(child->string, &end, 10);
      if (end == child->string || id == 0 || id > 65535) continue;
      std::string val = child->valuestring ? child->valuestring : "";
      if (val.empty()) continue;
      if (val.size() > 64) val.resize(64);
      codes[static_cast<uint16_t>(id)] = val;
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Only replace maps that were present in the JSON so partial updates work.
    if (issuersObj != nullptr) {
      m_issuers = std::move(issuers);
    }
    if (codesObj != nullptr) {
      m_codes = std::move(codes);
    }
  }
  return save();
}

std::string IdentityNamesManager::serializeToJson() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  JsonBuilder root = JsonBuilder::object();
  JsonBuilder issuers = JsonBuilder::object();
  for (const auto& [id, name] : m_issuers) {
    issuers.addString(id.c_str(), name.c_str());
  }
  JsonBuilder codes = JsonBuilder::object();
  for (const auto& [id, name] : m_codes) {
    codes.addString(std::to_string(id).c_str(), name.c_str());
  }
  root.addItem("issuers", JsonGuard(issuers.release()));
  root.addItem("codes", JsonGuard(codes.release()));
  return root.toStringUnformatted();
}

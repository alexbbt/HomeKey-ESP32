#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief Persist human-readable names for Home Key issuers and access codes.
 *
 * Names are local display / MQTT operator labels only. Issuer ids are Apple
 * Home Key person hashes; access-code ids are HAP identifiers on this lock.
 * PIN digits are never stored here.
 */
class IdentityNamesManager {
public:
  void begin();

  std::string issuerName(const std::string& issuerIdHex) const;
  std::string codeName(uint16_t accessCodeId) const;

  void setIssuerName(const std::string& issuerIdHex, const std::string& name);
  void setCodeName(uint16_t accessCodeId, const std::string& name);

  std::map<std::string, std::string> allIssuerNames() const;
  std::map<uint16_t, std::string> allCodeNames() const;

  /** Replace issuer and/or code name maps from a JSON object and persist. */
  bool updateFromJson(const std::string& json);
  std::string serializeToJson() const;

private:
  bool load();
  bool save() const;
  static std::string normalizeIssuerId(const std::string& issuerIdHex);

  mutable std::mutex m_mutex;
  std::map<std::string, std::string> m_issuers;
  std::map<uint16_t, std::string> m_codes;

  static constexpr const char* TAG = "IdentityNames";
  static constexpr const char* NVS_NS = "id_names";
  static constexpr const char* NVS_KEY = "names";
};

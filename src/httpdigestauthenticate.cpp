/**
 * @file httpdigestauthenticate.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014 Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include "httpdigestauthenticate.h"
#include <openssl/md5.h>
#include "mementosasevent.h"
#include <time.h>

HTTPDigestAuthenticate::HTTPDigestAuthenticate(AuthStore* auth_store,
                                               HomesteadConnection* homestead_conn,
                                               std::string home_domain,
                                               Counter* stat_auth_challenge_count,
                                               Counter* stat_auth_attempt_count,
                                               Counter* stat_auth_success_count,
                                               Counter* stat_auth_failure_count,
                                               Counter* stat_auth_stale_count) :
  _auth_store(auth_store),
  _homestead_conn(homestead_conn),
  _home_domain(home_domain),
  _stat_auth_challenge_count(stat_auth_challenge_count),
  _stat_auth_attempt_count(stat_auth_attempt_count),
  _stat_auth_success_count(stat_auth_success_count),
  _stat_auth_failure_count(stat_auth_failure_count),
  _stat_auth_stale_count(stat_auth_stale_count)
{
}

HTTPDigestAuthenticate::~HTTPDigestAuthenticate()
{
}

// LCOV_EXCL_START - The components of this function are tested separately
/// authenticate_request
/// Authenticates a request based on the IMPU and authorization request
HTTPCode HTTPDigestAuthenticate::authenticate_request(const std::string impu,
                                                      std::string authorization_header,
                                                      std::string& www_auth_header,
                                                      std::string method,
                                                      SAS::TrailId trail)
{
  set_members(impu, method, "", trail);
  Response* response = new Response();

  // Check whether the request contains authorization information
  bool auth_info = false;
  HTTPCode rc = check_auth_header(authorization_header, auth_info, response);

  // The authorization header was invalid
  if (rc != HTTP_OK)
  {
    return rc;
  }

  // If there's a full authorization header, attempt to retrieve the digest
  // from memcached. If not, request the digest from Homestead.
  if (auth_info)
  {
    SAS::Event event(trail, SASEvent::AUTHENTICATION_PRESENT, 0);
    event.add_var_param(authorization_header);
    SAS::report_event(event);

    rc = retrieve_digest_from_store(www_auth_header, response);
  }
  else
  {
    SAS::Event event(trail, SASEvent::NO_AUTHENTICATION_PRESENT, 0);
    SAS::report_event(event);

    rc = request_digest_and_store(www_auth_header, false, response);
  }

  delete response; response = NULL;

  return rc;
}
// LCOV_EXCL_STOP

HTTPCode HTTPDigestAuthenticate::check_auth_header(std::string authorization_header,
                                                   bool& auth_info,
                                                   Response* response)
{
  HTTPCode rc = HTTP_OK;

  if (authorization_header == "")
  {
    // No authorization information present. Default the private ID from the pub
    // public ID
    TRC_DEBUG("No authorization header present");

    auth_info = false;

    std::string sip = "sip:";
    size_t sip_pos = _impu.find(sip);
    if (sip_pos != std::string::npos)
    {
      _impi = _impu.substr(sip_pos + sip.length(), std::string::npos);
    }
    else
    {
      TRC_DEBUG("Private ID can't be derived from the public ID (%s)", _impu.c_str());
      rc = HTTP_BAD_REQUEST;
    }
  }
  else
  {
    // Check header is valid. Header should contain a username, realm, nonce, uri,
    // qop, nc, cnonce, response, opaque or only a username.
    TRC_DEBUG("Authorization header present: %s", authorization_header.c_str());

    rc = parse_auth_header(authorization_header, auth_info, response);

    if (rc == HTTP_OK)
    {
      if (auth_info)
      {
        if (response->_qop != "auth")
        {
          TRC_DEBUG("Client requesting non-auth (%s) digest", response->_qop.c_str());
          return HTTP_BAD_REQUEST;
        }
      }

      TRC_DEBUG("Authorization header is in a valid form for ID %s", _impi.c_str());
    }
  }

  return rc;
}

HTTPCode HTTPDigestAuthenticate::parse_auth_header(std::string auth_header,
                                                   bool& auth_info,
                                                   Response* response)
{
  HTTPCode rc = HTTP_OK;

  std::string digest = "Digest";
  size_t digest_pos = auth_header.find(digest);

  if (digest_pos != 0)
  {
    TRC_DEBUG("Authorization header doesn't contain Digest credentials");
    return HTTP_BAD_REQUEST;
  }

  std::string rest_of_header = auth_header.substr(digest.length());

  // Split the request on the delimiter ','
  std::vector<std::string> response_params;
  Utils::split_string(rest_of_header, ',', response_params, 0, true, true);

  // Then split by = and sort through array.
  std::map<std::string, std::string> response_key_values;
  std::vector<std::string> temp_value;

  for (std::vector<std::string>::iterator ii = response_params.begin(); ii != response_params.end(); ++ii)
  {
    std::string parameter(ii->c_str());
    Utils::split_string(parameter, '=', temp_value, 2, true);

    // Strip quotes off befores storing
    int val_len = temp_value[1].length();
    if (val_len > 1 && temp_value[1][0] == '"' && temp_value[1][val_len - 1] == '"')
    {
      temp_value[1] = temp_value[1].substr(1, val_len - 2);
    }

    response_key_values.insert(std::pair<std::string, std::string>(temp_value[0], temp_value[1]));
    temp_value.clear();
  }

  std::map<std::string, std::string>::iterator username_entry =
                                  response_key_values.find("username");
  std::map<std::string, std::string>::iterator realm_entry =
                                  response_key_values.find("realm");
  std::map<std::string, std::string>::iterator nonce_entry =
                                  response_key_values.find("nonce");
  std::map<std::string, std::string>::iterator uri_entry =
                                  response_key_values.find("uri");
  std::map<std::string, std::string>::iterator qop_entry =
                                  response_key_values.find("qop");
  std::map<std::string, std::string>::iterator nc_entry =
                                  response_key_values.find("nc");
  std::map<std::string, std::string>::iterator cnonce_entry =
                                  response_key_values.find("cnonce");
  std::map<std::string, std::string>::iterator response_entry =
                                  response_key_values.find("response");
  std::map<std::string, std::string>::iterator opaque_entry =
                                  response_key_values.find("opaque");

  // A valid Authorization header must contain a username.
  // It must then either contain all of a realm, nonce, uri, qop, nc,
  // cnonce, response and opaque values, or none of the above.
  // It can contain other parameters; these aren't validated.
  if ((username_entry != response_key_values.end()) &&
      (realm_entry != response_key_values.end()) &&
      (nonce_entry != response_key_values.end()) &&
      (uri_entry != response_key_values.end()) &&
      (qop_entry != response_key_values.end()) &&
      (nc_entry != response_key_values.end()) &&
      (cnonce_entry != response_key_values.end()) &&
      (response_entry != response_key_values.end()) &&
      (opaque_entry != response_key_values.end()))
  {
      TRC_DEBUG("Authorization header valid and complete");
      _impi = username_entry->second;
      auth_info = true;
      response->set_members(username_entry->second,
                            realm_entry->second,
                            nonce_entry->second,
                            uri_entry->second,
                            qop_entry->second,
                            nc_entry->second,
                            cnonce_entry->second,
                            response_entry->second,
                            opaque_entry->second);

    TRC_DEBUG("Raising correlating marker with opaque value = %s",
              opaque_entry->second.c_str());
    SAS::Marker corr(_trail, MARKED_ID_GENERIC_CORRELATOR, 0);
    corr.add_var_param(opaque_entry->second);

    // The marker should be trace-scoped, and should not reactivate any trail
    // groups 
    SAS::report_marker(corr, SAS::Marker::Scope::Trace, false);      
  }
  else if ((username_entry != response_key_values.end()) &&
           (realm_entry == response_key_values.end()) &&
           (nonce_entry == response_key_values.end()) &&
           (uri_entry == response_key_values.end()) &&
           (qop_entry == response_key_values.end()) &&
           (nc_entry == response_key_values.end()) &&
           (cnonce_entry == response_key_values.end()) &&
           (response_entry == response_key_values.end()) &&
           (opaque_entry == response_key_values.end()))
  {
    TRC_DEBUG("Authorization header valid and minimal");
    _impi = username_entry->second;
    auth_info = false;
  }
  else
  {
    TRC_DEBUG("Authorization header not valid");
    rc = HTTP_BAD_REQUEST;
  }

  return rc;
}

HTTPCode HTTPDigestAuthenticate::retrieve_digest_from_store(std::string& www_auth_header,
                                                            Response* response)
{
  TRC_DEBUG("Retrieve digest for IMPU: %s, IMPI: %s", _impu.c_str(), _impi.c_str());
  HTTPCode rc = HTTP_OK;

  _stat_auth_attempt_count->increment();

  AuthStore::Digest* digest;
  Store::Status store_rc = _auth_store->get_digest(_impi, response->_nonce, digest, _trail);

  if (store_rc == Store::OK)
  {
    // Successfully retrieved digest, so check whether it matches
    // the response sent by the client
    rc = check_if_matches(digest, www_auth_header, response);
  }
  else
  {
    // Digest wasn't found in the store. Request the digest from
    // homestead
    SAS::Event event(_trail, SASEvent::AUTHENTICATION_OUT_OF_DATE, 0);
    SAS::report_event(event);

    rc = request_digest_and_store(www_auth_header, true, response);
  }

  delete digest; digest = NULL;
  return rc;
}

// Request a digest from Homestead, store it in memcached, and generate
// the WWW-Authenticate header.
HTTPCode HTTPDigestAuthenticate::request_digest_and_store(std::string& www_auth_header,
                                                          bool include_stale,
                                                          Response* response)
{
  HTTPCode rc = HTTP_BAD_REQUEST;
  std::string ha1;
  std::string realm;
  TRC_DEBUG("Request digest for IMPU: %s, IMPI: %s", _impu.c_str(), _impi.c_str());

  // Request the digest from homestead
  rc = _homestead_conn->get_digest_data(_impi, _impu, ha1, realm, _trail);

  if (rc == HTTP_OK)
  {
    // Generate the digest structure and store it in memcached
    TRC_DEBUG("Store digest for IMPU: %s, IMPI: %s", _impu.c_str(), _impi.c_str());
    AuthStore::Digest* digest = new AuthStore::Digest();
    generate_digest(ha1, realm, digest);
    Store::Status status = _auth_store->set_digest(_impi, digest->_nonce, digest, _trail);

    if (status == Store::OK)
    {
      // Update statistics - either stale or (initial) challenge.
      if (include_stale)
      {
        _stat_auth_stale_count->increment();
      }
      else
      {
        _stat_auth_challenge_count->increment();
      }

      // Create the WWW-Authenticate header
      generate_www_auth_header(www_auth_header, include_stale, digest);
      rc = HTTP_UNAUTHORIZED;
    }
    else
    {
      // LCOV_EXCL_START - Store used in UT never fails
      TRC_ERROR("Unable to write digest to store");
      rc = HTTP_SERVER_ERROR;
      // LCOV_EXCL_STOP
    }
    delete digest; digest = NULL;
  }

  return rc;
}

// Check if the response from the client matches the stored digest
// The logic is:
//   HA1 is the digest returned from Homestead.
//   HA2 = MD5(method : uri), e.g. MD5(GET:/org.projectclearwater.call-list/users/<IMPU>/call-list.xml)
//   response = MD5(HA1 : nonce : nonce_count (provided by client) : cnonce : qop : HA2)
HTTPCode HTTPDigestAuthenticate::check_if_matches(AuthStore::Digest* digest,
                                                  std::string& www_auth_header,
                                                  Response* response)
{
  HTTPCode rc = HTTP_OK;

  if (digest->_opaque != response->_opaque)
  {
    TRC_DEBUG("The opaque value in the request (%s) doesn't match the stored value (%s)",
              response->_opaque.c_str(), digest->_opaque.c_str());
    return HTTP_BAD_REQUEST;
  }
  else if (response->_realm != digest->_realm)
  {
    TRC_DEBUG("Request not targeted at the stored domain. Target: %s, Realm: %s",
              response->_realm.c_str(), digest->_realm.c_str());
    return HTTP_BAD_REQUEST;
  }

  unsigned char ha2[Utils::MD5_HASH_SIZE];
  unsigned char ha2_hex[Utils::HEX_HASH_SIZE + 1];

  MD5_CTX Md5Ctx;
  MD5_Init(&Md5Ctx);
  MD5_Update(&Md5Ctx, _method.c_str(), strlen(_method.c_str()));
  MD5_Update(&Md5Ctx, ":", 1);
  MD5_Update(&Md5Ctx, response->_uri.c_str(), strlen(response->_uri.c_str()));
  MD5_Final(ha2, &Md5Ctx);
  Utils::hashToHex(ha2, ha2_hex);

  unsigned char resp[Utils::MD5_HASH_SIZE];
  unsigned char resp_hex[Utils::HEX_HASH_SIZE + 1];

  MD5_Init(&Md5Ctx);
  MD5_Update(&Md5Ctx, digest->_ha1.c_str(), strlen(digest->_ha1.c_str()));
  MD5_Update(&Md5Ctx, ":", 1);
  MD5_Update(&Md5Ctx, response->_nonce.c_str(), strlen(response->_nonce.c_str()));
  MD5_Update(&Md5Ctx, ":", 1);
  MD5_Update(&Md5Ctx, response->_nc.c_str(), strlen(response->_nc.c_str()));
  MD5_Update(&Md5Ctx, ":", 1);
  MD5_Update(&Md5Ctx, response->_cnonce.c_str(), strlen(response->_cnonce.c_str()));
  MD5_Update(&Md5Ctx, ":", 1);
  MD5_Update(&Md5Ctx, response->_qop.c_str(), strlen(response->_qop.c_str()));
  MD5_Update(&Md5Ctx, ":", 1);
  MD5_Update(&Md5Ctx, ha2_hex, Utils::HEX_HASH_SIZE);
  MD5_Final(resp, &Md5Ctx);
  Utils::hashToHex(resp, resp_hex);

  std::string stored_response((const char*) resp_hex);

  if (response->_response == stored_response)
  {
    TRC_DEBUG("Client response matches stored digest");

    // If the nonce count supplied isn't an int then this will return 0,
    // and the nonce will be treated as stale.
    uint32_t client_count = atoi(response->_nc.c_str());

    // Nonce count is stale. Request the digest from Homestead again.
    if (client_count < digest->_nonce_count)
    {
      TRC_DEBUG("Client response's nonce count is too low");
      SAS::Event event(_trail, SASEvent::AUTHENTICATION_OUT_OF_DATE, 0);
      SAS::report_event(event);

      rc = request_digest_and_store(www_auth_header, true, response);
    }
    else if (_impu != digest->_impu)
    {
      TRC_DEBUG("Request's IMPU doesn't match stored IMPU. Target: %s, Stored: %s",
               _impu.c_str(), digest->_impu.c_str());
      SAS::Event event(_trail, SASEvent::AUTHENTICATION_WRONG_IMPU, 0);
      event.add_var_param(_impu);
      event.add_var_param(digest->_impu);
      SAS::report_event(event);

      rc = request_digest_and_store(www_auth_header, true, response);
    }
    else
    {
      // Authentication successful. Increment the stored nonce count
      digest->_nonce_count++;
      Store::Status store_rc = _auth_store->set_digest(_impi,
                                                       digest->_nonce,
                                                       digest,
                                                       _trail);
      TRC_DEBUG("Updating nonce count - store returned %d", store_rc);

      if (store_rc == Store::DATA_CONTENTION)
      {
        // The write to the store failed due to a CAS mismatch.  This means that
        // the digest has already been used to authenticate another request, so
        // the authentication on this request is stale. Rechallenge.
        TRC_DEBUG("Failed to update nonce count - rechallenge");
        SAS::Event event(_trail, SASEvent::AUTHENTICATION_OUT_OF_DATE, 1);
        SAS::report_event(event);
        rc = request_digest_and_store(www_auth_header, true, response);
      }
      else
      {
        // The nonce count was either updated successfully (in which case we
        // accept the request) or the store failed (in which case we're not sure
        // what to do for the best, and accepting the request is sensible
        // default behaviour).
        TRC_DEBUG("Authentication accepted");
        SAS::Event event(_trail, SASEvent::AUTHENTICATION_ACCEPTED, 0);
        SAS::report_event(event);
        _stat_auth_success_count->increment();
      }
    }
  }
  else
  {
    // Digest doesn't match - reject the request
    TRC_DEBUG("Client response doesn't match stored digest");
    SAS::Event event(_trail, SASEvent::AUTHENTICATION_REJECTED, 0);
    SAS::report_event(event);

    _stat_auth_failure_count->increment();

    rc = HTTP_FORBIDDEN;
  }

  return rc;
}

void gen_unique_val(size_t length, std::string& unique_val)
{
  unique_val.reserve(length);

  long timestamp;
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  timestamp = spec.tv_sec * 1000 + (spec.tv_nsec / 1000000);

  unique_val = std::to_string(timestamp);

  std::string token;
  Utils::create_random_token(length - unique_val.length(), token);
  unique_val += token;
}

// Populate the Digest, including generating the nonce
void HTTPDigestAuthenticate::generate_digest(std::string ha1, std::string realm, AuthStore::Digest* digest)
{
  digest->_ha1 = ha1;
  digest->_impi = _impi;
  digest->_realm = realm;
  digest->_impu = _impu;

  gen_unique_val(32, digest->_nonce);
  gen_unique_val(32, digest->_opaque);
}

// Generate a WWW-Authenticate header. This has the format:
// WWW-Authenticate: Digest realm="<home domain>",
//                          qop="auth",
//                          nonce="<nonce>",
//                          opaque="<opaque>",
//                          [stale=TRUE]
void HTTPDigestAuthenticate::generate_www_auth_header(std::string& www_auth_header, bool include_stale, AuthStore::Digest* digest)
{
  www_auth_header = "Digest";
  www_auth_header.append(" realm=\"").append(_home_domain).append("\"");
  www_auth_header.append(",qop=\"").append("auth").append("\"");
  www_auth_header.append(",nonce=\"").append(digest->_nonce).append("\"");
  www_auth_header.append(",opaque=\"").append(digest->_opaque).append("\"");

  if (include_stale)
  {
    www_auth_header.append(",stale=TRUE");
  }

  TRC_DEBUG("WWW-Authenticate header generated: %s", www_auth_header.c_str());

  TRC_DEBUG("Raising correlating marker with opaque value = %s",
            digest->_opaque.c_str());
  SAS::Marker corr(_trail, MARKED_ID_GENERIC_CORRELATOR, 0);
  corr.add_var_param(digest->_opaque);

  // The marker should be trace-scoped, and should not reactivate any trail
  // groups
  SAS::report_marker(corr, SAS::Marker::Scope::Trace, false);
}

// Set up the member variables (split into separate function for UTs)
void HTTPDigestAuthenticate::set_members(std::string impu,
                                         std::string method,
                                         std::string impi,
                                         SAS::TrailId trail)
{
  _impu = impu;
  _method = method;
  _impi = impi;
  _trail = trail;
}

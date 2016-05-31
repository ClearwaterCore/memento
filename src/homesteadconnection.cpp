/**
 * @file homesteadconnection.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
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

#include "homesteadconnection.h"
#include "mementosasevent.h"
#include <rapidjson/document.h>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

HomesteadConnection::HomesteadConnection(const std::string& server,
                                         HttpResolver* resolver,
                                         LoadMonitor *load_monitor,
                                         CommunicationMonitor* comm_monitor) :
  _http(new HttpConnection(server,
                           false,
                           resolver,
                           NULL,
                           load_monitor,
                           SASEvent::HttpLogLevel::PROTOCOL,
                           comm_monitor))
{
}

HomesteadConnection::~HomesteadConnection()
{
  delete _http; _http = NULL;
}

/// Retrieve user's digest data.
HTTPCode HomesteadConnection::get_digest_data(const std::string& private_user_identity,
                                              const std::string& public_user_identity,
                                              std::string& digest,
                                              std::string& realm,
                                              SAS::TrailId trail)
{
  SAS::Event event(trail, SASEvent::HTTP_HS_DIGEST_LOOKUP, 0);
  event.add_var_param(private_user_identity);
  event.add_var_param(public_user_identity);
  SAS::report_event(event);

  std::string path = "/impi/" +
                     Utils::url_escape(private_user_identity) +
                     "/av?impu=" +
                     Utils::url_escape(public_user_identity);
  HTTPCode rc = get_digest_and_parse(path, digest, realm, trail);

  if (rc != HTTP_OK)
  {
    SAS::Event event(trail, SASEvent::HTTP_HS_DIGEST_LOOKUP_FAILURE, 0);
    event.add_var_param(private_user_identity);
    event.add_var_param(public_user_identity);
    event.add_static_param(rc);
    SAS::report_event(event);
  }
  else
  {
    SAS::Event event(trail, SASEvent::HTTP_HS_DIGEST_LOOKUP_SUCCESS, 0);
    event.add_var_param(private_user_identity);
    event.add_var_param(public_user_identity);
    SAS::report_event(event);
  }

  return rc;
}

/// Parse received digest. This must be valid JSON and have the format:
/// { "digest" : { "ha1": "ha1",
///                "qop": "qop",
///                "realm": "realm" }}
HTTPCode HomesteadConnection::get_digest_and_parse(const std::string& path,
                                                   std::string& digest,
                                                   std::string& realm,
                                                   SAS::TrailId trail)
{
  std::string json_data;
  HTTPCode rc = _http->send_get(path, json_data, "", trail);

  if (rc == HTTP_OK)
  {
    rapidjson::Document doc;
    doc.Parse<0>(json_data.c_str());

    if (doc.HasParseError())
    {
      TRC_WARNING("Failed to parse JSON body %s", json_data.c_str());
      rc = HTTP_BAD_REQUEST;
    }
    else if (!doc.HasMember("digest"))
    {
      TRC_WARNING("Returned Digest is invalid. JSON is: %s", json_data.c_str());
      rc = HTTP_BAD_REQUEST;
    }

    if (rc == HTTP_OK)
    {
      rapidjson::Value& digest_v = doc["digest"];

      if (!digest_v.HasMember("ha1"))
      {
        TRC_WARNING("Returned Digest is invalid. JSON is: %s", json_data.c_str());
        rc = HTTP_BAD_REQUEST;
      }
      else if (!digest_v.HasMember("qop"))
      {
        TRC_WARNING("Returned Digest is invalid. JSON is: %s", json_data.c_str());
        rc = HTTP_BAD_REQUEST;
      }
      else if (std::string(digest_v["qop"].GetString()) != "auth")
      {
        TRC_WARNING("Returned Digest is invalid. QoP isn't auth (%s)", digest_v["qop"].GetString());
        rc = HTTP_BAD_REQUEST;
      }
      else if (!digest_v.HasMember("realm"))
      {
        TRC_WARNING("Returned Digest is invalid. JSON is: %s", json_data.c_str());
        rc = HTTP_BAD_REQUEST;
      }
      else
      {
        digest = digest_v["ha1"].GetString();
        realm = digest_v["realm"].GetString();
      }
    }
  }
  else if (rc == HTTP_SERVER_UNAVAILABLE)
  {
    // Change a 503 response to 504 as we don't want to trigger retries -
    // as httpconnection will already have retried for us.
    rc = HTTP_GATEWAY_TIMEOUT;
  }

  return rc;
}

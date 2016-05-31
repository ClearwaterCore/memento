/**
 * @file handlers.cpp handlers for memento
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

#include "handlers.h"
#include "httpdigestauthenticate.h"
#include "mementosasevent.h"
#include "call_list_store.h"
#include "call_list_xml.h"

// This handler deals with requests to the call list URL
void CallListTask::run()
{
  HTTPCode rc = parse_request();

  if (rc != HTTP_OK)
  {
    send_http_reply(rc);
    delete this;
    return;
  }

  SAS::Marker start_marker(trail(), MARKER_ID_START, 1u);
  SAS::report_marker(start_marker);

  TRC_DEBUG("Parsed Call Lists request. Public ID: %s", _impu.c_str());
  SAS::Event rx_event(trail(), SASEvent::CALL_LIST_REQUEST_RX, 0);
  rx_event.add_var_param(_impu);
  SAS::report_event(rx_event);

  // This will only be usable for SAS if the user is numeric - which
  // may not be true. Still, this is the best we can do at the moment
  // and it matches Sprout's function
  std::string dn = user_from_impu(_impu);
  SAS::Marker calling_dn(trail(), MARKER_ID_CALLING_DN, 1u);
  calling_dn.add_var_param(dn);
  SAS::report_marker(calling_dn);
  SAS::Marker called_dn(trail(), MARKER_ID_CALLED_DN, 1u);
  called_dn.add_var_param(dn);
  SAS::report_marker(called_dn);

  std::string api_key_header = _req.header("NGV-API-Key");
  if (!api_key_header.empty() && api_key_header == _cfg->_api_key)
  {
    TRC_DEBUG("Authenticating using API key");
    respond_when_authenticated();
  }
  else
  {
    std::string www_auth_header;
    std::string auth_header = _req.header("Authorization");
    std::string method = _req.method_as_str();

    rc = _auth_mod->authenticate_request(_impu, auth_header, www_auth_header, method, trail());

    //LCOV_EXCL_START - These cases are tested thoroughly in individual tests
    if (rc == HTTP_UNAUTHORIZED)
    {
      TRC_DEBUG("Authorization data missing or out of date, responding with 401");
      _req.add_header("WWW-Authenticate", www_auth_header);
      send_http_reply(rc);
    }
    else if (rc != HTTP_OK)
    {
      TRC_DEBUG("Authorization failed, responding with %d", rc);
      send_http_reply(rc);
    }
    else
    {
      respond_when_authenticated();
    }
    // LCOV_EXCL_STOP
  }

  SAS::Marker end_marker(trail(), MARKER_ID_END, 1u);
  SAS::report_marker(end_marker);

  delete this;
  return;
}

void CallListTask::respond_when_authenticated()
{
  Utils::StopWatch stop_watch;
  stop_watch.start();

  std::vector<CallListStore::CallFragment> records;
  CassandraStore::ResultCode db_rc =
    _cfg->_call_list_store->get_call_fragments_sync(_impu, records, trail());

  // We know this is a valid subscriber because of authentication, so
  // NOT_FOUND just means they haven't made any calls and should still
  // get a non-error response.
  if ((db_rc != CassandraStore::OK) && (db_rc != CassandraStore::NOT_FOUND))
  {
    SAS::Event db_err_event(trail(), SASEvent::CALL_LIST_DB_RETRIEVAL_FAILED, 0);
    db_err_event.add_var_param(_impu);
    SAS::report_event(db_err_event);

    TRC_DEBUG("get_call_records_sync failed with result code %d", db_rc);
    send_http_reply(HTTP_SERVER_ERROR);
    return;
  }

  // Update the latency statistics.
  unsigned long latency_us = 0;
  if (stop_watch.read(latency_us))
  {
    _cfg->_stat_cassandra_read_latency->accumulate(latency_us);
  }

  SAS::Event db_event(trail(), SASEvent::CALL_LIST_DB_RETRIEVAL_SUCCESS, 0);
  db_event.add_static_param(records.size());
  db_event.add_var_param(_impu);
  SAS::report_event(db_event);

  // Request has authenticated, so attempt to get the call lists.
  std::string calllists = xml_from_call_records(records, trail());
  _req.add_header("Content-Type", "application/vnd.projectclearwater.call-list+xml");
  _req.add_content(calllists);

  // Update statistics about the size and number of records in the result.
  _cfg->_stat_record_size->accumulate(calllists.length());
  _cfg->_stat_record_length->accumulate(records.size());

  SAS::Event tx_event(trail(), SASEvent::CALL_LIST_RSP_TX, 0);
  tx_event.add_var_param(_impu);
  SAS::report_event(tx_event);

  // 200 OK response - we're still active and providing service
  _cfg->_health_checker->health_check_passed();  
  send_http_reply(HTTP_OK);
}

HTTPCode CallListTask::parse_request()
{
  const std::string prefix = "/org.projectclearwater.call-list/users/";
  std::string path = _req.path();

  _impu = path.substr(prefix.length(), path.find_first_of("/", prefix.length()) - prefix.length());

  if (_req.method() != htp_method_GET)
  {
    return HTTP_BADMETHOD;
  }
  return HTTP_OK;
}

std::string CallListTask::user_from_impu(std::string impu)
{
  // Returns the user part of an IMPU (should be a SIP URI). We 
  // use simple string manipulation to pull out the user part (ideally
  // we'd use something like PJSIP for this manipulation, but this isn't
  // used by Memento HTTP). 
  std::size_t is_sip = impu.find("sip:");
  if (is_sip == std::string::npos)
  {
    // We won't be able to search for traces in SAS with this, but it's
    // our best guess to the Calling/Called DNs (it also matches Sprout's
    // behaviour). 
    return impu;  // LCOV_EXCL_LINE
  }
  else
  {
    std::string impi = impu.substr(4);
    std::vector<std::string> uri_parts;
    Utils::split_string(impi, '@', uri_parts, 0, false);
    return uri_parts[0];
  }
}

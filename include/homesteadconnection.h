/**
 * @file homesteadconnection.h
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

#ifndef HOMESTEADCONNECTION_H__
#define HOMESTEADCONNECTION_H__

#include "httpconnection.h"
#include "sas.h"

/// @class HomesteadConnection
///
/// Provides a connection to the Homstead service for retrieving digests
class HomesteadConnection
{
public:
  /// Constructor.
  /// @param server           The homestead cluster name to use.
  /// @param resolver         HTTP resolver to use to query homestead.
  /// @param load_monitor     Load monitor monitoring these requests.
  /// @param stats_aggregator Statistics aggregator.
  /// @param comm_monitor     An optional CommunicatorMonitor object to monitor
  ///                         the state of the connection and reports alarms.
  HomesteadConnection(const std::string& server,
                      HttpResolver* resolver,
                      LoadMonitor *load_monitor,
                      CommunicationMonitor* comm_monitor=NULL);

  /// Destructor
  virtual ~HomesteadConnection();

  /// get_digest_data
  /// @param private_user_identity  A reference to the private user identity.
  /// @param public_user_identity   A reference to the public user identity.
  /// @param digest                 The retrieved digest (as a string)
  /// @param realm                  The retrieved realm (as a string)
  HTTPCode get_digest_data(const std::string& private_user_identity,
                           const std::string& public_user_identity,
                           std::string& digest,
                           std::string& realm,
                           SAS::TrailId trail);
private:
  /// get_digest_and_parse
  /// @param path    The path for the homestead request
  /// @param digest  The retrieved digest (as a string)
  /// @param realm   The retrieved realm (as a string)
  virtual HTTPCode get_digest_and_parse(const std::string& path,
                                        std::string& digest,
                                        std::string& realm,
                                        SAS::TrailId trail);

  /// Pointer to the underlying http connection manager.
  HttpConnection* _http;
};
#endif

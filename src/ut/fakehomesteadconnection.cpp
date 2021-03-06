/**
 * @file fakehomesteadconnection.cpp
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

#include "fakehomesteadconnection.hpp"
#include "gtest/gtest.h"

FakeHomesteadConnection::FakeHomesteadConnection() :
  // Pass NULL in as the HTTP resolver.  This will never get invoked amnyway.
  HomesteadConnection("narcissus", NULL, NULL, NULL)
{
}


FakeHomesteadConnection::~FakeHomesteadConnection()
{
  flush_all();
}


void FakeHomesteadConnection::flush_all()
{
  _results.clear();
  _rcs.clear();
}

void FakeHomesteadConnection::set_result(const std::string& url,
                                         const std::vector<std::string> result)
{
  _results[url] = result;
}

void FakeHomesteadConnection::delete_result(const std::string& url)
{
  _results.erase(url);
}

void FakeHomesteadConnection::set_rc(const std::string& url,
                                     long rc)
{
  _rcs[url] = rc;
}


void FakeHomesteadConnection::delete_rc(const std::string& url)
{
  _rcs.erase(url);
}

long FakeHomesteadConnection::get_digest_and_parse(const std::string& path,
                                                   std::string& digest,
                                                   std::string& realm,
                                                   SAS::TrailId trail)
{
  HTTPCode http_code = HTTP_NOT_FOUND;
  std::map<std::string, std::vector<std::string>>::const_iterator i = _results.find(path);
  if (i != _results.end())
  {
    digest = i->second[0];
    realm = i->second[1];
    http_code = HTTP_OK;
  }

  std::map<std::string, long>::const_iterator i2 = _rcs.find(path);
  if (i2 != _rcs.end())
  {
    http_code = i2->second;
  }

  return http_code;
}

/**
* @file homesteadconnection_test.cpp
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
* A PARTICULAR PURPOSE. See the GNU General Public License for more
* details. You should have received a copy of the GNU General Public
* License along with this program. If not, see
* <http://www.gnu.org/licenses/>.
*
* The author can be reached by email at clearwater@metaswitch.com or by
* post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
*
* Special Exception
* Metaswitch Networks Ltd grants you permission to copy, modify,
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
#include <string>
#include "gtest/gtest.h"

#include "utils.h"
#include "sas.h"
#include "homesteadconnection.h"
#include "fakecurl.hpp"
#include "fakehttpresolver.hpp"
#include "memento_lvc.h"

class HomesteadConnectionTest : public ::testing::Test
{
  FakeHttpResolver _resolver;
  LoadMonitor _lm;
  HomesteadConnection _hc;

  HomesteadConnectionTest():
    _resolver("10.42.42.42"),
    _lm(100000, 20, 10, 10),
    _hc("narcissus", &_resolver, &_lm)
  {
    fakecurl_responses.clear();
    fakecurl_responses["http://10.42.42.42:80/impi/privid1/av?impu=pubid1"] = "{\"digest\":{\"realm\": \"cw-ngv.com\",\"qop\": \"auth\",\"ha1\": \"12345678\"}}";
    fakecurl_responses["http://10.42.42.42:80/impi/privid2/av?impu=pubid2"] = 503;
    fakecurl_responses["http://10.42.42.42:80/impi/privid3/av?impu=pubid3"] = "{\"digest\"{\"realm\": \"cw-ngv.com\",\"qop\": \"auth\",\"ha1\": \"12345678\"}}";
    fakecurl_responses["http://10.42.42.42:80/impi/privid4/av?impu=pubid4"] = "{\"digest1\":{\"realm\": \"cw-ngv.com\",\"qop\": \"auth\",\"ha1\": \"12345678\"}}";
    fakecurl_responses["http://10.42.42.42:80/impi/privid5/av?impu=pubid5"] = "{\"digest\":{\"realm1\": \"cw-ngv.com\",\"qop\": \"auth\",\"ha1\": \"12345678\"}}";
    fakecurl_responses["http://10.42.42.42:80/impi/privid6/av?impu=pubid6"] = "{\"digest\":{\"realm\": \"cw-ngv.com\",\"qop1\": \"auth\",\"ha1\": \"12345678\"}}";
    fakecurl_responses["http://10.42.42.42:80/impi/privid7/av?impu=pubid7"] = "{\"digest\":{\"realm\": \"cw-ngv.com\",\"qop\": \"auth\",\"ha11\": \"12345678\"}}";
    fakecurl_responses["http://10.42.42.42:80/impi/privid8/av?impu=pubid8"] = "{\"digest\":{\"realm\": \"cw-ngv.com\",\"qop\": \"auth1\",\"ha1\": \"12345678\"}}";
 }

  virtual ~HomesteadConnectionTest()
  {
  }
};

// Mainline case - Digest is successfully retrieved
TEST_F(HomesteadConnectionTest, Mainline)
{
  std::string digest;
  std::string realm;
  long rc = _hc.get_digest_data("privid1", "pubid1", digest, realm, 0);
  ASSERT_EQ(rc, 200);
  ASSERT_EQ(digest, "12345678");
}

// Timeout when retrieving the digest. The rc should be converted
// to 504.
TEST_F(HomesteadConnectionTest, DigestTimeout)
{
  std::string digest;
  std::string realm;
  long rc =_hc.get_digest_data("privid2", "pubid2", digest, realm, 0);
  ASSERT_EQ(rc, 504);
  ASSERT_EQ(digest, "");
}

// Retrieved digest is invalid
TEST_F(HomesteadConnectionTest, DigestInvalidJSON)
{
  std::string digest;
  std::string realm;

  // Invalid JSON
  long rc =_hc.get_digest_data("privid3", "pubid3", digest, realm, 0);
  ASSERT_EQ(rc, 400);
  ASSERT_EQ(digest, "");

  // Missing digest
  rc =_hc.get_digest_data("privid4", "pubid4", digest, realm, 0);
  ASSERT_EQ(rc, 400);
  ASSERT_EQ(digest, "");

  // Missing realm
  rc =_hc.get_digest_data("privid5", "pubid5", digest, realm, 0);
  ASSERT_EQ(rc, 400);
  ASSERT_EQ(digest, "");

  // Missing QoP
  rc =_hc.get_digest_data("privid6", "pubid6", digest, realm, 0);
  ASSERT_EQ(rc, 400);
  ASSERT_EQ(digest, "");

  // Missing ha1
  rc =_hc.get_digest_data("privid7", "pubid7", digest, realm, 0);
  ASSERT_EQ(rc, 400);
  ASSERT_EQ(digest, "");

  // QoP isn't auth
  rc =_hc.get_digest_data("privid8", "pubid8", digest, realm, 0);
  ASSERT_EQ(rc, 400);
  ASSERT_EQ(digest, "");
}

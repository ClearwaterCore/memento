/**
 * @file handlers_test.cpp UT for Handlers module.
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
#include "test_utils.hpp"
#include "test_interposer.hpp"

#include "mockhttpstack.hpp"
#include "mock_call_list_store.h"
#include "handlers.h"
#include "localstore.h"
#include "fakehomesteadconnection.hpp"
#include "memento_lvc.h"
#include "mock_health_checker.hpp"

using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArgs;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Mock;

// Fixture for HandlersTest.
class HandlersTest : public testing::Test
{
public:
  static MockHttpStack* _httpstack;
  static LastValueCache* _stats_aggregator;
  LocalStore* _store;
  AuthStore* _auth_store;
  MockCallListStore* _call_store;
  FakeHomesteadConnection* _hc;
  HealthChecker* _health_checker;
  CallListTask::Config* _cfg;

  HandlersTest()
  {
    _store = new LocalStore();
    _auth_store = new AuthStore(_store, 20);
    _call_store = new MockCallListStore();
    _hc = new FakeHomesteadConnection();
    _health_checker = new HealthChecker();
    _cfg = new CallListTask::Config(_auth_store, _hc, _call_store, "localhost", _stats_aggregator, _health_checker, "APIKEY");

  }
  virtual ~HandlersTest()
  {
    delete _health_checker;
    delete _auth_store;
    delete _store;
    delete _call_store;
    delete _hc;
    delete _cfg;
  }

  static void SetUpTestCase()
  {
    _httpstack = new MockHttpStack();
    _stats_aggregator = new MementoLVC(10);  // Short period to reduce shutdown delays.
  }

  static void TearDownTestCase()
  {
    delete _stats_aggregator; _stats_aggregator = NULL;
    delete _httpstack; _httpstack = NULL;
  }
};

MockHttpStack* HandlersTest::_httpstack = NULL;
LastValueCache* HandlersTest::_stats_aggregator = NULL;

// Test the handler creation.
TEST_F(HandlersTest, HandlerCreation)
{
  MockHttpStack::Request req(_httpstack,
                             "/org.projectclearwater.call-list/users/sip:6505551234@home.domain/call-list.xml",
                             "",
                             "");
  CallListTask* handler = new CallListTask(req, _cfg, 0);

  // The request to get the digest from homestead will fail, so
  // the response will be a 404
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));
  handler->run();
}

// Test a request with an invalid method
TEST_F(HandlersTest, HandlerCreationInvalidMethod)
{
  MockHttpStack::Request req(_httpstack,
                             "/org.projectclearwater.call-list/users/sip:6505551234@home.domain/call-list.xml",
                             "",
                             "",
                             "",
                             htp_method_PUT);
  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_httpstack, send_reply(_, 405, _));
  handler->run();
}

TEST_F(HandlersTest, ApiKey)
{
  std::vector<CallListStore::CallFragment> records;
  MockHttpStack::Request req(_httpstack,
                             "/org.projectclearwater.call-list/users/sip:6505551234@home.domain/call-list.xml",
                             "",
                             "");
  req.add_header_to_incoming_req("NGV-API-Key", "APIKEY");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(DoAll(SetArgReferee<1>(records), Return(CassandraStore::ResultCode::OK)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->run();

  EXPECT_EQ("<call-list><calls></calls></call-list>", req.content());
}

TEST_F(HandlersTest, InvalidApiKey)
{
  std::vector<CallListStore::CallFragment> records;
  MockHttpStack::Request req(_httpstack,
                             "/org.projectclearwater.call-list/users/sip:6505551234@home.domain/call-list.xml",
                             "",
                             "");
  req.add_header_to_incoming_req("NGV-API-Key", "INVALID-APIKEY");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));
  handler->run();
}

TEST_F(HandlersTest, EmptyApiKey)
{
  std::vector<CallListStore::CallFragment> records;
  CallListTask::Config cfg(_auth_store, _hc, _call_store, "localhost", _stats_aggregator, _health_checker, "");
  MockHttpStack::Request req(_httpstack,
                             "/org.projectclearwater.call-list/users/sip:6505551234@home.domain/call-list.xml",
                             "",
                             "");
  req.add_header_to_incoming_req("NGV-API-Key", "");

  CallListTask* handler = new CallListTask(req, &cfg, 0);

  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));
  handler->run();
}

TEST_F(HandlersTest, Mainline)
{
  std::vector<CallListStore::CallFragment> records;
  CallListStore::CallFragment record1;
  CallListStore::CallFragment record2;
  CallListStore::CallFragment record3;
  CallListStore::CallFragment record4;
  record1.type = CallListStore::CallFragment::Type::BEGIN;
  record1.id = "a";
  record1.contents = (
    "<to>"
      "<URI>alice@example.com</URI>"
      "<name>Alice Adams</name>"
    "</to>"
    "<from>"
      "<URI>bob@example.com</URI>"
      "<name>Bob Barker</name>"
    "</from>"
    "<answered>1</answered>"
    "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:10</start-time>"
    "<answer-time>2002-05-30T09:30:20</answer-time>");
  record2.type = CallListStore::CallFragment::Type::END;
  record2.id = "a";
  record2.contents = "<end-time>2002-05-30T09:35:00</end-time>";
  record3.type = CallListStore::CallFragment::Type::REJECTED;
  record3.id = "b";
  record3.contents = (
    "<to>"
      "<URI>alice@example.net</URI>"
      "<name>Alice Adams</name>"
    "</to>"
    "<from>"
      "<URI>bob@example.net</URI>"
      "<name>Bob Barker</name>"
    "</from>"
    "<answered>0</answered>"
    "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:10</start-time>");
  records.push_back(record1);
  records.push_back(record3);
  records.push_back(record2);
  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(DoAll(SetArgReferee<1>(records), Return(CassandraStore::ResultCode::OK)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->respond_when_authenticated();

  EXPECT_EQ((
              "<call-list><calls>"
                "<call>"
                  "<to>"
                    "<URI>alice@example.com</URI>"
                    "<name>Alice Adams</name>"
                  "</to>"
                  "<from>"
                    "<URI>bob@example.com</URI>"
                    "<name>Bob Barker</name>"
                  "</from>"
                  "<answered>1</answered>"
                  "<outgoing>1</outgoing>"
                  "<start-time>2002-05-30T09:30:10</start-time>"
                  "<answer-time>2002-05-30T09:30:20</answer-time>"
                  "<end-time>2002-05-30T09:35:00</end-time>"
                "</call>"
                "<call>"
                  "<to>"
                    "<URI>alice@example.net</URI>"
                    "<name>Alice Adams</name>"
                  "</to>"
                  "<from>"
                    "<URI>bob@example.net</URI>"
                    "<name>Bob Barker</name>"
                  "</from>"
                  "<answered>0</answered>"
                  "<outgoing>1</outgoing>"
                  "<start-time>2002-05-30T09:30:10</start-time>"
                "</call>"
              "</calls></call-list>"),
            req.content());
  delete handler;
}

TEST_F(HandlersTest, DuplicatedBegin)
{
  std::vector<CallListStore::CallFragment> records;
  CallListStore::CallFragment record1;
  CallListStore::CallFragment record2;
  record1.type = CallListStore::CallFragment::Type::BEGIN;
  record1.id = "a";
  record1.contents = (
    "<to>"
        "<URI>alice@example.com</URI>"
        "<name>Alice Adams</name>"
      "</to>"
      "<from>"
        "<URI>bob@example.com</URI>"
        "<name>Bob Barker</name>"
      "</from>"
      "<answered>1</answered>"
      "<outgoing>1</outgoing>"
      "<start-time>2002-05-30T09:30:10</start-time>"
    "<answer-time>2002-05-30T09:30:20</answer-time>");
  record2.type = CallListStore::CallFragment::Type::END;
  record2.id = "a";
  record2.contents = "<end-time>2002-05-30T09:35:00</end-time>";
  records.push_back(record1);
  records.push_back(record1);
  records.push_back(record2);
  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(DoAll(SetArgReferee<1>(records), Return(CassandraStore::ResultCode::OK)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->respond_when_authenticated();

  // Invalid records are ignored
  EXPECT_EQ( ("<call-list><calls>"
    "</calls></call-list>")
    , req.content());
  delete handler;
}

TEST_F(HandlersTest, DuplicatedEnd)
{
  std::vector<CallListStore::CallFragment> records;
  CallListStore::CallFragment record1;
  CallListStore::CallFragment record2;
  record1.type = CallListStore::CallFragment::Type::BEGIN;
  record1.id = "a";
  record1.contents = (
    "<to>"
        "<URI>alice@example.com</URI>"
        "<name>Alice Adams</name>"
      "</to>"
      "<from>"
        "<URI>bob@example.com</URI>"
        "<name>Bob Barker</name>"
      "</from>"
      "<answered>1</answered>"
      "<outgoing>1</outgoing>"
      "<start-time>2002-05-30T09:30:10</start-time>"
    "<answer-time>2002-05-30T09:30:20</answer-time>");
  record2.type = CallListStore::CallFragment::Type::END;
  record2.id = "a";
  record2.contents = "<end-time>2002-05-30T09:35:00</end-time>";
  records.push_back(record1);
  records.push_back(record2);
  records.push_back(record2);
  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(DoAll(SetArgReferee<1>(records), Return(CassandraStore::ResultCode::OK)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->respond_when_authenticated();

  EXPECT_EQ(("<call-list><calls>"
    "</calls></call-list>")
    , req.content());

  delete handler;
}

TEST_F(HandlersTest, DuplicatedRejected)
{
  std::vector<CallListStore::CallFragment> records;
  CallListStore::CallFragment record1;
  CallListStore::CallFragment record2;
  record1.type = CallListStore::CallFragment::Type::REJECTED;
  record1.id = "a";
  record1.contents = (
    "<to>"
        "<URI>alice@example.com</URI>"
        "<name>Alice Adams</name>"
      "</to>"
      "<from>"
        "<URI>bob@example.com</URI>"
        "<name>Bob Barker</name>"
      "</from>"
      "<answered>0</answered>"
      "<outgoing>1</outgoing>"
    "<start-time>2002-05-30T09:30:10</start-time>");
  records.push_back(record1);
  records.push_back(record1);
  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(DoAll(SetArgReferee<1>(records), Return(CassandraStore::ResultCode::OK)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->respond_when_authenticated();

  EXPECT_EQ(("<call-list><calls>"
             "</calls></call-list>"), req.content());

  delete handler;
}


TEST_F(HandlersTest, WrongOrder)
{
  std::vector<CallListStore::CallFragment> records;
  CallListStore::CallFragment record1;
  CallListStore::CallFragment record2;
  record1.type = CallListStore::CallFragment::Type::BEGIN;
  record1.id = "a";
  record1.contents = (
    "<to>"
        "<URI>alice@example.com</URI>"
        "<name>Alice Adams</name>"
      "</to>"
      "<from>"
        "<URI>bob@example.com</URI>"
        "<name>Bob Barker</name>"
      "</from>"
      "<answered>1</answered>"
      "<outgoing>1</outgoing>"
      "<start-time>2002-05-30T09:30:10</start-time>"
    "<answer-time>2002-05-30T09:30:20</answer-time>");
  record2.type = CallListStore::CallFragment::Type::END;
  record2.id = "a";
  record2.contents = "<end-time>2002-05-30T09:35:00</end-time>";
  records.push_back(record2);
  records.push_back(record1);
  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(DoAll(SetArgReferee<1>(records), Return(CassandraStore::ResultCode::OK)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->respond_when_authenticated();

  EXPECT_EQ(("<call-list><calls>"
             "</calls></call-list>")
            , req.content());
  delete handler;
}

TEST_F(HandlersTest, MissingEnd)
{
  std::vector<CallListStore::CallFragment> records;
  CallListStore::CallFragment record1;
  record1.type = CallListStore::CallFragment::Type::BEGIN;
  record1.id = "a";
  record1.contents = (
    "<to>"
        "<URI>alice@example.com</URI>"
        "<name>Alice Adams</name>"
      "</to>"
      "<from>"
        "<URI>bob@example.com</URI>"
        "<name>Bob Barker</name>"
      "</from>"
      "<answered>1</answered>"
      "<outgoing>1</outgoing>"
      "<start-time>2002-05-30T09:30:10</start-time>"
    "<answer-time>2002-05-30T09:30:20</answer-time>");
  records.push_back(record1);
  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(DoAll(SetArgReferee<1>(records), Return(CassandraStore::ResultCode::OK)));

  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->respond_when_authenticated();

  EXPECT_EQ("<call-list><calls></calls></call-list>", req.content());
  delete handler;
}

TEST_F(HandlersTest, NotFound)
{
  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(Return(CassandraStore::ResultCode::NOT_FOUND));

  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->respond_when_authenticated();

  EXPECT_EQ("<call-list><calls></calls></call-list>", req.content());
  delete handler;
}

TEST_F(HandlersTest, DbError)
{
  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, _cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(Return(CassandraStore::ResultCode::RESOURCE_ERROR));

  EXPECT_CALL(*_httpstack, send_reply(_, 500, _));
  handler->respond_when_authenticated();

  EXPECT_EQ("", req.content());
  delete handler;
}

TEST_F(HandlersTest, HTTPOKPassesHealthCheck)
{
  MockHealthChecker mock_health_checker;
  CallListTask::Config cfg(_auth_store, _hc, _call_store, "localhost", _stats_aggregator, &mock_health_checker, "APIKEY");

  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, &cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(Return(CassandraStore::ResultCode::NOT_FOUND));

  EXPECT_CALL(mock_health_checker, health_check_passed()).Times(1);
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  handler->respond_when_authenticated();

  delete handler;
}

TEST_F(HandlersTest, HTTPErrorFailsHealthCheck)
{
  MockHealthChecker mock_health_checker;
  CallListTask::Config cfg(_auth_store, _hc, _call_store, "localhost", _stats_aggregator, &mock_health_checker, "APIKEY");

  MockHttpStack::Request req(_httpstack, "/", "", "");

  CallListTask* handler = new CallListTask(req, &cfg, 0);

  EXPECT_CALL(*_call_store, get_call_fragments_sync(_, _, _))
    .WillOnce(Return(CassandraStore::ResultCode::RESOURCE_ERROR));

  EXPECT_CALL(mock_health_checker, health_check_passed()).Times(0);
  EXPECT_CALL(*_httpstack, send_reply(_, 500, _));
  handler->respond_when_authenticated();

  delete handler;
}


/**
 * @file call_list_store_test.cpp Call list store unit tests
 *
 * Project Clearwater - IMS in the cloud.
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

#ifndef CALL_LIST_STORE_TEST_CPP_
#define CALL_LIST_STORE_TEST_CPP_

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "mock_cassandra_store.h"
#include "cass_test_utils.h"
#include "mock_sas.h"
#include "mock_cassandra_connection_pool.h"
#include "mock_a_record_resolver.h"
#include "fake_base_addr_iterator.h"

#include "call_list_store.h"
#include "mementosasevent.h"

using namespace CassTestUtils;

const SAS::TrailId FAKE_TRAIL = 0x123456;

// The class under test.
//
// We don't test the Cache class directly as we need to use a
// MockCassandraConnectionPool that we can use to return MockCassandraClients.
// However all other methods are the real ones from Cache.
class TestCallListStore : public CallListStore::Store
{
public:
  void set_conn_pool(CassandraStore::CassandraConnectionPool* pool)
  {
    delete _conn_pool;
    _conn_pool = pool;
  }
};

class CallListStoreFixture : public ::testing::Test
{
public:
  CallListStoreFixture()
  {
    _iter = new FakeBaseAddrIterator(create_target("10.0.0.1"));

    // This passes ownership of the pool to the _store
    MockCassandraConnectionPool* pool = new MockCassandraConnectionPool();
    _store.set_conn_pool(pool);

    _store.configure_connection("localhost", 1234, NULL, &_resolver);

    // Just return a fake iterator every time
    EXPECT_CALL(_resolver, resolve_iter(_,_,_)).WillRepeatedly(Return(_iter));

    // The pool should just repeatedly return _client
    EXPECT_CALL(*pool, get_client()).Times(testing::AnyNumber()).WillRepeatedly(Return(&_client));

    // Not interested in the resolver success calls
    EXPECT_CALL(_resolver, success(_)).Times(testing::AnyNumber());

    // We expect connect(), is_connected() and set_keyspace() to be called in
    // every test. By default, just mock them out so that we don't get warnings.
    EXPECT_CALL(_client, set_keyspace(_)).Times(testing::AnyNumber());
    EXPECT_CALL(_client, connect()).Times(testing::AnyNumber());
    EXPECT_CALL(_client, is_connected()).Times(testing::AnyNumber()).WillRepeatedly(Return(false));

    CassandraStore::ResultCode rc = _store.start();
    EXPECT_EQ(rc, CassandraStore::OK);
  }

  virtual ~CallListStoreFixture()
  {
    _store.stop();
    _store.wait_stopped();
    delete _iter; _iter = NULL;
  }

  AddrInfo create_target(std::string address)
  {
    AddrInfo ai;
    BaseResolver::parse_ip_target(address, ai.address);
    ai.port = 1;
    ai.transport = IPPROTO_TCP;
    return ai;
  }

  TestCallListStore _store;
  MockCassandraClient _client;
  MockCassandraResolver _resolver;
  FakeBaseAddrIterator* _iter;
};

//
// TESTS
//
TEST_F(CallListStoreFixture, WriteFragmentMainline)
{
  const std::string CALL_TIMESTAMP = "20140723150400";
  const std::string ID = "0123456789ABCDEF";
  const std::string CONTENT = "<xml>";
  const std::string EXPECT_COL_PREFIX = "call_20140723150400_0123456789ABCDEF_";
  CassandraStore::ResultCode rc;

  CallListStore::CallFragment frag;
  frag.timestamp = CALL_TIMESTAMP;
  frag.id = ID;
  frag.contents = CONTENT;

  std::map<std::string, std::string> columns;

  // Test that the store can write begin records.
  columns.clear();
  columns[EXPECT_COL_PREFIX + "begin"] = "<xml>";
  frag.type = CallListStore::CallFragment::BEGIN;

  EXPECT_CALL(_client, batch_mutate(
                         MutationMap("call_lists", "kermit", columns, 1000, 3600),
                         _));
  rc = _store.write_call_fragment_sync("kermit", frag, 1000, 3600, FAKE_TRAIL);
  EXPECT_EQ(rc, CassandraStore::OK);

  // ... and end records ...
  columns.clear();
  columns[EXPECT_COL_PREFIX + "end"] = "<xml>";
  frag.type = CallListStore::CallFragment::END;

  EXPECT_CALL(_client, batch_mutate(
                         MutationMap("call_lists", "kermit", columns, 1000, 3600),
                         _));
  rc = _store.write_call_fragment_sync("kermit", frag, 1000, 3600, FAKE_TRAIL);
  EXPECT_EQ(rc, CassandraStore::OK);

  // ... and rejected records.
  columns.clear();
  columns[EXPECT_COL_PREFIX + "rejected"] = "<xml>";
  frag.type = CallListStore::CallFragment::REJECTED;

  EXPECT_CALL(_client, batch_mutate(
                         MutationMap("call_lists", "kermit", columns, 1000, 3600),
                         _));
  rc = _store.write_call_fragment_sync("kermit", frag, 1000, 3600, FAKE_TRAIL);
  EXPECT_EQ(rc, CassandraStore::OK);
}


TEST_F(CallListStoreFixture, WriteFragmentError)
{
  CassandraStore::ResultCode rc;

  CallListStore::CallFragment frag;
  frag.timestamp = "20140101130101";
  frag.id = "0123456789ABCDEF";
  frag.type = CallListStore::CallFragment::BEGIN;
  frag.contents = "<xml>";

  cass::InvalidRequestException ire;
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ire));

  rc = _store.write_call_fragment_sync("kermit", frag, 1000, 3600, FAKE_TRAIL);
  EXPECT_EQ(rc, CassandraStore::INVALID_REQUEST);
}


TEST_F(CallListStoreFixture, GetFragmentsMainline)
{
  // Make a slice to return to the store.
  std::map<std::string, std::string> columns;
  columns["call_20140101130100_0000000000000000_begin"] = "<begin-record>";
  columns["call_20140101130100_0000000000000000_end"] = "<end-record>";
  columns["call_20140101130100_0000000000000001_rejected"] = "<rejected-record>";

  slice_t slice;
  make_slice(slice, columns);

  // The call fragments retrieved by the store.
  std::vector<CallListStore::CallFragment> fetched_fragments;

  // Expect the store to request a slice from cassandra, and return the one we
  // made earlier.
  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("call_lists"),
                                 ColumnsWithPrefix("call_"),
                                 _))
   .WillOnce(SetArgReferee<0>(slice));

  // Now actually invoke the store.
  CassandraStore::ResultCode rc =
    _store.get_call_fragments_sync("kermit", fetched_fragments, FAKE_TRAIL);

  // Check that worked.
  EXPECT_EQ(rc, CassandraStore::OK);

  // We should have all 3 records back.
  EXPECT_EQ(fetched_fragments.size(), 3u);

  // Check the 3 records that came back.  Note that the records are returned to
  // the user in the same order that they were returned from cassandra.
  EXPECT_EQ(fetched_fragments[0].timestamp, "20140101130100");
  EXPECT_EQ(fetched_fragments[0].id, "0000000000000000");
  EXPECT_EQ(fetched_fragments[0].type, CallListStore::CallFragment::BEGIN);
  EXPECT_EQ(fetched_fragments[0].contents, "<begin-record>");

  EXPECT_EQ(fetched_fragments[1].timestamp, "20140101130100");
  EXPECT_EQ(fetched_fragments[1].id, "0000000000000000");
  EXPECT_EQ(fetched_fragments[1].type, CallListStore::CallFragment::END);
  EXPECT_EQ(fetched_fragments[1].contents, "<end-record>");

  EXPECT_EQ(fetched_fragments[2].timestamp, "20140101130100");
  EXPECT_EQ(fetched_fragments[2].id, "0000000000000001");
  EXPECT_EQ(fetched_fragments[2].type, CallListStore::CallFragment::REJECTED);
  EXPECT_EQ(fetched_fragments[2].contents, "<rejected-record>");
}


TEST_F(CallListStoreFixture, GetFragmentsError)
{
  std::vector<CallListStore::CallFragment> fetched_fragments;

  CassandraStore::RowNotFoundException rnfe("call_lists", "kermit");
  EXPECT_CALL(_client, get_slice(_, _, _, _, _)).WillOnce(Throw(rnfe));

  CassandraStore::ResultCode rc =
    _store.get_call_fragments_sync("kermit", fetched_fragments, FAKE_TRAIL);
  EXPECT_EQ(rc, CassandraStore::NOT_FOUND);
}


// Check that if an empty slice is returned from cassandra that this is treated
// as a not found error.
TEST_F(CallListStoreFixture, EmptySlice)
{
  std::vector<CallListStore::CallFragment> fetched_fragments;

  EXPECT_CALL(_client, get_slice(_,
                                 "kermit",
                                 ColumnPathForTable("call_lists"),
                                 ColumnsWithPrefix("call_"),
                                 _))
   .WillOnce(SetArgReferee<0>(empty_slice));

  // Now actually invoke the store.
  CassandraStore::ResultCode rc =
    _store.get_call_fragments_sync("kermit", fetched_fragments, FAKE_TRAIL);

  // Check that returned "not found"
  EXPECT_EQ(rc, CassandraStore::NOT_FOUND);
}


TEST_F(CallListStoreFixture, DeleteOldFragmentsMainline)
{
  CallListStore::CallFragment record;
  record.type = CallListStore::CallFragment::Type::REJECTED;
  record.timestamp = "20020530093010";
  record.id = "a";
  record.contents = "contents";
  std::vector<CallListStore::CallFragment> fragments;
  fragments.push_back(record);

  std::map<std::string, std::string> deleted_columns;
  deleted_columns["call_20020530093010_a_rejected"] = "";

  std::vector<CassandraStore::RowColumns> expected;
  expected.push_back(CassandraStore::RowColumns("call_lists", "kermit", deleted_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));

  CassandraStore::ResultCode rc =
    _store.delete_old_call_fragments_sync("kermit", fragments, 1000, FAKE_TRAIL);

  EXPECT_EQ(rc, CassandraStore::OK);
}


TEST_F(CallListStoreFixture, DeleteOldFragmentsError)
{
  cass::InvalidRequestException ire;
  CallListStore::CallFragment record;
  record.type = CallListStore::CallFragment::Type::REJECTED;
  record.timestamp = "20020530093010";
  record.id = "a";
  record.contents = "contents";
  std::vector<CallListStore::CallFragment> fragments;
  fragments.push_back(record);

  std::map<std::string, std::string> deleted_columns;
  deleted_columns["call_20020530093010_a_rejected"] = "";

  std::vector<CassandraStore::RowColumns> expected;
  expected.push_back(CassandraStore::RowColumns("call_lists", "kermit", deleted_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _)).WillOnce(Throw(ire));

  CassandraStore::ResultCode rc =
    _store.delete_old_call_fragments_sync("kermit", fragments, 1000, FAKE_TRAIL);
  EXPECT_EQ(rc, CassandraStore::INVALID_REQUEST);
}


TEST_F(CallListStoreFixture, SasLogging)
{
  mock_sas_collect_messages(true);

  // Call list fragment for writing.
  CallListStore::CallFragment frag;
  frag.timestamp = "20140101130101";
  frag.id = "0123456789ABCDEF";
  frag.type = CallListStore::CallFragment::BEGIN;
  frag.contents = "<xml>";

  // Column to return when getting fragments.
  std::map<std::string, std::string> columns;
  columns["call_20140101130100_0000000000000000_begin"] = "<begin-record>";
  slice_t slice;
  make_slice(slice, columns);

  // Exception used to trigger errors.
  cass::InvalidRequestException ire;

  std::vector<CallListStore::CallFragment> fetched_fragments;

  //
  // TESTS START HERE
  //

  // Write a fragment. Check we get start and OK events.
  EXPECT_CALL(_client, batch_mutate(_, _));
  _store.write_call_fragment_sync("kermit", frag, 1000, 3600, FAKE_TRAIL);

  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_WRITE_STARTED);
  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_WRITE_OK);
  EXPECT_NO_SAS_EVENT(SASEvent::CALL_LIST_WRITE_FAILED);

  mock_sas_discard_messages();

  // Failing to write a fragment.  Check we get start and failed events.
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ire));
  _store.write_call_fragment_sync("kermit", frag, 1000, 3600, FAKE_TRAIL);

  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_WRITE_STARTED);
  EXPECT_NO_SAS_EVENT(SASEvent::CALL_LIST_WRITE_OK);
  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_WRITE_FAILED);

  mock_sas_discard_messages();

  // Get some fragments, check we get start and OK events.
  EXPECT_CALL(_client, get_slice(_, _, _, _, _)).WillOnce(SetArgReferee<0>(slice));
  _store.get_call_fragments_sync("kermit", fetched_fragments, FAKE_TRAIL);

  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_READ_STARTED);
  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_READ_OK);
  EXPECT_NO_SAS_EVENT(SASEvent::CALL_LIST_READ_FAILED);

  mock_sas_discard_messages();

  // Fail to get any fragments, check we get start and failed events.
  EXPECT_CALL(_client, get_slice(_, _, _, _, _)).WillOnce(SetArgReferee<0>(empty_slice));
  _store.get_call_fragments_sync("kermit", fetched_fragments, FAKE_TRAIL);

  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_READ_STARTED);
  EXPECT_NO_SAS_EVENT(SASEvent::CALL_LIST_READ_OK);
  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_READ_FAILED);

  mock_sas_discard_messages();

  // Delete old fragments. Check we get start and OK events.
  CallListStore::CallFragment record;
  record.type = CallListStore::CallFragment::Type::REJECTED;
  record.timestamp = "20020530093010";
  record.id = "a";
  record.contents = "contents";
  std::vector<CallListStore::CallFragment> fragments;
  fragments.push_back(record);

  std::map<std::string, std::string> deleted_columns;
  deleted_columns["call_20020530093010_a_rejected"] = "";

  std::vector<CassandraStore::RowColumns> expected;
  expected.push_back(CassandraStore::RowColumns("call_lists", "kermit", deleted_columns));

  EXPECT_CALL(_client, batch_mutate(DeletionMap(expected), _));
  _store.delete_old_call_fragments_sync("kermit", fragments, 1000, FAKE_TRAIL);

  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_TRIM_STARTED);
  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_TRIM_OK);
  EXPECT_NO_SAS_EVENT(SASEvent::CALL_LIST_TRIM_FAILED);

  mock_sas_discard_messages();

  // Fail to delete old fragments. Check we get start and failure events.
  EXPECT_CALL(_client, batch_mutate(_, _)).WillOnce(Throw(ire));
  _store.delete_old_call_fragments_sync("kermit", fragments, 1000, FAKE_TRAIL);

  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_TRIM_STARTED);
  EXPECT_NO_SAS_EVENT(SASEvent::CALL_LIST_TRIM_OK);
  EXPECT_SAS_EVENT(SASEvent::CALL_LIST_TRIM_FAILED);

  mock_sas_discard_messages();

  mock_sas_collect_messages(false);
}

#endif


/**
 * @file call_list_store.cpp Memento call list store implementation.
 *
 * Project Clearwater - IMS in the cloud.
 * Copyright (C) 2013  Metaswitch Networks Ltd
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

#include "call_list_store.h"
#include "mementosasevent.h"

// The keyspace that that call list store uses.
const static std::string KEYSPACE = "memento";

// The column family (table) that the call list store uses.
const static std::string COLUMN_FAMILY = "call_lists";

// String representations of the idfferent call fragment types. These are
// encoded into the column names in the `call_lists` column family.
const static std::string STR_BEGIN = "begin";
const static std::string STR_END = "end";
const static std::string STR_REJECTED = "rejected";

// All call list fragements begin with this prefix. This is to allow the column
// family to be easily extended to contain different types of columns in future
// (e.g. metatdata related to the call list).
const static std::string CALL_COLUMN_PREFIX = "call_";

namespace CallListStore
{

namespace cass = org::apache::cassandra;

/// Utility method for converting a call fragment type to the string
/// representation used in cassandra.
///
/// @param type    - The type of fragment.
/// @return        - The string representation.
std::string fragment_type_to_string(CallFragment::Type type)
{
  switch(type)
  {
    case CallFragment::BEGIN:
      return STR_BEGIN;

    case CallFragment::END:
      return STR_END;

    case CallFragment::REJECTED:
      return STR_REJECTED;

    default:
      // LCOV_EXCL_START - We should never reach this code.  The function must
      // be passed a value in the enumeration, and we have already handled all
      // of them.
      LOG_ERROR("Unexpected call fragment type %d", (int)type);
      return (std::string("UNKNOWN (") + std::to_string(type) + std::string(")"));
      // LCOV_EXCL_STOP
  }
}

// Utility method for converting a call fragment string (as stored in
// cassandra) into an enumerated type.
//
// @param fragment_str    - The string to convert.
// @param type            - (out) The type of the fragment.
//
// @return                - True of the string was converted successfully, false
//                          if the string is not recognized.
bool fragment_type_from_string(const std::string& fragment_str,
                               CallFragment::Type& type)
{
  bool success = true;

  if (fragment_str == STR_BEGIN)
  {
    type = CallFragment::BEGIN;
  }
  else if (fragment_str == STR_END)
  {
    type = CallFragment::END;
  }
  else if (fragment_str == STR_REJECTED)
  {
    type = CallFragment::REJECTED;
  }
  else
  {
    // LCOV_EXCL_START Should not hit this as the call list store does not
    // write values with a unrecognized fragment type string.
    success = false;
    // LCOV_EXCL_STOP
  }

  return success;
}

void sas_log_cassandra_failure(const SAS::TrailId trail,
                               const int event_id,
                               const CassandraStore:: ResultCode status,
                               const std::string& description,
                               uint32_t instance_id = 0)
{
  SAS::Event ev(trail, event_id, instance_id);
  ev.add_static_param(status);
  ev.add_var_param(description);
  SAS::report_event(ev);
}

//
// Call list store methods.
//

Store::Store() : CassandraStore::Store(KEYSPACE) {}

Store::~Store() {}

//
// Operation definitions.
//

//
// Write a new call fragment to cassandra.
//

WriteCallFragment::WriteCallFragment(const std::string& impu,
                                     const CallFragment& fragment,
                                     const int64_t cass_timestamp,
                                     const int32_t ttl) :
  CassandraStore::Operation(),
  _impu(impu),
  _fragment(fragment),
  _cass_timestamp(cass_timestamp),
  _ttl(ttl)
{}

WriteCallFragment::~WriteCallFragment()
{}

bool WriteCallFragment::perform(CassandraStore::ClientInterface* client,
                                SAS::TrailId trail)
{
  // Log the start of the write.
  LOG_DEBUG("Writing %s call fragment for IMPU '%s'",
            fragment_type_to_string(_fragment.type).c_str(),
            _impu.c_str());

  { // New scope to avoid accidentally operating on the wrong SAS event.
    SAS::Event ev(trail, SASEvent::CALL_LIST_WRITE_STARTED, 0);
    ev.add_static_param(_fragment.type);
    ev.add_var_param(_impu);
    ev.add_var_param(_fragment.timestamp);
    ev.add_compressed_param(_fragment.contents);
    SAS::report_event(ev);
  }

  // The column name is of the form:
  //   call_<timestamp>_<id>_<type>
  //
  // For example:
  //   call_20140722120000_12345_begin
  std::string column_name;
  column_name.append(CALL_COLUMN_PREFIX)
             .append(_fragment.timestamp).append("_")
             .append(_fragment.id).append("_")
             .append(fragment_type_to_string(_fragment.type));

  // Map describing the columns to write.
  std::map<std::string, std::string> columns;
  columns[column_name] = _fragment.contents;

  // Write to the supplied impu only.
  std::vector<std::string> keys;
  keys.push_back(_impu);

  put_columns(client,
              COLUMN_FAMILY,
              keys,
              columns,
              _cass_timestamp,
              _ttl);

  { // New scope to avoid accidentally operating on the wrong SAS event.
    SAS::Event ev(trail, SASEvent::CALL_LIST_WRITE_OK, 0);
    SAS::report_event(ev);
  }

  return true;
}

void WriteCallFragment::unhandled_exception(CassandraStore:: ResultCode status,
                                            std::string& description,
                                            SAS::TrailId trail)
{
  CassandraStore::Operation::unhandled_exception(status, description, trail);

  LOG_WARNING("Failed to write call list fragment for IMPU %s because '%s' (RC = %d)",
              _impu.c_str(), description.c_str(), status);
  sas_log_cassandra_failure(trail,
                            SASEvent::CALL_LIST_WRITE_FAILED,
                            status,
                            description);
}

WriteCallFragment*
Store::new_write_call_fragment_op(const std::string& impu,
                                  const CallFragment& fragment,
                                  const int64_t cass_timestamp,
                                  const int32_t ttl)
{
  return new WriteCallFragment(impu, fragment, cass_timestamp, ttl);
}

//
// Get all the call fragments for a given IMPU.
//

GetCallFragments::GetCallFragments(const std::string& impu) :
  CassandraStore::Operation(), _impu(impu), _fragments()
{}

GetCallFragments::~GetCallFragments()
{}

bool GetCallFragments::perform(CassandraStore::ClientInterface* client,
                               SAS::TrailId trail)
{
  // Log the start of the read
  LOG_DEBUG("Get call fragments for IMPU: '%s'", _impu.c_str());

  { // New scope to avoid accidentally operating on the wrong SAS event.
    SAS::Event ev(trail, SASEvent::CALL_LIST_READ_STARTED, 0);
    ev.add_var_param(_impu);
    SAS::report_event(ev);
  }

  // Get all the call columns for the IMPU's cassandra row.
  std::vector<cass::ColumnOrSuperColumn> columns;
  ha_get_columns_with_prefix(client,
                             COLUMN_FAMILY,
                             _impu,
                             CALL_COLUMN_PREFIX,
                             columns,
                             trail);

  for(std::vector<cass::ColumnOrSuperColumn>::const_iterator column_it = columns.begin();
      column_it != columns.end();
      ++column_it)
  {
    // The columns name is of the form call_<timestamp>_<id>_<type>. The call_
    // prefix has already been stripped off, so just split the remaining string
    // on underscores and access the elements.
    std::vector<std::string> tokens;
    Utils::split_string(column_it->column.name, '_', tokens);

    std::string& timestamp_str = tokens[0];
    std::string& id_str = tokens[1];
    std::string& type_str = tokens[2];
    CallFragment::Type type;
    fragment_type_from_string(type_str, type);

    // Now form a call fragment and add it to the output of the operation.
    CallFragment fragment;
    fragment.timestamp = timestamp_str;
    fragment.id = id_str;
    fragment.type = type;
    fragment.contents = column_it->column.value;

    _fragments.push_back(fragment);
  }

  LOG_DEBUG("Retrieved %d call fragments from the store", _fragments.size());

  { // New scope to avoid accidentally operating on the wrong SAS event.
    SAS::Event ev(trail, SASEvent::CALL_LIST_READ_OK, 0);
    ev.add_static_param(_fragments.size());
    ev.add_var_param(columns.front().column.name);
    ev.add_var_param(columns.back().column.name);
    SAS::report_event(ev);
  }

  return true;
}

void GetCallFragments::unhandled_exception(CassandraStore::ResultCode status,
                                           std::string& description,
                                           SAS::TrailId trail)
{
  CassandraStore::Operation::unhandled_exception(status, description, trail);

  LOG_WARNING("Failed to get call list fragments for IMPU %s because '%s' (RC = %d)",
              _impu.c_str(), description.c_str(), status);
  sas_log_cassandra_failure(trail,
                            SASEvent::CALL_LIST_READ_FAILED,
                            status,
                            description);
}

void GetCallFragments::get_result(std::vector<CallFragment>& fragments)
{
  fragments = _fragments;
}

GetCallFragments*
Store::new_get_call_fragments_op(const std::string& impu)
{
  return new GetCallFragments(impu);
}

//
// Delete old call fragments for the givem IMPU.
//

DeleteOldCallFragments::DeleteOldCallFragments(const std::string& impu,
                                               const std::vector<CallFragment> fragments,
                                               const int64_t cass_timestamp) :
  CassandraStore::Operation(),
  _impu(impu),
  _fragments(fragments),
  _cass_timestamp(cass_timestamp)
{}

DeleteOldCallFragments::~DeleteOldCallFragments()
{}

bool DeleteOldCallFragments::perform(CassandraStore::ClientInterface* client,
                                     SAS::TrailId trail)
{
  LOG_DEBUG("Deleting %d call fragments for IMPU '%s'",
            _fragments.size(),
            _impu.c_str());

  { // New scope to avoid accidentally operating on the wrong SAS event.
    SAS::Event ev(trail, SASEvent::CALL_LIST_TRIM_STARTED, 0);
    ev.add_var_param(_impu);
    ev.add_static_param(_fragments.size());
    SAS::report_event(ev);
  }

  // The column name is of the form:
  //   call_<timestamp>_<id>_<type>
  //
  // For example:
  //   call_20140722120000_12345_begin
  std::vector<CassandraStore::RowColumns> to_delete;
  for (std::vector<CallFragment>::const_iterator ii = _fragments.begin();
       ii != _fragments.end();
       ii++)
  {
    std::string column_name;
    column_name.append(CALL_COLUMN_PREFIX)
               .append(ii->timestamp).append("_")
               .append(ii->id).append("_")
               .append(fragment_type_to_string(ii->type));
    std::map<std::string, std::string> columns;
    columns[column_name] = "";
    to_delete.push_back(CassandraStore::RowColumns(COLUMN_FAMILY, _impu, columns));
  }

  delete_columns(client,
                 to_delete,
                 _cass_timestamp);

  LOG_DEBUG("Successfully deleted call fragments");

  { // New scope to avoid accidentally operating on the wrong SAS event.
    SAS::Event ev(trail, SASEvent::CALL_LIST_TRIM_OK, 0);
    SAS::report_event(ev);
  }

  return true;
}

void DeleteOldCallFragments::unhandled_exception(CassandraStore::ResultCode status,
                                                 std::string& description,
                                                 SAS::TrailId trail)
{
  CassandraStore::Operation::unhandled_exception(status, description, trail);

  LOG_WARNING("Failed to delete old call list fragments for IMPU %s because '%s' (RC = %d)",
              _impu.c_str(), description.c_str(), status);
  sas_log_cassandra_failure(trail,
                            SASEvent::CALL_LIST_TRIM_FAILED,
                            status,
                            description);
}

DeleteOldCallFragments*
Store::new_delete_old_call_fragments_op(const std::string& impu,
                                        const std::vector<CallFragment> fragments,
                                        const int64_t cass_timestamp)
{
  return new DeleteOldCallFragments(impu, fragments, cass_timestamp);
}


//
// Wrappers for synchronous operations.
//

CassandraStore::ResultCode
Store::write_call_fragment_sync(const std::string& impu,
                                const CallFragment& fragment,
                                const int64_t cass_timestamp,
                                const int32_t ttl,
                                SAS::TrailId trail)
{
  WriteCallFragment* op = new_write_call_fragment_op(impu,
                                                     fragment,
                                                     cass_timestamp,
                                                     ttl);

  do_sync(op, trail);
  CassandraStore::ResultCode result = op->get_result_code();

  delete op; op = NULL;
  return result;
}


CassandraStore::ResultCode
Store::get_call_fragments_sync(const std::string& impu,
                               std::vector<CallFragment>& fragments,
                               SAS::TrailId trail)
{
  GetCallFragments* op = new_get_call_fragments_op(impu);

  if (do_sync(op, trail))
  {
    op->get_result(fragments);
  }

  CassandraStore::ResultCode result = op->get_result_code();

  delete op; op = NULL;
  return result;
}


CassandraStore::ResultCode
Store::delete_old_call_fragments_sync(const std::string& impu,
                                      const std::vector<CallFragment> fragments,
                                      const int64_t cass_timestamp,
                                      SAS::TrailId trail)
{
  DeleteOldCallFragments* op = new_delete_old_call_fragments_op(impu,
                                                                fragments,
                                                                cass_timestamp);
  do_sync(op, trail);
  CassandraStore::ResultCode result = op->get_result_code();

  delete op; op = NULL;
  return result;
}

} // namespace CallListStore

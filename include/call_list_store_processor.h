/**
 * @file call_list_store_processor.h
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

#ifndef CALL_LIST_STORE_PROCESSOR_H_
#define CALL_LIST_STORE_PROCESSOR_H_

#include "call_list_store.h"
#include "threadpool.h"
#include "load_monitor.h"
#include "sas.h"
#include "mementosasevent.h"

class CallListStoreProcessor
{
public:
  /// Constructor
  CallListStoreProcessor(LoadMonitor* load_monitor,
                         CallListStore::Store* call_list_store,
                         const int max_call_list_length,
                         const int memento_thread,
                         const int call_list_ttl);

  /// Destructor
  ~CallListStoreProcessor();

  /// This function constructs a Cassandra request to write a call to the
  /// call list store, It runs synchronously, so must be done in a
  /// separate thread to the memento processing (as that's on the call
  /// path).
  /// @param impu       IMPU
  /// @param timestamp  Timestamp of call list entry
  /// @param id         Id of call list entry
  /// @param type       Type of call fragment to write
  /// @param xml        Contents of call list entry
  /// @param trail      SAS trail
  virtual void write_call_list_entry(std::string impu,
                                     std::string timestamp,
                                     std::string id,
                                     CallListStore::CallFragment::Type type,
                                     std::string xml,
                                     SAS::TrailId trail);

  struct CallListEntry
  {
    Utils::StopWatch stop_watch;
    std::string impu;
    std::string timestamp;
    std::string id;
    CallListStore::CallFragment::Type type;
    std::string contents;
    SAS::TrailId trail;
  };

private:
  /// @class CallListStoreProcessorThreadPool
  /// The thread pool used by the call list store processor
  class CallListStoreProcessorThreadPool : public ThreadPool<CallListStoreProcessor::CallListEntry*>
  {
  public:
    /// Constructor.
    /// @param call_list_store      A pointer to the underlying call list store.
    /// @param load_monitor         A pointer to the load monitor.
    /// @param max_call_list_length Maximum number of complete calls to store
    /// @param call_list_ttl        TTL of call list store entries.
    /// @param num_threads          Number of memento worker threads to start
    /// @param max_queue            Max queue size to allow.
    CallListStoreProcessorThreadPool(CallListStore::Store* call_list_store,
                                     LoadMonitor* load_monitor,
                                     const int max_call_list_length,
				     const int call_list_ttl,
                                     unsigned int num_threads,
                                     unsigned int max_queue = 0);

    /// Destructor
    virtual ~CallListStoreProcessorThreadPool();

  private:
    /// Called by worker threads when they pull work off the queue.
    virtual void process_work(CallListStoreProcessor::CallListEntry*&);

    /// Performs call trim calculations
    /// @param impu            IMPU.
    /// @param cass_timestamp  Cassandra timestamp
    /// @param trail           SAS trail
    void perform_call_trim(std::string impu,
                           uint64_t cass_timestamp,
                           SAS::TrailId trail);

    /// Checks whether the call records should be retrieved from the call
    /// list store. Returns false if there's no maximum limit.
    /// @returns    - whether the call records should be retrieved from
    ///               the call list store.
    bool is_call_record_count_needed();

    /// Requests the stored calls from Cassandra. If the number of stored calls
    /// is too high, sets a timestamp to delete before to reduce the call
    /// list length.
    /// @param impu            IMPU.
    /// @param timestamp       Timestamp to pass to call list store
    /// @param trail           SAS trail
    bool is_call_trim_needed(std::string impu,
                             std::string& timestamp,
                             SAS::TrailId trail);

    /// Underlying call list store
    CallListStore::Store* _call_list_store;

    /// Load monitor
    LoadMonitor* _load_monitor;

    /// Maximum number of calls to store.
    int _max_call_list_length;

    /// Time to store calls in Cassandra.
    int _call_list_ttl;
  };

  friend class CallListStoreProcessorThreadPool;

  ///  Thread pool
  CallListStoreProcessorThreadPool* _thread_pool;
};

#endif

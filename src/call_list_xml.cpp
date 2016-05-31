/**
 * @file call_list_xml.cpp XML processing for call lists.
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


#include "call_list_xml.h"
#include "mementosasevent.h"
#include "rapidxml/rapidxml.hpp"
#include <set>

typedef CallListStore::CallFragment::Type FragmentType;

std::string xml_from_call_records(const std::vector<CallListStore::CallFragment>& records, SAS::TrailId trail)
{
  std::map<std::string, std::vector<CallListStore::CallFragment> > ids_to_records;

  // Group all entries by time and record ID
  for (std::vector<CallListStore::CallFragment>::const_iterator ii = records.begin();
       ii != records.end();
       ii++)
  {
    std::string record_id = ((ii->timestamp) + "_" + (ii->id));
    ids_to_records[record_id].push_back(*ii);
  }

  // Build an XML document from the valid call records, discarding any
  // that aren't a single REJECTED, a BEGIN/END pair, or which have
  // invalid XML.
  std::string final_xml = "<call-list><calls>";
  for (std::map<std::string, std::vector<CallListStore::CallFragment> >::const_iterator ii = ids_to_records.begin();
       ii != ids_to_records.end();
       ii++)
  {
    std::string record_id = ii->first;
    std::vector<CallListStore::CallFragment> record_fragments = ii->second;

    if (record_fragments.size() == 1)
    {
      // REJECTED is the only record type where having one fragment is valid
      if (record_fragments[0].type == FragmentType::REJECTED)
      {
        std::string xml = "<call>" + record_fragments[0].contents + "</call>";
        final_xml.append(xml);
      }
      else
      {
        SAS::Event invalid_record(trail, SASEvent::CALL_LIST_DB_INVALID_RECORD_1, 0);
        invalid_record.add_var_param(record_fragments[0].id);
        invalid_record.add_var_param(record_fragments[0].timestamp);
        invalid_record.add_static_param(record_fragments[0].type);
        SAS::report_event(invalid_record);

        TRC_WARNING("Only one entry for call record %s but it was not REJECTED", record_id.c_str());
      }
    }
    else if (record_fragments.size() == 2)
    {
      // If it's not a REJECTED record, it must be BEGIN and END
      if ((record_fragments[0].type == FragmentType::BEGIN) &&
          (record_fragments[1].type == FragmentType::END))
      {
        std::string xml = "<call>" + record_fragments[0].contents + record_fragments[1].contents + "</call>";
        final_xml.append(xml);
      }
      else
      {
        SAS::Event invalid_record(trail, SASEvent::CALL_LIST_DB_INVALID_RECORD_2, 0);
        invalid_record.add_var_param(record_fragments[0].id);
        invalid_record.add_var_param(record_fragments[0].timestamp);
        invalid_record.add_static_param(record_fragments[0].type);
        invalid_record.add_static_param(record_fragments[1].type);
        SAS::report_event(invalid_record);

        TRC_WARNING("Found two entries for call record %s but it was not a BEGIN followed by an END",
                    record_id.c_str());
      }
    }
    else
    {
      SAS::Event invalid_record(trail, SASEvent::CALL_LIST_DB_INVALID_RECORD, 0);

      // record_fragments should always be nonempty at this point, so exclude the
      // else branch from coverage
      if (!record_fragments.empty())
      {
        invalid_record.add_var_param(record_fragments[0].id);
        invalid_record.add_var_param(record_fragments[0].timestamp);
      }
      else
      {
        invalid_record.add_var_param(record_id); // LCOV_EXCL_LINE
        invalid_record.add_var_param(record_id); // LCOV_EXCL_LINE
      }
      invalid_record.add_static_param(record_fragments.size());
      SAS::report_event(invalid_record);

      TRC_WARNING("Found %d entries for call record %s, expected 1 (REJECTED) or 2 (BEGIN/END)",
                  record_fragments.size(),
                  record_id.c_str());
    }

  }

  final_xml.append("</calls></call-list>");

  return final_xml;
}


/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2017, Regents of the University of California.
 *
 * This file is part of ndncert, a certificate management system based on NDN.
 *
 * ndncert is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * ndncert is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received copies of the GNU General Public License along with
 * ndncert, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndncert authors and contributors.
 */

#include "json-helper.hpp"
#include <boost/lexical_cast.hpp>

namespace ndn {
namespace ndncert {

const JsonSection
genResponseProbeJson(const Name& identifier, const Name& caInformation)
{
  JsonSection root;

  root.put(JSON_IDNENTIFIER, identifier.toUri());
  root.put(JSON_CA_INFO, caInformation.toUri());

  return root;
}

const JsonSection
genResponseNewJson(const std::string& requestId, const std::string& status,
                   const std::list<std::string>& challenges)
{
  JsonSection root;
  JsonSection challengesSection;
  root.put(JSON_REQUEST_ID, requestId);
  root.put(JSON_STATUS, status);

  for (const auto& entry : challenges) {
    JsonSection challenge;
    challenge.put(JSON_CHALLENGE_TYPE, entry);
    challengesSection.push_back(std::make_pair("", challenge));
  }
  root.add_child(JSON_CHALLENGES, challengesSection);

  return root;
}

const JsonSection
genResponseChallengeJson(const std::string& requestId, const std::string& challengeType,
                         const std::string& status, const Name& name,
                         const std::map<std::string, std::string>& challengeData)
{
  JsonSection root;
  root.put(JSON_REQUEST_ID, requestId);
  root.put(JSON_CHALLENGE_TYPE, challengeType);
  root.put(JSON_STATUS, status);
  if (!name.empty()) {
    root.put(JSON_CERTIFICATE, name.toUri());
  }
  if (!challengeData.empty()) {
    JsonSection data;
    for (const auto& item : challengeData) {
      data.put(item.first, item.second);
    }
    root.put_child(JSON_CHALLENGE_DATA, data);
  }
  return root;
}

const JsonSection
genFailureJson(const std::string& requestId, const std::string& challengeType,
               const std::string& status, const std::string& failureInfo)
{
  JsonSection root;
  root.put(JSON_REQUEST_ID, requestId);
  root.put(JSON_CHALLENGE_TYPE, challengeType);
  root.put(JSON_STATUS, status);
  root.put(JSON_FAILURE_INFO, failureInfo);
  return root;
}

} // namespace ndncert
} // namespace ndn

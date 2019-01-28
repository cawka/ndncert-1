/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2017-2018, Regents of the University of California.
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

#include "client-module.hpp"
#include "challenge-module.hpp"
#include "invoke-client.hpp"

#include <iostream>
#include <string>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>
#include <ndn-cxx/security/verification-helpers.hpp>
#include <ndn-cxx/util/io.hpp>

//Declare function parameters as global variables
int m_index = 0;
std::string m_ca_prefix;
std::string m_user_identity;
std::string m_challenge;

namespace ndn {
namespace ndncert {

int nStep;

class ClientTool
{
public:
  ClientTool(ClientModule& clientModule)
    : client(clientModule)
  {
  }

  void
  errorCb(const std::string& errorInfo)
  {
    std::cerr << "Error: " << errorInfo << std::endl;
  }

  void
  certCb(const Certificate& cert)
  {
    //Retrieved CA certificate to use for <Data> packet verfications
    //io::save(targetCaItem.m_anchor,std::cout);
  }

  void
  downloadCb(const shared_ptr<RequestState>& state)
  {
    std::cerr << "Step " << nStep++
              << " DONE! Certificate has already been installed to local keychain\n";
    return;
  }

  void
  anchorCb(const Interest& request, const Data& reply,
           const ClientCaItem& anchorItem, const Name& assignedName)
  {
    auto contentJson = ClientModule::getJsonFromData(reply);
    auto caItem = ClientConfig::extractCaItem(contentJson);

    if (!security::verifySignature(caItem.m_anchor, anchorItem.m_anchor)) {
      std::cerr << "Fail to verify fetched anchor" << std::endl;
      return;
    }
    client.getClientConf().m_caItems.push_back(caItem);

    if (assignedName.toUri() != "/") {
      client.sendNew(caItem, assignedName,
                     bind(&ClientTool::newCb, this, _1),
                     bind(&ClientTool::errorCb, this, _1));
    }
    else {
      if (caItem.m_probe != "") {
        std::cerr << "Step " << nStep++ << ": Probe Requirement-" << caItem.m_probe << std::endl;
        std::string probeInfo;
        getline(std::cin, probeInfo);
        client.sendProbe(caItem, probeInfo,
                         bind(&ClientTool::newCb, this, _1),
                         bind(&ClientTool::errorCb, this, _1));
      }
      else {
        std::cerr << "Step " << nStep++ << ": Please type in the identity name\n";
        std::string nameComponent;
        getline(std::cin, nameComponent);
        Name identityName = caItem.m_caName.getPrefix(-1);
        identityName.append(nameComponent);
        client.sendNew(caItem, identityName,
                       bind(&ClientTool::newCb, this, _1),
                       bind(&ClientTool::errorCb, this, _1));
      }
    }
  }

  void
  listCb(const std::list<Name>& caList, const Name& assignedName, const Name& schema,
         const ClientCaItem& caItem)
  {
    if (assignedName.toUri() != "" && caList.size() == 1) {
      // with recommendation

      std::cerr << "Get recommended CA: " << caList.front()
                << "Get recommended Identity: " << assignedName << std::endl;
      client.requestCaTrustAnchor(caList.front(),
                                  bind(&ClientTool::anchorCb, this, _1, _2, caItem, assignedName),
                                  bind(&ClientTool::errorCb, this, _1));
    }
    else {
      // without recommendation
      int count = 0;
      for (auto name : caList) {
        std::cerr << "***************************************\n"
                  << "Index: " << count++ << "\n"
                  << "CA prefix:" << name << "\n"
                  << "***************************************\n";
      }
      std::cerr << "Select an index to apply for a certificate\n";

      std::string option;
      getline(std::cin, option);
      int caIndex = std::stoi(option);

      std::vector<Name> caVector{std::begin(caList), std::end(caList)};
      Name targetCaName = caVector[caIndex];

      client.requestCaTrustAnchor(targetCaName,
                                  bind(&ClientTool::anchorCb, this, _1, _2, caItem, Name("")),
                                  bind(&ClientTool::errorCb, this, _1));
    }
  }

  void
  validateCb(const shared_ptr<RequestState>& state)
  {
    //---Download certificate for no-challenge option, no PIN code verification was performed
    if (state->m_challengeType == "NOCHALL") {
      std::cerr << "DONE! Certificate has already been issued \n";
      client.requestDownload(state,
                             bind(&ClientTool::downloadCb, this, _1),
                             bind(&ClientTool::errorCb, this, _1));
      return;
    }

    if (state->m_status == ChallengeModule::SUCCESS) {
      std::cerr << "DONE! Certificate has already been issued \n";
      client.requestDownload(state,
                             bind(&ClientTool::downloadCb, this, _1),
                             bind(&ClientTool::errorCb, this, _1));
      return;
    }

    auto challenge = ChallengeModule::createChallengeModule(state->m_challengeType);
    auto requirementList = challenge->getRequirementForValidate(state->m_status);

    std::cerr << "Step " << nStep++ << ": Please satisfy following instruction(s)\n";
    for (auto requirement : requirementList) {
      std::cerr << "\t" << requirement << std::endl;
    }
    std::list<std::string> paraList;
    for (size_t i = 0; i < requirementList.size(); i++) {
      std::string tempParam;
      getline(std::cin, tempParam);
      paraList.push_back(tempParam);
    }
    auto paramJson = challenge->genValidateParamsJson(state->m_status, paraList);
    client.sendValidate(state, paramJson,
                        bind(&ClientTool::validateCb, this, _1),
                        bind(&ClientTool::errorCb, this, _1));
  }

  void
  selectCb(const shared_ptr<RequestState>& state)
  {
    auto challenge = ChallengeModule::createChallengeModule(state->m_challengeType);
    auto requirementList = challenge->getRequirementForValidate(state->m_status);

    if (state->m_challengeType != "NOCHALL") {
    	std::cerr << "Step " << nStep++ << ": Please satisfy following instruction(s)" << std::endl;
    }
    for (auto item : requirementList) {
      std::cerr << "\t" << item << std::endl;
    }
    std::list<std::string> paraList;
    for (size_t i = 0; i < requirementList.size(); i++) {
      if (state->m_status == "no-code") {
        //---Bypass user input for PIN code 
	paraList.push_back("00000");
      }
      else {
      	std::string tempParam;
      	getline(std::cin, tempParam);
      	paraList.push_back(tempParam);
      }
    }

    auto paramJson = challenge->genValidateParamsJson(state->m_status, paraList);
    client.sendValidate(state, paramJson,
                        bind(&ClientTool::validateCb, this, _1),
                        bind(&ClientTool::errorCb, this, _1));
  }

  void
  newCb(const shared_ptr<RequestState>& state)
  {
    //---Remove interruptive prompt
    //std::cerr << "Step " << nStep++ << ": Please select one challenge from following types\n";
    //for (auto item : state->m_challengeList) {
      //std::cerr << "\t" << item << std::endl;
    //}
    std::string choice;
    //---Remove interruptive prompt
    //getline(std::cin, choice);
    choice = m_challenge;

    auto challenge = ChallengeModule::createChallengeModule(choice);
    auto requirementList = challenge->getRequirementForSelect();
    std::list<std::string> paraList;
    if (requirementList.size() != 0) {
      std::cerr << "Step " << nStep++ << ": Please satisfy following instruction(s)\n";
      for (auto item : requirementList) {
        std::cerr << "\t" << item << std::endl;
      }
      for (size_t i = 0; i < requirementList.size(); i++) {
        std::string tempParam;
        getline(std::cin, tempParam);
        paraList.push_back(tempParam);
      }
    }
    auto paramJson = challenge->genSelectParamsJson(state->m_status, paraList);
    client.sendSelect(state, choice, paramJson,
                      bind(&ClientTool::selectCb, this, _1),
                      bind(&ClientTool::errorCb, this, _1));
  }

public:
  ClientModule& client;
};

int
main_entry()
{

  namespace po = boost::program_options;
  std::string configFilePath = std::string(SYSCONFDIR) + "/ndncert/client.conf";
  bool isIntra = false;
  po::options_description description("General Usage\n ndncert-client [-h] [-i] [-f]\n");
  description.add_options()
    ("help,h",
     "produce help message")
    ("intra-node,i",
     "optional, if specified, switch on the intra-node mode")
    ("config-file,f", po::value<std::string>(&configFilePath),
     "config file name");
  po::positional_options_description p;

  po::variables_map vm;

/* Disables command line argument parser
  try {
    //po::store(po::command_line_parser(argc, argv).options(description).positional(p).run(), vm);
    //po::notify(vm);
  }
  catch (const std::exception& e) {
    //---Allow command line arguments
    //std::cerr << "ERROR: " << e.what() << std::endl;
    //return 1; 
  }
*/

  if (vm.count("help") != 0) {
    std::cerr << description << std::endl;
    return 0;
  }
  if (vm.count("intra-node") != 0) {
    isIntra = true;
  }

  nStep = 0;
  Face face;
  security::v2::KeyChain keyChain;
  ClientModule client(face, keyChain);
  client.getClientConf().load(configFilePath);
  ClientTool tool(client);

  if (isIntra) {
    client.requestLocalhostList([&](const ClientConfig& config) {
        auto caList = config.m_caItems;
        int count = 0;
        for (auto item : caList) {

	  int pref_len = item.m_caName.toUri().size();
          std::string pref_name = item.m_caName.toUri();

          if (m_ca_prefix == pref_name.substr(0,pref_len-3)) {
                m_index = count;
          }

          std::cerr << "***************************************\n"
                    << "Index: " << count++ << "\n"
                    << "CA prefix:" << item.m_caName << "\n"
                    << "Introduction: " << item.m_caInfo << "\n"
                    << "***************************************\n";
        }
        std::vector<ClientCaItem> caVector{std::begin(caList), std::end(caList)};
        //---Remove interruptive prompt
        //std::cerr << "Step " << nStep++ << ": Please type in the CA namespace index that you want to apply\n";
        
        //---Remove interruptive prompt
        //getline(std::cin, caIndexS);
        
        int caIndex = m_index;
        BOOST_ASSERT(caIndex <= count);
        auto targetCaItem = caVector[caIndex];

        //Set the namespace to request certificate for
        targetCaItem.m_caName = m_ca_prefix + "/CA";

	//Get CA certificate to use for <Data> packet verifications
	client.sendCert(targetCaItem,
                           bind(&ClientTool::newCb, &tool, _1),
                           bind(&ClientTool::errorCb, &tool, _1),
			   bind(&ClientTool::certCb, &tool, _1));

        if (targetCaItem.m_probe != "") {
	  //---Remove interruptive prompt
          //std::cerr << "Step " << nStep++ << ": Probe Requirement-" << targetCaItem.m_probe << std::endl;
          std::string probeInfo;
	  //---Remove interruptive prompt
          //getline(std::cin, probeInfo);
	  probeInfo = m_user_identity;
          client.sendProbe(targetCaItem, probeInfo,
                           bind(&ClientTool::newCb, &tool, _1),
                           bind(&ClientTool::errorCb, &tool, _1));
        }
        else {
          std::cerr << "Step " << nStep++ << ": Please type in the identity name\n";
          std::string nameComponent;
          getline(std::cin, nameComponent);
          Name identityName = targetCaItem.m_caName.getPrefix(-1);
          identityName.append(nameComponent);
          client.sendNew(targetCaItem, identityName,
                         bind(&ClientTool::newCb, &tool, _1),
                         bind(&ClientTool::errorCb, &tool, _1));
        }
      },
      bind(&ClientTool::errorCb, &tool, _1));
  }
  else {
    // Inter-node Application
    bool listFirst = false;
    auto caList = client.getClientConf().m_caItems;
    int count = 0;
    for (auto item : caList) {

      int pref_len = item.m_caName.toUri().size();
      std::string pref_name = item.m_caName.toUri();
      if (m_ca_prefix == pref_name.substr(0,pref_len-3)) {
        m_index = count;
      }

      std::cerr << "***************************************\n"
                << "Index: " << count++ << "\n"
                << "CA prefix:" << item.m_caName << "\n"
                << "Introduction: " << item.m_caInfo << "\n"
                << "***************************************\n";
    }
    std::vector<ClientCaItem> caVector{std::begin(caList), std::end(caList)};
    //---Remove interruptive prompt
    //std::cerr << "Step " << nStep++ << ": Please type in the CA namespace index that you want to apply\n";
    
    //---Remove interruptive prompt
    //getline(std::cin, caIndexS);
    
    int caIndex = m_index;
    BOOST_ASSERT(caIndex <= count);
    auto targetCaItem = caVector[caIndex];

    //Set the namespace to request certificate for
    targetCaItem.m_caName = m_ca_prefix + "/CA";

    //Get CA certificate to use for <Data> packet verifications
    client.sendCert(targetCaItem,
                           bind(&ClientTool::newCb, &tool, _1),
                           bind(&ClientTool::errorCb, &tool, _1),
			   bind(&ClientTool::certCb, &tool, _1));

    if (targetCaItem.m_isListEnabled) {
      std::cerr << "This CA provides several sub-namepace CAs \n"
                << "Do you want to (A) get a certificate from " << targetCaItem.m_caName << "directly? \n"
                << "Or (B) get a certificate from one of its sub-namespace CAs? \n"
                << "Please type in your choice (A or B) \n";
      std::string listOption;
      getline(std::cin, listOption);
      if (listOption == "A" || listOption == "a") {
        listFirst = false;
      }
      else if (listOption == "B" || listOption == "b") {
        listFirst = true;
        std::string additionalInfo = "";
        if (targetCaItem.m_targetedList != "") {
          std::cerr << "Step " << nStep++
                    << ": Enter nothing if you want to see all available sub-namespace CAs"
                    << " or follow the instruction to get a recommended CA\n"
                    << "\t" << targetCaItem.m_targetedList << std::endl;
          getline(std::cin, additionalInfo);
        }
        client.requestList(targetCaItem, additionalInfo,
                           bind(&ClientTool::listCb, &tool, _1, _2, _3, targetCaItem),
                           bind(&ClientTool::errorCb, &tool, _1));
      }
      else {
        std::cerr << "Your input is not an option." << std::endl;
        return 1;
      }
    }
    if (!listFirst) {
      if (targetCaItem.m_probe != "") {
	//---Remove interruptive prompt
        //std::cerr << "Step " << nStep++ << ": Probe Requirement-" << targetCaItem.m_probe << std::endl;
        std::string probeInfo;
	//---Remove interruptive prompt
        //getline(std::cin, probeInfo);
	probeInfo = m_user_identity;
        client.sendProbe(targetCaItem, probeInfo,
                         bind(&ClientTool::newCb, &tool, _1),
                         bind(&ClientTool::errorCb, &tool, _1));
      }
      else {
        std::cerr << "Step " << nStep++ << ": Please type in the identity name\n";
        std::string nameComponent;
        getline(std::cin, nameComponent);
        Name identityName = targetCaItem.m_caName.getPrefix(-1);
        identityName.append(nameComponent);
        client.sendNew(targetCaItem, identityName,
                       bind(&ClientTool::newCb, &tool, _1),
                       bind(&ClientTool::errorCb, &tool, _1));
      }
    }
  }

  face.processEvents();
  return 0;
}


} // namespace ndncert
} // namespace ndn


//Define class method that starts client's main function call
InvokeClient::InvokeClient() {
}

int InvokeClient::CallClientMain(std::string p_ca_prefix, std::string p_user_identity, std::string p_challenge) {

	m_ca_prefix = p_ca_prefix;
        m_user_identity = p_user_identity;
        m_challenge = p_challenge;
	return ndn::ndncert::main_entry();
}
//End of class method definitions

int main(int argc, char* argv[])
{
	InvokeClient cl;
	return cl.CallClientMain("/ndn/edu/sh", "AP90", "NOCHALL");
}

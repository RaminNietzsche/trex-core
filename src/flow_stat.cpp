/*
  Ido Barnea
  Cisco Systems, Inc.
*/

/*
  Copyright (c) 2015-2016 Cisco Systems, Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/
#include <sstream>
#include <string>
#include <iostream>
#include <assert.h>
#include <os_time.h>
#include <internal_api/trex_platform_api.h>
#include "trex_stateless.h"
#include "trex_stream.h"
#include "flow_stat_parser.h"
#include "flow_stat.h"


#define FLOW_STAT_ADD_ALL_PORTS 255

static const uint16_t FREE_HW_ID = UINT16_MAX;

#ifdef __DEBUG_FUNC_ENTRY__
inline std::string methodName(const std::string& prettyFunction)
{
    size_t colons = prettyFunction.find("::");
    size_t begin = prettyFunction.substr(0,colons).rfind(" ") + 1;
    size_t end = prettyFunction.rfind("(") - begin;

    return prettyFunction.substr(begin,end) + "()";
}

#define __METHOD_NAME__ methodName(__PRETTY_FUNCTION__)
#define FUNC_ENTRY (std::cout << __METHOD_NAME__ << std::endl);
#else
#define FUNC_ENTRY
#endif

/************** class CFlowStatUserIdInfo ***************/
CFlowStatUserIdInfo::CFlowStatUserIdInfo(uint8_t proto) {
    memset(m_rx_counter, 0, sizeof(m_rx_counter));
    memset(m_rx_counter_base, 0, sizeof(m_rx_counter));
    memset(m_tx_counter, 0, sizeof(m_tx_counter));
    memset(m_tx_counter_base, 0, sizeof(m_tx_counter));
    m_hw_id = UINT16_MAX;
    m_proto = proto;
    m_ref_count = 1;
    m_trans_ref_count = 0;
}

std::ostream& operator<<(std::ostream& os, const class CFlowStatUserIdInfo& cf) {
    os << "hw_id:" << cf.m_hw_id << " proto:" << (uint16_t) cf.m_proto << " ref("
       << (uint16_t) cf.m_ref_count << "," << (uint16_t) cf.m_trans_ref_count << ")";
    os << " rx count (";
    os << cf.m_rx_counter[0];
    for (int i = 1; i < TREX_MAX_PORTS; i++) {
        os << "," << cf.m_rx_counter[i];
    }
    os << ")";
    os << " rx count base(";
    os << cf.m_rx_counter_base[0];
    for (int i = 1; i < TREX_MAX_PORTS; i++) {
        os << "," << cf.m_rx_counter_base[i];
    }
    os << ")";

    os << " tx count (";
    os << cf.m_tx_counter[0];
    for (int i = 1; i < TREX_MAX_PORTS; i++) {
        os << "," << cf.m_tx_counter[i];
    }
    os << ")";
    os << " tx count base(";
    os << cf.m_tx_counter_base[0];
    for (int i = 1; i < TREX_MAX_PORTS; i++) {
        os << "," << cf.m_tx_counter_base[i];
    }
    os << ")";

    return os;
}

int CFlowStatUserIdInfo::add_stream(uint8_t proto) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " proto:" << (uint16_t)proto << std::endl;
#endif

    if (proto != m_proto)
        return -1;

    m_ref_count++;

    return 0;
}

void CFlowStatUserIdInfo::reset_hw_id() {
    FUNC_ENTRY;

    m_hw_id = UINT16_MAX;
    // we are not attached to hw. Save packet count of session.
    // Next session will start counting from 0.
    for (int i = 0; i < TREX_MAX_PORTS; i++) {
        m_rx_counter_base[i] += m_rx_counter[i];
        m_rx_counter[i] = 0;
        m_tx_counter_base[i] += m_tx_counter[i];
        m_tx_counter[i] = 0;
    }
}
/************** class CFlowStatUserIdMap ***************/
CFlowStatUserIdMap::CFlowStatUserIdMap() {

}

std::ostream& operator<<(std::ostream& os, const CFlowStatUserIdMap& cf) {
    std::map<unsigned int, CFlowStatUserIdInfo*>::const_iterator it;
    for (it = cf.m_map.begin(); it != cf.m_map.end(); it++) {
        CFlowStatUserIdInfo *user_id_info = it->second;
        uint32_t user_id = it->first;
        os << "Flow stat user id info:\n";
        os << "  " << user_id << ":" << *user_id_info << std::endl;
    }
    return os;
}

uint16_t CFlowStatUserIdMap::get_hw_id(uint32_t user_id) {
    class CFlowStatUserIdInfo *cf = find_user_id(user_id);

    if (cf == NULL) {
        return FREE_HW_ID;
    } else {
        return cf->get_hw_id();
    }
}

class CFlowStatUserIdInfo *
CFlowStatUserIdMap::find_user_id(uint32_t user_id) {
    flow_stat_user_id_map_it_t it = m_map.find(user_id);

    if (it == m_map.end()) {
        return NULL;
    } else {
        return it->second;
    }
}

class CFlowStatUserIdInfo *
CFlowStatUserIdMap::add_user_id(uint32_t user_id, uint8_t proto) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << user_id << " proto:" << (uint16_t)proto
              << std::endl;
#endif

    class CFlowStatUserIdInfo *new_id = new CFlowStatUserIdInfo(proto);
    if (new_id != NULL) {
        std::pair<flow_stat_user_id_map_it_t, bool> ret;
        ret = m_map.insert(std::pair<uint32_t, class CFlowStatUserIdInfo *>(user_id, new_id));
        if (ret.second == false) {
            printf("%s Error: Trying to add user id %d which already exist\n", __func__, user_id);
            delete new_id;
            return NULL;
        }
        return new_id;
    } else {
        return NULL;
    }
}

int CFlowStatUserIdMap::add_stream(uint32_t user_id, uint8_t proto) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << user_id << " proto:" << (uint16_t)proto
              << std::endl;
#endif

    class CFlowStatUserIdInfo *c_user_id;

    c_user_id = find_user_id(user_id);
    if (! c_user_id) {
        c_user_id = add_user_id(user_id, proto);
        if (! c_user_id)
            return -1;
        return 0;
    } else {
        return c_user_id->add_stream(proto);
    }
}

int CFlowStatUserIdMap::del_stream(uint32_t user_id) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << user_id << std::endl;
#endif

    class CFlowStatUserIdInfo *c_user_id;

    c_user_id = find_user_id(user_id);
    if (! c_user_id) {
        return -1;
    }

    if (c_user_id->del_stream() == 0) {
        // ref count of this port became 0. can release this entry.
        m_map.erase(user_id);
        delete c_user_id;
    }

    return 0;
}

int CFlowStatUserIdMap::start_stream(uint32_t user_id, uint16_t hw_id) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << user_id << " hw_id:" << hw_id << std::endl;
#endif

    class CFlowStatUserIdInfo *c_user_id;

    c_user_id = find_user_id(user_id);
    if (! c_user_id) {
        fprintf(stderr, "%s Error: Trying to associate hw id %d to user_id %d but it does not exist\n"
                , __func__, hw_id, user_id);
        return -1;
    }

    if (c_user_id->is_hw_id()) {
        fprintf(stderr, "%s Error: Trying to associate hw id %d to user_id %d but it is already associate to %ld\n"
                , __func__, hw_id, user_id, c_user_id->get_hw_id());
        return -1;
    }
    c_user_id->set_hw_id(hw_id);
    c_user_id->add_started_stream();

    return 0;
}

int CFlowStatUserIdMap::start_stream(uint32_t user_id) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << user_id << std::endl;
#endif

    class CFlowStatUserIdInfo *c_user_id;

    c_user_id = find_user_id(user_id);
    if (! c_user_id) {
        fprintf(stderr, "%s Error: Trying to start stream on user_id %d but it does not exist\n"
                , __func__, user_id);
        return -1;
    }

    c_user_id->add_started_stream();

    return 0;
}

// return: negative number in case of error.
//         Number of started streams attached to used_id otherwise.
int CFlowStatUserIdMap::stop_stream(uint32_t user_id) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << user_id << std::endl;
#endif

    class CFlowStatUserIdInfo *c_user_id;

    c_user_id = find_user_id(user_id);
    if (! c_user_id) {
        fprintf(stderr, "%s Error: Trying to stop stream on user_id %d but it does not exist\n"
                , __func__, user_id);
        return -1;
    }

    return c_user_id->stop_started_stream();
}

bool CFlowStatUserIdMap::is_started(uint32_t user_id) {
    class CFlowStatUserIdInfo *c_user_id;

    c_user_id = find_user_id(user_id);
    if (! c_user_id) {
        return false;
    }

    return c_user_id->is_started();
}

uint8_t CFlowStatUserIdMap::l4_proto(uint32_t user_id) {
    class CFlowStatUserIdInfo *c_user_id;

    c_user_id = find_user_id(user_id);
    if (! c_user_id) {
        return 0;
    }

    return c_user_id->get_proto();
}

uint16_t CFlowStatUserIdMap::unmap(uint32_t user_id) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << user_id << std::endl;
#endif

    class CFlowStatUserIdInfo *c_user_id;

    c_user_id = find_user_id(user_id);
    if (! c_user_id) {
        return UINT16_MAX;
    }
    uint16_t old_hw_id = c_user_id->get_hw_id();
    c_user_id->reset_hw_id();

    return old_hw_id;
}

/************** class CFlowStatHwIdMap ***************/
CFlowStatHwIdMap::CFlowStatHwIdMap() {
    m_num_free = MAX_FLOW_STATS;
    for (int i = 0; i < MAX_FLOW_STATS; i++) {
        m_map[i] = FREE_HW_ID;
    }
}

std::ostream& operator<<(std::ostream& os, const CFlowStatHwIdMap& cf) {
    int count = 0;

    os << "HW id map:\n";
    os << "  num free:" << cf.m_num_free << std::endl;
    for (int i = 0; i < MAX_FLOW_STATS; i++) {
        if (cf.m_map[i] != 0) {
            count++;
            os << "(" << i << ":" << cf.m_map[i] << ")";
            if (count == 10) {
                os << std::endl;
                count = 0;
            }
        }
    }

    return os;
}

uint16_t CFlowStatHwIdMap::find_free_hw_id() {
    for (int i = 0; i < MAX_FLOW_STATS; i++) {
        if (m_map[i] == FREE_HW_ID)
            return i;
    }

    return FREE_HW_ID;
}

void CFlowStatHwIdMap::map(uint16_t hw_id, uint32_t user_id) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " hw id:" << hw_id << " user id:" << user_id << std::endl;
#endif

    m_map[hw_id] = user_id;
    m_num_free--;
}

void CFlowStatHwIdMap::unmap(uint16_t hw_id) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " hw id:" << hw_id << std::endl;
#endif

    m_map[hw_id] = FREE_HW_ID;
    m_num_free++;
}

/************** class CFlowStatRuleMgr ***************/
CFlowStatRuleMgr::CFlowStatRuleMgr() {
    m_api = NULL;
}

std::ostream& operator<<(std::ostream& os, const CFlowStatRuleMgr& cf) {
    os << "Flow stat rule mgr (" << cf.m_num_ports << ") ports:" << std::endl;
    os << cf.m_hw_id_map;
    os << cf.m_user_id_map;
    return os;
}

int CFlowStatRuleMgr::compile_stream(const TrexStream * stream, Cxl710Parser &parser) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << stream->m_rx_check.m_user_id << " en:";
    std::cout << stream->m_rx_check.m_enabled << std::endl;
#endif

    // currently we support only IP ID rule types
    // all our ports are the same type, so testing port 0 is enough
    uint16_t num_counters, capabilities;
    m_api->get_interface_stat_info(0, num_counters, capabilities);    
    if ((capabilities & TrexPlatformApi::IF_STAT_IPV4_ID) == 0) {
        return -2;
    }
        
    if (parser.parse(stream->m_pkt.binary, stream->m_pkt.len) != 0) {
        // if we could not parse the packet, but no stat count needed, it is probably OK.
        if (stream->m_rx_check.m_enabled) {
            fprintf(stderr, "Error: %s - Compilation failed\n", __func__);
            return -1;
        } else {
            return 0;
        }
    }

    if (!parser.is_fdir_supported()) {
        if (stream->m_stream_id <= 0) {
            // rx stat not needed. Do nothing.
            return 0;
        } else {
            // rx stat needed, but packet format is not supported
            fprintf(stderr, "Error: %s - Unsupported packet format for rx stat\n", __func__);
            return -1;
        }
    }
    return 0;
}

int CFlowStatRuleMgr::add_stream(const TrexStream * stream) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << stream->m_rx_check.m_user_id << std::endl;
#endif

    if (! m_api ) {
        TrexStateless *tstateless = get_stateless_obj();
        m_api = tstateless->get_platform_api();
        //    m_api = get_stateless_obj()->get_platform_api();    
        m_api->get_port_num(m_num_ports);
    }
    
    Cxl710Parser parser;
    int ret;

    if (! stream->m_rx_check.m_enabled) {
        return 0;
    }

    if ((ret = compile_stream(stream, parser)) < 0)
        return ret;

    uint8_t l4_proto;
    if (parser.get_l4_proto(l4_proto) < 0) {
        printf("Error: %s failed finding l4 proto\n", __func__);
        return -1;
    }

    return m_user_id_map.add_stream(stream->m_rx_check.m_user_id, l4_proto);
}

int CFlowStatRuleMgr::del_stream(const TrexStream * stream) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << stream->m_rx_check.m_user_id << std::endl;
#endif

    if (! stream->m_rx_check.m_enabled) {
        return 0;
    }

    return m_user_id_map.del_stream(stream->m_rx_check.m_user_id);
}

// called on all streams, when stream start to transmit
// If stream need flow stat counting, make sure the type of packet is supported, and
// embed needed info in packet.
// If stream does not need flow stat counting, make sure it does not interfere with
// other streams that do need stat counting.
// Might change the IP ID of the stream packet
int CFlowStatRuleMgr::start_stream(TrexStream * stream) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << stream->m_rx_check.m_user_id << std::endl;
#endif

    Cxl710Parser parser;
    int ret;

    if (! m_api ) {
        return 0;
    }

    if ((ret = compile_stream(stream, parser)) < 0)
        return ret;

    // first handle streams that do not need rx stat
    if (! stream->m_rx_check.m_enabled) {
        // no need for stat count
        uint16_t ip_id;
        if (parser.get_ip_id(ip_id) < 0) {
            return 0; // if we could not find and ip id, no need to fix
        }
        // verify no reserved IP_ID used, and change if needed
        if (ip_id >= IP_ID_RESERVE_BASE) {
            if (parser.set_ip_id(ip_id & 0xefff) < 0) {
                return -1;
            }
        }
        return 0;
    }

    // from here, we know the stream need rx stat
    if (m_user_id_map.is_started(stream->m_rx_check.m_user_id)) {
        m_user_id_map.start_stream(stream->m_rx_check.m_user_id); // just increase ref count;
    } else {
        uint16_t hw_id = m_hw_id_map.find_free_hw_id();
        if (hw_id == FREE_HW_ID) {
            printf("Error: %s failed finding free hw_id\n", __func__);
            return -1;
        } else {
            uint32_t user_id = stream->m_rx_check.m_user_id;
            m_user_id_map.start_stream(user_id, hw_id);
            m_hw_id_map.map(hw_id, user_id);
            add_hw_rule(hw_id, m_user_id_map.l4_proto(user_id));
        }
    }

    uint16_t hw_id = m_user_id_map.get_hw_id(stream->m_rx_check.m_user_id); // can't fail if we got here
    parser.set_ip_id(IP_ID_RESERVE_BASE + hw_id);

    return 0;
}

int CFlowStatRuleMgr::add_hw_rule(uint16_t hw_id, uint8_t proto) {    
    for (int port = 0; port < m_num_ports; port++) {
        m_api->add_rx_flow_stat_rule(port, FLOW_STAT_RULE_TYPE_IPV4_ID, proto, hw_id);
    }

    return 0;
}

int CFlowStatRuleMgr::stop_stream(const TrexStream * stream) {
#ifdef __DEBUG_FUNC_ENTRY__
    std::cout << __METHOD_NAME__ << " user id:" << stream->m_rx_check.m_user_id << std::endl;
#endif
    if (! stream->m_rx_check.m_enabled) {
        return 0;
    }
    if (! m_api ) {
        return 0;
    }

    if (m_user_id_map.stop_stream(stream->m_rx_check.m_user_id) == 0) {
        // last stream associated with the entry stopped transmittig.
        // remove user_id <--> hw_id mapping
        uint8_t proto = m_user_id_map.l4_proto(stream->m_rx_check.m_user_id);
        uint16_t hw_id = m_user_id_map.unmap(stream->m_rx_check.m_user_id);
        if (hw_id >= MAX_FLOW_STATS) {
            fprintf(stderr, "Error: %s got wrong hw_id %d from unmap\n", __func__, hw_id);
            return -1;
        } else {
            // update counters, and reset before unmapping
            class CFlowStatUserIdInfo *p_user_id = m_user_id_map.find_user_id(m_hw_id_map.get_user_id(hw_id));
            assert(p_user_id != NULL);
            uint64_t counter;
            for (uint8_t port = 0; port < m_num_ports; port++) {
                m_api->del_rx_flow_stat_rule(port, FLOW_STAT_RULE_TYPE_IPV4_ID, proto, hw_id);
                m_api->get_rx_stats(port, &counter, hw_id, true);
                p_user_id->set_rx_counter(port, counter);
                p_user_id->set_tx_counter(port, counter); //??? until tx work, just set for same value
            }
            m_hw_id_map.unmap(hw_id);
        }
    }
    return 0;
}

// return false if no counters changed since last run. true otherwise
bool CFlowStatRuleMgr::dump_json(std::string & json) {
    uint64_t stats[TREX_FDIR_STAT_SIZE];
    Json::FastWriter writer;
    Json::Value root;
    bool ret = false;

    if (! m_api ) {
        return false;
    }
    root["name"] = "rx-stats";
    root["type"] = 0;
    Json::Value &data_section = root["data"];

    // read hw counters, and update
    data_section["timestamp"] = Json::Value::UInt64(os_get_hr_tick_64());
    for (uint8_t port = 0; port < m_num_ports; port++) {
        int rc = m_api->get_rx_stats(port, stats, -1, false);
        if (rc == -1) {
            continue;
        }

        for (int i = 0; i < TREX_FDIR_STAT_SIZE; i++) {
            if (stats[i] != 0) {
                m_user_id_map.find_user_id(m_hw_id_map.get_user_id(i))->set_rx_counter(port, stats[i]);
                m_user_id_map.find_user_id(m_hw_id_map.get_user_id(i))->set_tx_counter(port, stats[i]);  //??? until tx work, just set for same value
            }
        }
    }

    // build json report
    flow_stat_user_id_map_it_t it;
    for (it = m_user_id_map.begin(); it != m_user_id_map.end(); it++) {
        CFlowStatUserIdInfo *user_id_info = it->second;
        uint32_t user_id = it->first;
        std::string str_user_id = static_cast<std::ostringstream*>( &(std::ostringstream()
                                                                      << user_id) )->str();
        for (uint8_t port = 0; port < m_num_ports; port++) {
            if ((user_id_info->get_tx_counter(port) != 0) || (user_id_info->get_rx_counter(port) != 0)) {
                std::string str_port = static_cast<std::ostringstream*>( &(std::ostringstream()
                                                                           << port) )->str();
                data_section[str_user_id]["rx"][str_port] = Json::Value::UInt64(user_id_info->get_rx_counter(port));
                data_section[str_user_id]["tx"][str_port] = Json::Value::UInt64(user_id_info->get_tx_counter(port));
                ret = true;
            }
        }
    }

    json = writer.write(root);
    return ret;
}

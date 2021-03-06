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

#ifndef __FLOW_STAT_H__
#define __FLOW_STAT_H__
#include <stdio.h>
#include <string>
#include <map>
#include "trex_defs.h"

#define MAX_FLOW_STATS 128
// range reserved for rx stat measurement is from IP_ID_RESERVE_BASE to 0xffff
// Do not change this value. In i350 cards, we filter according to first byte of IP ID
// In other places, we identify packets by if (ip_id > IP_ID_RESERVE_BASE)
#define IP_ID_RESERVE_BASE 0xff00

typedef std::map<uint32_t, uint16_t> flow_stat_map_t;
typedef std::map<uint32_t, uint16_t>::iterator flow_stat_map_it_t;

class CPhyEthIF;
class Cxl710Parser;

class CFlowStatUserIdInfo {
 public:
    CFlowStatUserIdInfo(uint8_t proto);
    friend std::ostream& operator<<(std::ostream& os, const CFlowStatUserIdInfo& cf);
    void set_rx_counter(uint8_t port, uint64_t val) {m_rx_counter[port] = val;}
    uint64_t get_rx_counter(uint8_t port) {return m_rx_counter[port] + m_rx_counter_base[port];}
    void set_tx_counter(uint8_t port, uint64_t val) {m_tx_counter[port] = val;}
    uint64_t get_tx_counter(uint8_t port) {return m_tx_counter[port] + m_tx_counter_base[port];}
    void set_hw_id(uint16_t hw_id) {m_hw_id = hw_id;}
    uint64_t get_hw_id() {return m_hw_id;}
    void reset_hw_id();
    bool is_hw_id() {return (m_hw_id != UINT16_MAX);}
    uint64_t get_proto() {return m_proto;}
    uint8_t get_ref_count() {return m_ref_count;}
    int add_stream(uint8_t proto);
    int del_stream() {m_ref_count--; return m_ref_count;}
    void add_started_stream() {m_trans_ref_count++;}
    int stop_started_stream() {m_trans_ref_count--; return m_trans_ref_count;}
    bool is_started() {return (m_trans_ref_count != 0);}

 private:
    uint64_t m_rx_counter[TREX_MAX_PORTS]; // How many packets received with this user id since stream start
    // How many packets received with this user id, since stream creation, before stream start.
    uint64_t m_rx_counter_base[TREX_MAX_PORTS];
    uint64_t m_tx_counter[TREX_MAX_PORTS]; // How many packets transmitted with this user id since stream start
    // How many packets transmitted with this user id, since stream creation, before stream start.
    uint64_t m_tx_counter_base[TREX_MAX_PORTS];
    uint16_t m_hw_id;     // Associated hw id. UINT16_MAX if no associated hw id.
    uint8_t m_proto;      // protocol (UDP, TCP, other), associated with this user id.
    uint8_t m_ref_count;  // How many streams with this ref count exists
    uint8_t m_trans_ref_count;  // How many streams with this ref count currently transmit
};

typedef std::map<uint32_t, class CFlowStatUserIdInfo *> flow_stat_user_id_map_t;
typedef std::map<uint32_t, class CFlowStatUserIdInfo *>::iterator flow_stat_user_id_map_it_t;

class CFlowStatUserIdMap {
 public:
    CFlowStatUserIdMap();
    friend std::ostream& operator<<(std::ostream& os, const CFlowStatUserIdMap& cf);
    uint16_t get_hw_id(uint32_t user_id);
    class CFlowStatUserIdInfo * find_user_id(uint32_t user_id);
    class CFlowStatUserIdInfo * add_user_id(uint32_t user_id, uint8_t proto);
    int add_stream(uint32_t user_id, uint8_t proto);
    int del_stream(uint32_t user_id);
    int start_stream(uint32_t user_id, uint16_t hw_id);
    int start_stream(uint32_t user_id);
    int stop_stream(uint32_t user_id);
    bool is_started(uint32_t user_id);
    uint8_t l4_proto(uint32_t user_id);
    uint16_t unmap(uint32_t user_id);
    flow_stat_user_id_map_it_t begin() {return m_map.begin();}
    flow_stat_user_id_map_it_t end() {return m_map.end();}
 private:
    flow_stat_user_id_map_t m_map;
};

class CFlowStatHwIdMap {
 public:
    CFlowStatHwIdMap();
    friend std::ostream& operator<<(std::ostream& os, const CFlowStatHwIdMap& cf);
    uint16_t find_free_hw_id();
    void map(uint16_t hw_id, uint32_t user_id);
    void unmap(uint16_t hw_id);
    uint32_t get_user_id(uint16_t hw_id) {return m_map[hw_id];};
 private:
    uint32_t m_map[MAX_FLOW_STATS]; // translation from hw id to user id
    uint16_t m_num_free; // How many free entries in the m_rules array
};

class CFlowStatRuleMgr {
 public:
    enum flow_stat_rule_types_e {
        FLOW_STAT_RULE_TYPE_NONE,
        FLOW_STAT_RULE_TYPE_IPV4_ID,
        FLOW_STAT_RULE_TYPE_PAYLOAD,
        FLOW_STAT_RULE_TYPE_IPV6_FLOW_LABEL,
    };

    CFlowStatRuleMgr();
    friend std::ostream& operator<<(std::ostream& os, const CFlowStatRuleMgr& cf);
    int add_stream(const TrexStream * stream);
    int del_stream(const TrexStream * stream);
    int start_stream(TrexStream * stream);
    int stop_stream(const TrexStream * stream);
    bool dump_json(std::string & json);

 private:
    int compile_stream(const TrexStream * stream, Cxl710Parser &parser);
    int add_hw_rule(uint16_t hw_id, uint8_t proto);

 private:
    class CFlowStatHwIdMap m_hw_id_map; // map hw ids to user ids
    class CFlowStatUserIdMap m_user_id_map; // map user ids to hw ids
    uint8_t m_num_ports; // How many ports are being used
    const TrexPlatformApi *m_api;
};

#endif
